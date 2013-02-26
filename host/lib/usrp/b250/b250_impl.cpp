//
// Copyright 2013 Ettus Research LLC
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#include "b250_impl.hpp"
#include "b250_regs.hpp"
#include <uhd/utils/static.hpp>
#include <uhd/utils/msg.hpp>
#include <uhd/utils/images.hpp>
#include <uhd/transport/if_addrs.hpp>
#include <boost/foreach.hpp>
#include <boost/asio.hpp>
#include <boost/functional/hash.hpp>
#include <fstream>

using namespace uhd;
using namespace uhd::transport;
namespace asio = boost::asio;

/***********************************************************************
 * Discovery over the udp transport
 **********************************************************************/
static device_addrs_t b250_find_with_addr(const device_addr_t &dev_addr)
{
    udp_simple::sptr comm = udp_simple::make_broadcast(
        dev_addr["addr"], BOOST_STRINGIZE(B250_FW_COMMS_UDP_PORT));

    //load request struct
    b250_fw_comms_t request = b250_fw_comms_t();
    request.flags = uhd::htonx<boost::uint32_t>(B250_FW_COMMS_FLAGS_ACK);
    request.sequence = 0;

    //send request
    comm->send(asio::buffer(&request, sizeof(request)));

    //loop for replies until timeout
    device_addrs_t addrs;
    while (true)
    {
        char buff[B250_FW_COMMS_MTU] = {};
        const size_t nbytes = comm->recv(asio::buffer(buff));
        if (nbytes == 0) break;
        device_addr_t new_addr;
        new_addr["type"] = "b250";
        new_addr["addr"] = comm->get_recv_addr();
        addrs.push_back(new_addr);
    }

    return addrs;
}

static device_addrs_t b250_find(const device_addr_t &hint)
{
    device_addrs_t addrs;
    if (hint.has_key("type") and hint["type"] != "b250") return addrs;

    //use the address given
    if (hint.has_key("addr"))
    {
        device_addrs_t reply_addrs = b250_find_with_addr(hint);
        BOOST_FOREACH(const device_addr_t &reply_addr, reply_addrs)
        {
            if (b250_find_with_addr(reply_addr).empty()) continue;
            addrs.push_back(reply_addr);
        }
        return addrs;
    }

    //otherwise, no address was specified, send a broadcast on each interface
    BOOST_FOREACH(const if_addrs_t &if_addrs, get_if_addrs())
    {
        //avoid the loopback device
        if (if_addrs.inet == asio::ip::address_v4::loopback().to_string()) continue;

        //create a new hint with this broadcast address
        device_addr_t new_hint = hint;
        new_hint["addr"] = if_addrs.bcast;

        //call discover with the new hint and append results
        device_addrs_t new_addrs = b250_find(new_hint);
        addrs.insert(addrs.begin(), new_addrs.begin(), new_addrs.end());
    }

    return addrs;
}

/***********************************************************************
 * Make
 **********************************************************************/
static device::sptr b250_make(const device_addr_t &device_addr)
{
    return device::sptr(new b250_impl(device_addr));
}

UHD_STATIC_BLOCK(register_b250_device)
{
    device::register_device(&b250_find, &b250_make);
}

static void b250_load_fw(const std::string &addr, const std::string &file_name)
{
    udp_simple::sptr comm = udp_simple::make_connected(
        addr, BOOST_STRINGIZE(B250_FW_COMMS_UDP_PORT));

    UHD_MSG(status) << "Loading firmware " << file_name << std::flush;

    //load file into memory
    std::ifstream fw_file(file_name.c_str());
    boost::uint32_t fw_file_buff[B250_FW_NUM_BYTES/sizeof(boost::uint32_t)];
    fw_file.read((char *)fw_file_buff, sizeof(fw_file_buff));
    fw_file.close();

    //poke the fw words into the upper bootram half
    size_t seq = 0;
    for (size_t i = 0; i < B250_FW_NUM_BYTES; i+=sizeof(boost::uint32_t))
    {
        //do ack for occasional backpressure
        const bool ack = (i & 0xf) == 0;

        //load request struct
        b250_fw_comms_t request = b250_fw_comms_t();
        request.flags = uhd::htonx<boost::uint32_t>(B250_FW_COMMS_FLAGS_POKE32 | (ack?B250_FW_COMMS_FLAGS_ACK : 0));
        request.sequence = uhd::htonx<boost::uint32_t>(seq++);
        request.addr = uhd::htonx<boost::uint32_t>(B250_FW_NUM_BYTES+i);
        request.data = uhd::htonx(uhd::byteswap(fw_file_buff[i/sizeof(boost::uint32_t)]));

        //send request
        comm->send(asio::buffer(&request, sizeof(request)));

        //do ack for occasional backpressure
        char buff[B250_FW_COMMS_MTU] = {};
        if (ack) comm->recv(asio::buffer(buff));

        if ((i & 0xfff) == 0) UHD_MSG(status) << "." << std::flush;
    }

    UHD_MSG(status) << " done!" << std::endl;
}

b250_impl::b250_impl(const uhd::device_addr_t &dev_addr)
{
    _tree = uhd::property_tree::make();

    //extract the FW path for the B250
    //and live load fw over ethernet link
    const std::string b250_fw_image = find_image_path(
        dev_addr.has_key("fw")? dev_addr["fw"] : B250_FW_FILE_NAME
    );
    b250_load_fw(dev_addr["addr"], b250_fw_image);

    //create basic communication
    _zpu_ctrl.reset(new b250_ctrl_iface(udp_simple::make_connected(dev_addr["addr"], BOOST_STRINGIZE(B250_FW_COMMS_UDP_PORT))));
    _zpu_spi = spi_core_3000::make(_zpu_ctrl, SR_ADDR(SET0_BASE, ZPU_SR_SPI), SR_ADDR(SET0_BASE, ZPU_RB_SPI));
    this->setup_ad9510_clock(_zpu_spi);

    this->setup_crossbar_router();

    //create radio0 control
    udp_zero_copy::sptr r0_ctrl_xport = this->make_transport(dev_addr["addr"], B200_R0_CTRL_SID);
    _radio_ctrl0 = b250_ctrl::make(r0_ctrl_xport, B200_R0_CTRL_SID);
    _radio_ctrl0->poke32(TOREG(SR_MISC_OUTS), (1 << 2)); //reset adc + dac
    _radio_ctrl0->poke32(TOREG(SR_MISC_OUTS), (1 << 1) | (1 << 0)); //out of reset + dac enable

    //sleep(5);
    this->register_loopback_self_test();

    _radio_spi0 = spi_core_3000::make(_radio_ctrl0, TOREG(SR_SPI), RB32_SPI);
    _adc_ctrl0 = b250_adc_ctrl::make(_radio_spi0, DB_ADC_SEN);
    this->set_ad9146_dac(_radio_spi0);

}

b250_impl::~b250_impl(void)
{
    //NOP
}

uhd::transport::udp_zero_copy::sptr b250_impl::make_transport(const std::string &addr, const boost::uint32_t sid)
{
    //make a new transport - fpga has no idea how to talk to use on this yet
    udp_zero_copy::sptr xport = udp_zero_copy::make(addr, BOOST_STRINGIZE(B250_VITA_UDP_PORT));

    //clear the ethernet dispatcher's udp port
    _zpu_ctrl->poke32(SR_ADDR(SET0_BASE, (ZPU_SR_ETHINT0+8+3)), 0);

    //send a mini packet with SID into the ZPU
    //ZPU will reprogram the ethernet framer
    managed_send_buffer::sptr mb = xport->get_send_buff();
    mb->cast<boost::uint32_t *>()[0] = uhd::htonx(sid);
    mb->commit(4);
    mb.reset();

    //reprogram the ethernet dispatcher's udp port
    _zpu_ctrl->poke32(SR_ADDR(SET0_BASE, (ZPU_SR_ETHINT0+8+3)), B250_VITA_UDP_PORT);

    return xport;
}

void b250_impl::setup_crossbar_router(void)
{
    _zpu_ctrl->poke32(SR_ADDR(SET0_BASE, ZPU_SR_XB_LOCAL), FPGA_ADDR);
    _zpu_ctrl->poke32(SR_ADDR(SETXB_BASE, 256 + (RADIO0_CTRL_DST & 0xff)), XB_DST_R0);
    _zpu_ctrl->poke32(SR_ADDR(SETXB_BASE, 0   + (RADIO0_CTRL_SRC & 0xff)), XB_DST_E0);
}

void b250_impl::register_loopback_self_test(void)
{
    bool test_fail = false;
    UHD_MSG(status) << "Performing register loopback test... " << std::flush;
    size_t hash = time(NULL);
    for (size_t i = 0; i < 100; i++)
    {
        boost::hash_combine(hash, i);
        _radio_ctrl0->poke32(TOREG(SR_TEST), boost::uint32_t(hash));
        test_fail = _radio_ctrl0->peek32(RB32_TEST) != boost::uint32_t(hash);
        if (test_fail) break; //exit loop on any failure
    }
    UHD_MSG(status) << ((test_fail)? " fail" : "pass") << std::endl;
}

void b250_impl::setup_ad9510_clock(spi_core_3000::sptr iface)
{
    #define write_ad9510_reg(addr, data) \
        iface->write_spi(1, spi_config_t::EDGE_RISE, ((addr) << 8) | (data), 24)
    write_ad9510_reg(0x3C, 0x08); // TEST_CLK on
    write_ad9510_reg(0x3D, 0x02); // NC off
    write_ad9510_reg(0x3E, 0x08); // RX_CLK on
    write_ad9510_reg(0x3F, 0x08); // TX_CLK on
    write_ad9510_reg(0x40, 0x02); // FPGA_CLK on
    write_ad9510_reg(0x41, 0x01); // NC off
    write_ad9510_reg(0x42, 0x01); // MIMO off
    write_ad9510_reg(0x43, 0x01); // NC off
    write_ad9510_reg(0x49, 0x80); // TEST_CLK bypass div
    write_ad9510_reg(0x4B, 0x80); // NC bypass div
    write_ad9510_reg(0x4D, 0x80); // RX_CLK bypass div
    write_ad9510_reg(0x4F, 0x80); // TX_CLK bypass div
    write_ad9510_reg(0x51, 0x80); // FPGA_CLK bypass div
    write_ad9510_reg(0x53, 0x80); // NC bypass div
    write_ad9510_reg(0x55, 0x80); // MIMO bypass div
    write_ad9510_reg(0x57, 0x80); // NC bypass div
    write_ad9510_reg(0x5a, 0x01); // Apply settings
}

void b250_impl::set_ad9146_dac(spi_core_3000::sptr iface)
{
    spi_config_t spi_config(spi_config_t::EDGE_FALL);
    spi_config.mosi_edge = spi_config_t::EDGE_RISE;
    spi_config.miso_edge = spi_config_t::EDGE_RISE;

    #define write_ad9146_reg(addr, data) \
        iface->write_spi(DB_DAC_SEN, spi_config, ((addr) << 8) | (data), 16)
    #define read_ad9146_reg(addr) \
        (iface->read_spi(DB_DAC_SEN, spi_config, ((addr) << 8) | (1 << 15), 16) & 0xffff)
    //
    write_ad9146_reg(0x0, 1 << 5); //reset
    write_ad9146_reg(0x0, 1 << 7); //config + out of reset
    write_ad9146_reg(0x1, 0x0); //out of power down
    UHD_MSG(status) << std::hex << read_ad9146_reg(0x7f) << std::endl;
    UHD_MSG(status) << std::hex << read_ad9146_reg(0x49) << std::endl;
    UHD_MSG(status) << std::hex << read_ad9146_reg(0x4A) << std::endl;
    UHD_MSG(status) << std::hex << read_ad9146_reg(0xE) << std::endl;
    UHD_MSG(status) << std::hex << read_ad9146_reg(0xF) << std::endl;
    UHD_MSG(status) << std::hex << read_ad9146_reg(0x0) << std::endl;
    write_ad9146_reg(0x8, 0x3f);
    UHD_MSG(status) << std::hex << read_ad9146_reg(0x8) << std::endl;
}
