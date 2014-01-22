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

#include "x300_impl.hpp"
#include "x300_regs.hpp"
#include "x300_lvbitx.hpp"
#include "x310_lvbitx.hpp"
#include <boost/algorithm/string.hpp>
#include <boost/asio.hpp>
#include "apply_corrections.hpp"
#include <uhd/utils/static.hpp>
#include <uhd/utils/msg.hpp>
#include <uhd/utils/images.hpp>
#include <uhd/utils/safe_call.hpp>
#include <uhd/usrp/subdev_spec.hpp>
#include <uhd/transport/if_addrs.hpp>
#include <boost/foreach.hpp>
#include <boost/bind.hpp>
#include <boost/functional/hash.hpp>
#include <boost/assign/list_of.hpp>
#include <fstream>
#include <uhd/transport/udp_zero_copy.hpp>
#include <uhd/transport/nirio_zero_copy.hpp>
#include <uhd/transport/nirio/niusrprio_session.h>
#include <uhd/utils/platform.hpp>

#define NIUSRPRIO_DEFAULT_RPC_PORT "5444"

using namespace uhd;
using namespace uhd::usrp;
using namespace uhd::transport;
using namespace uhd::niusrprio;
namespace asio = boost::asio;

/***********************************************************************
 * Discovery over the udp and pcie transport
 **********************************************************************/
static std::string get_fpga_option(wb_iface::sptr zpu_ctrl) {
    //1G = {0:1G, 1:1G} w/ DRAM, HG = {0:1G, 1:10G} w/ DRAM, XG = {0:10G, 1:10G} w/ DRAM
    //HGS = {0:1G, 1:10G} w/ SRAM, XGS = {0:10G, 1:10G} w/ SRAM

    //In the default configuration, UHD does not support the HG and XG images so
    //they are never autodetected.
    bool eth0XG = (zpu_ctrl->peek32(SR_ADDR(SET0_BASE, ZPU_RB_ETH_TYPE0)) == 0x1);
    bool eth1XG = (zpu_ctrl->peek32(SR_ADDR(SET0_BASE, ZPU_RB_ETH_TYPE1)) == 0x1);
    return (eth0XG && eth1XG) ? "XGS" : (eth1XG ? "HGS" : "1G");
}

//@TODO: Refactor the find functions to collapse common code for ethernet and PCIe
static device_addrs_t x300_find_with_addr(const device_addr_t &hint)
{
    udp_simple::sptr comm = udp_simple::make_broadcast(
        hint["addr"], BOOST_STRINGIZE(X300_FW_COMMS_UDP_PORT));

    //load request struct
    x300_fw_comms_t request = x300_fw_comms_t();
    request.flags = uhd::htonx<boost::uint32_t>(X300_FW_COMMS_FLAGS_ACK);
    request.sequence = uhd::htonx<boost::uint32_t>(std::rand());

    //send request
    comm->send(asio::buffer(&request, sizeof(request)));

    //loop for replies until timeout
    device_addrs_t addrs;
    while (true)
    {
        char buff[X300_FW_COMMS_MTU] = {};
        const size_t nbytes = comm->recv(asio::buffer(buff), 0.050);
        if (nbytes == 0) break;
        const x300_fw_comms_t *reply = (const x300_fw_comms_t *)buff;
        if (request.flags != reply->flags) break;
        if (request.sequence != reply->sequence) break;
        device_addr_t new_addr;
        new_addr["type"] = "x300";
        new_addr["addr"] = comm->get_recv_addr();

        //Attempt to read the name from the EEPROM and perform filtering.
        //This operation can throw due to compatibility mismatch.
        try
        {
            wb_iface::sptr zpu_ctrl = x300_make_ctrl_iface_enet(udp_simple::make_connected(new_addr["addr"], BOOST_STRINGIZE(X300_FW_COMMS_UDP_PORT)));
            if (x300_impl::is_claimed(zpu_ctrl)) continue; //claimed by another process
            new_addr["fpga"] = get_fpga_option(zpu_ctrl);

            i2c_core_100_wb32::sptr zpu_i2c = i2c_core_100_wb32::make(zpu_ctrl, I2C1_BASE);
            i2c_iface::sptr eeprom16 = zpu_i2c->eeprom16();
            const mboard_eeprom_t mb_eeprom(*eeprom16, "X300");
            new_addr["name"] = mb_eeprom["name"];
            new_addr["serial"] = mb_eeprom["serial"];
            switch (boost::lexical_cast<boost::uint16_t>(mb_eeprom["product"]))
            {
                case X300_PCIE_SSID:
                    new_addr["product"] = "X300";
                    break;
                case X310_PCIE_SSID:
                    new_addr["product"] = "X310";
                    break;
                default: UHD_MSG(error) << "X300 unknown product code: " << mb_eeprom["product"] << std::endl;
            }
        }
        catch(const std::exception &)
        {
            //set these values as empty string so the device may still be found
            //and the filter's below can still operate on the discovered device
            new_addr["name"] = "";
            new_addr["serial"] = "";
        }
        //filter the discovered device below by matching optional keys
        if (
            (not hint.has_key("name")    or hint["name"]    == new_addr["name"]) and
            (not hint.has_key("serial")  or hint["serial"]  == new_addr["serial"]) and
            (not hint.has_key("product") or hint["product"] == new_addr["product"])
        ){
            addrs.push_back(new_addr);
        }
    }

    return addrs;
}

static device_addrs_t x300_find_pcie(const device_addr_t &hint, bool explicit_query)
{
    std::string rpc_port_name(NIUSRPRIO_DEFAULT_RPC_PORT);
    if (hint.has_key("niusrpriorpc_port")) {
        rpc_port_name = hint["niusrpriorpc_port"];
    }

    device_addrs_t addrs;
    niusrprio_session::device_info_vtr dev_info_vtr;
    nirio_status status = niusrprio_session::enumerate(rpc_port_name, dev_info_vtr);
    if (explicit_query) nirio_status_to_exception(status, "x300_find_pcie: Error enumerating NI-RIO devices.");

    BOOST_FOREACH(niusrprio_session::device_info &dev_info, dev_info_vtr)
    {
        device_addr_t new_addr;
        new_addr["type"] = "x300";
        new_addr["resource"] = dev_info.resource_name;
        std::string resource_d(dev_info.resource_name);
        boost::to_upper(resource_d);

        niriok_proxy kernel_proxy;
        kernel_proxy.open(dev_info.interface_path);

        //Attempt to read the name from the EEPROM and perform filtering.
        //This operation can throw due to compatibility mismatch.
        try
        {
            //This call could throw an exception if the user is switching to using UHD
            //after LabVIEW FPGA. In that case, skip reading the name and serial and pick
            //a default FPGA flavor. During make, a new image will be loaded and everything
            //will be OK
            wb_iface::sptr zpu_ctrl = x300_make_ctrl_iface_pcie(kernel_proxy);
            if (x300_impl::is_claimed(zpu_ctrl)) continue; //claimed by another process

            //Attempt to autodetect the FPGA type
            if (not hint.has_key("fpga")) {
                new_addr["fpga"] = get_fpga_option(zpu_ctrl);
            }

            i2c_core_100_wb32::sptr zpu_i2c = i2c_core_100_wb32::make(zpu_ctrl, I2C1_BASE);
            i2c_iface::sptr eeprom16 = zpu_i2c->eeprom16();
            const mboard_eeprom_t mb_eeprom(*eeprom16, "X300");
            new_addr["name"] = mb_eeprom["name"];
            new_addr["serial"] = mb_eeprom["serial"];
            switch (boost::lexical_cast<boost::uint16_t>(mb_eeprom["product"]))
            {
                case X300_PCIE_SSID:
                    new_addr["product"] = "X300";
                    break;
                case X310_PCIE_SSID:
                    new_addr["product"] = "X310";
                    break;
                default: UHD_MSG(error) << "X300 unknown product code: " << mb_eeprom["product"] << std::endl;
            }
        }
        catch(const std::exception &)
        {
            //set these values as empty string so the device may still be found
            //and the filter's below can still operate on the discovered device
            if (not hint.has_key("fpga")) {
                new_addr["fpga"] = "HGS";
            }
            new_addr["name"] = "";
            new_addr["serial"] = "";
        }
        kernel_proxy.close();

        //filter the discovered device below by matching optional keys
        std::string resource_i = hint.has_key("resource") ? hint["resource"] : "";
        boost::to_upper(resource_i);

        if (
            (not hint.has_key("resource") or resource_i     == resource_d) and
            (not hint.has_key("name")     or hint["name"]   == new_addr["name"]) and
            (not hint.has_key("serial")   or hint["serial"] == new_addr["serial"]) and
            (not hint.has_key("product") or hint["product"] == new_addr["product"])
        ){
            addrs.push_back(new_addr);
        }
    }
    return addrs;
}

static device_addrs_t x300_find(const device_addr_t &hint_)
{
    //handle the multi-device discovery
    device_addrs_t hints = separate_device_addr(hint_);
    if (hints.size() > 1)
    {
        device_addrs_t found_devices;
        std::string error_msg;
        BOOST_FOREACH(const device_addr_t &hint_i, hints)
        {
            device_addrs_t found_devices_i = x300_find(hint_i);
            if (found_devices_i.size() != 1) error_msg += str(boost::format(
                "Could not resolve device hint \"%s\" to a single device."
            ) % hint_i.to_string());
            else found_devices.push_back(found_devices_i[0]);
        }
        if (found_devices.empty()) return device_addrs_t();
        if (not error_msg.empty()) throw uhd::value_error(error_msg);

        return device_addrs_t(1, combine_device_addrs(found_devices));
    }

    //initialize the hint for a single device case
    UHD_ASSERT_THROW(hints.size() <= 1);
    hints.resize(1); //in case it was empty
    device_addr_t hint = hints[0];
    device_addrs_t addrs;
    if (hint.has_key("type") and hint["type"] != "x300") return addrs;


    //use the address given
    if (hint.has_key("addr"))
    {
        device_addrs_t reply_addrs;
        try
        {
            reply_addrs = x300_find_with_addr(hint);
        }
        catch(const std::exception &ex)
        {
            UHD_MSG(error) << "X300 Network discovery error " << ex.what() << std::endl;
        }
        catch(...)
        {
            UHD_MSG(error) << "X300 Network discovery unknown error " << std::endl;
        }
        BOOST_FOREACH(const device_addr_t &reply_addr, reply_addrs)
        {
            device_addrs_t new_addrs = x300_find_with_addr(reply_addr);
            addrs.insert(addrs.begin(), new_addrs.begin(), new_addrs.end());
        }
        return addrs;
    }

    if (!hint.has_key("resource"))
    {
        //otherwise, no address was specified, send a broadcast on each interface
        BOOST_FOREACH(const if_addrs_t &if_addrs, get_if_addrs())
        {
            //avoid the loopback device
            if (if_addrs.inet == asio::ip::address_v4::loopback().to_string()) continue;

            //create a new hint with this broadcast address
            device_addr_t new_hint = hint;
            new_hint["addr"] = if_addrs.bcast;

            //call discover with the new hint and append results
            device_addrs_t new_addrs = x300_find(new_hint);
            addrs.insert(addrs.begin(), new_addrs.begin(), new_addrs.end());
        }
    }

    device_addrs_t pcie_addrs = x300_find_pcie(hint, hint.has_key("resource"));
    if (not pcie_addrs.empty()) addrs.insert(addrs.end(), pcie_addrs.begin(), pcie_addrs.end());

    return addrs;
}

/***********************************************************************
 * Make
 **********************************************************************/
static device::sptr x300_make(const device_addr_t &device_addr)
{
    return device::sptr(new x300_impl(device_addr));
}

UHD_STATIC_BLOCK(register_x300_device)
{
    device::register_device(&x300_find, &x300_make);
}

static void x300_load_fw(wb_iface::sptr fw_reg_ctrl, const std::string &file_name)
{
    UHD_MSG(status) << "Loading firmware " << file_name << std::flush;

    //load file into memory
    std::ifstream fw_file(file_name.c_str());
    boost::uint32_t fw_file_buff[X300_FW_NUM_BYTES/sizeof(boost::uint32_t)];
    fw_file.read((char *)fw_file_buff, sizeof(fw_file_buff));
    fw_file.close();

    //Poke the fw words into the WB boot loader
    fw_reg_ctrl->poke32(SR_ADDR(BOOT_LDR_BASE, BL_ADDRESS), 0);
    for (size_t i = 0; i < X300_FW_NUM_BYTES; i+=sizeof(boost::uint32_t))
    {
        //@TODO: FIXME: Since x300_ctrl_iface acks each write and traps exceptions, the first try for the last word
        //              written will print an error because it triggers a FW reload and fails to reply.
        fw_reg_ctrl->poke32(SR_ADDR(BOOT_LDR_BASE, BL_DATA), uhd::byteswap(fw_file_buff[i/sizeof(boost::uint32_t)]));
        if ((i & 0x1fff) == 0) UHD_MSG(status) << "." << std::flush;
    }

    UHD_MSG(status) << " done!" << std::endl;
}

x300_impl::x300_impl(const uhd::device_addr_t &dev_addr)
{
    UHD_MSG(status) << "X300 initialization sequence..." << std::endl;
    _async_md.reset(new async_md_type(1000/*messages deep*/));
    _tree = uhd::property_tree::make();
    _tree->create<std::string>("/name").set("X-Series Device");
    _sid_framer = 0;

    const device_addrs_t device_args = separate_device_addr(dev_addr);
    _mb.resize(device_args.size());
    for (size_t i = 0; i < device_args.size(); i++)
    {
        this->setup_mb(i, device_args[i]);
    }
}

void x300_impl::setup_mb(const size_t mb_i, const uhd::device_addr_t &dev_addr)
{
    const fs_path mb_path = "/mboards/"+boost::lexical_cast<std::string>(mb_i);
    mboard_members_t &mb = _mb[mb_i];

    mb.addr = dev_addr.has_key("resource") ? dev_addr["resource"] : dev_addr["addr"];
    mb.xport_path = dev_addr.has_key("resource") ? "nirio" : "eth";
    mb.if_pkt_is_big_endian = mb.xport_path != "nirio";

    if (mb.xport_path == "nirio")
    {
        nirio_status status = 0;

        std::string rpc_port_name(NIUSRPRIO_DEFAULT_RPC_PORT);
        if (dev_addr.has_key("niusrpriorpc_port")) {
            rpc_port_name = dev_addr["niusrpriorpc_port"];
        }
        UHD_MSG(status) << boost::format("Connecting to niusrpriorpc at localhost:%s...\n") % rpc_port_name;

        //Detect the PCIe product ID to distinguish between X300 and X310
        boost::uint32_t pid;
        niriok_proxy::sptr discovery_proxy =
            niusrprio_session::create_kernel_proxy(dev_addr["resource"], rpc_port_name);
        if (discovery_proxy) {
            nirio_status_chain(discovery_proxy->get_attribute(PRODUCT_NUMBER, pid), status);
            discovery_proxy->close();
        }
        nirio_status_to_exception(status, "x300_impl: Could not detect device type. Please ensure that that x300 device drivers are loaded and the device has a valid FPGA bitstream.");

        //Instantiate the correct lvbitx object
        nifpga_lvbitx::sptr lvbitx;
        if (pid == X310_PCIE_SSID) {
            lvbitx.reset(new x310_lvbitx(dev_addr["fpga"]));
        } else {
            lvbitx.reset(new x300_lvbitx(dev_addr["fpga"]));
        }

        //Load the lvbitx onto the device
        UHD_MSG(status) << boost::format("Using LVBITX bitfile %s...\n") % lvbitx->get_bitfile_path();
        mb.rio_fpga_interface.reset(new niusrprio_session(dev_addr["resource"], rpc_port_name));
        nirio_status_chain(mb.rio_fpga_interface->open(lvbitx, dev_addr.has_key("download-fpga")), status);
        nirio_status_to_exception(status, "x300_impl: Could not initialize RIO session.");
    }

    BOOST_FOREACH(const std::string &key, dev_addr.keys())
    {
        if (key.find("recv") != std::string::npos) mb.recv_args[key] = dev_addr[key];
        if (key.find("send") != std::string::npos) mb.send_args[key] = dev_addr[key];
    }

    const std::vector<std::string> DB_NAMES = boost::assign::list_of("A")("B");

    //create basic communication
    UHD_MSG(status) << "Setup basic communication..." << std::endl;
    if (mb.xport_path == "nirio")
        mb.zpu_ctrl = x300_make_ctrl_iface_pcie(mb.rio_fpga_interface->get_kernel_proxy());
    else {
        mb.zpu_ctrl = x300_make_ctrl_iface_enet(udp_simple::make_connected(mb.addr, BOOST_STRINGIZE(X300_FW_COMMS_UDP_PORT)));

        mtu_result_t user_set;
        user_set.recv_mtu = dev_addr.has_key("recv_frame_size") ? boost::lexical_cast<size_t>(dev_addr["recv_frame_size"]) : X300_ETH_DATA_FRAME_SIZE;
        user_set.send_mtu = dev_addr.has_key("send_frame_size") ? boost::lexical_cast<size_t>(dev_addr["send_frame_size"]) : X300_ETH_DATA_FRAME_SIZE;

        // Detect the MTU on the path to the USRP
        mtu_result_t result;
        try {
            result = determine_mtu(mb.addr, user_set);
        } catch(std::exception &e)
        {
            UHD_MSG(error) << e.what() << std::endl;
        }

        if(result.recv_mtu < user_set.recv_mtu)
            UHD_MSG(warning) << boost::format("The receive path contains entities that do not support MTUs >= one recv frame's size (%lu).") % user_set.recv_mtu << std::endl
                             << "Please verify your NIC's MTU setting using 'ip link' or set the recv_frame_size argument." << std::endl;

        if(result.send_mtu < user_set.send_mtu)
            UHD_MSG(warning) << boost::format("The send path contains entities that do not support MTUs >= one send frame's size (%lu).") % user_set.send_mtu << std::endl
                             << "Please verify your NIC's MTU setting using 'ip link' or set the send_frame_size argument." << std::endl;
    }


    mb.claimer_task = uhd::task::make(boost::bind(&x300_impl::claimer_loop, this, mb.zpu_ctrl));

    //extract the FW path for the X300
    //and live load fw over ethernet link
    if (dev_addr.has_key("fw"))
    {
        const std::string x300_fw_image = find_image_path(
            dev_addr.has_key("fw")? dev_addr["fw"] : X300_FW_FILE_NAME
        );
        x300_load_fw(mb.zpu_ctrl, x300_fw_image);
    }

    //check compat -- good place to do after conditional loading
    this->check_fw_compat(mb_path, mb.zpu_ctrl);
    this->check_fpga_compat(mb_path, mb.zpu_ctrl);

    //low speed perif access
    mb.zpu_spi = spi_core_3000::make(mb.zpu_ctrl, SR_ADDR(SET0_BASE, ZPU_SR_SPI), SR_ADDR(SET0_BASE, ZPU_RB_SPI));
    mb.zpu_i2c = i2c_core_100_wb32::make(mb.zpu_ctrl, I2C1_BASE);
    mb.zpu_i2c->set_clock_rate(X300_BUS_CLOCK_RATE);

    ////////////////////////////////////////////////////////////////////
    // print network routes mapping
    ////////////////////////////////////////////////////////////////////
    /*
    const uint32_t routes_addr = mb.zpu_ctrl->peek32(SR_ADDR(X300_FW_SHMEM_BASE, X300_FW_SHMEM_ROUTE_MAP_ADDR));
    const uint32_t routes_len = mb.zpu_ctrl->peek32(SR_ADDR(X300_FW_SHMEM_BASE, X300_FW_SHMEM_ROUTE_MAP_LEN));
    UHD_VAR(routes_len);
    for (size_t i = 0; i < routes_len; i+=1)
    {
        const uint32_t node_addr = mb.zpu_ctrl->peek32(SR_ADDR(routes_addr, i*2+0));
        const uint32_t nbor_addr = mb.zpu_ctrl->peek32(SR_ADDR(routes_addr, i*2+1));
        if (node_addr != 0 and nbor_addr != 0)
        {
            UHD_MSG(status) << boost::format("%u: %s -> %s")
                % i
                % asio::ip::address_v4(node_addr).to_string()
                % asio::ip::address_v4(nbor_addr).to_string()
            << std::endl;
        }
    }
    //*/

    ////////////////////////////////////////////////////////////////////
    // setup the mboard eeprom
    ////////////////////////////////////////////////////////////////////
    UHD_MSG(status) << "Loading values from EEPROM..." << std::endl;
    i2c_iface::sptr eeprom16 = mb.zpu_i2c->eeprom16();
    if (dev_addr.has_key("blank_eeprom"))
    {
        UHD_MSG(warning) << "Obliterating the motherboard EEPROM..." << std::endl;
        eeprom16->write_eeprom(0x50, 0, byte_vector_t(256, 0xff));
    }
    const mboard_eeprom_t mb_eeprom(*eeprom16, "X300");
    _tree->create<mboard_eeprom_t>(mb_path / "eeprom")
        .set(mb_eeprom)
        .subscribe(boost::bind(&x300_impl::set_mb_eeprom, this, mb.zpu_i2c, _1));

    ////////////////////////////////////////////////////////////////////
    // parse the product number
    ////////////////////////////////////////////////////////////////////
    std::string product_name = "X300?";
    if (not mb_eeprom["product"].empty())
    {
        switch (boost::lexical_cast<boost::uint16_t>(mb_eeprom["product"]))
        {
        case X300_PCIE_SSID:
            product_name = "X300";
            break;
        case X310_PCIE_SSID:
            product_name = "X310";
            break;
        default: UHD_MSG(error) << "X300 unknown product code: " << mb_eeprom["product"] << std::endl;
        }
    }
    _tree->create<std::string>(mb_path / "name").set(product_name);
    _tree->create<std::string>(mb_path / "codename").set("Yetti");

    ////////////////////////////////////////////////////////////////////
    // determine routing based on address match
    ////////////////////////////////////////////////////////////////////
    mb.router_dst_here = X300_XB_DST_E0; //some default if eeprom not match
    if (mb.xport_path == "nirio") {
        mb.router_dst_here = X300_XB_DST_PCI;
    } else {
        if (mb.addr == mb_eeprom["ip-addr0"]) mb.router_dst_here = X300_XB_DST_E0;
        if (mb.addr == mb_eeprom["ip-addr1"]) mb.router_dst_here = X300_XB_DST_E1;
        if (mb.addr == mb_eeprom["ip-addr2"]) mb.router_dst_here = X300_XB_DST_E0;
        if (mb.addr == mb_eeprom["ip-addr3"]) mb.router_dst_here = X300_XB_DST_E1;
    }
    ////////////////////////////////////////////////////////////////////
    // read dboard eeproms
    ////////////////////////////////////////////////////////////////////
    for (size_t i = 0; i < 8; i++)
    {
        if (i == 0 or i == 2) continue; //not used
        mb.db_eeproms[i].load(*mb.zpu_i2c, 0x50 | i);
    }

    ////////////////////////////////////////////////////////////////////
    // create clock control objects
    ////////////////////////////////////////////////////////////////////
    UHD_MSG(status) << "Setup RF frontend clocking..." << std::endl;

    //init shadow and clock source
    mb.clock_control_regs__clock_source = 0;
    mb.clock_control_regs__pps_select = 0;
    mb.clock_control_regs__pps_out_enb = 0;
    mb.clock_control_regs__tcxo_enb = 1;
    mb.clock_control_regs__gpsdo_pwr = 1;
    this->update_clock_source(mb, "internal");
    this->update_clock_control(mb);

    const std::string rev = mb_eeprom["revision"];
    mb.clock = x300_clock_ctrl::make(mb.zpu_spi, 1/*slaveno*/,
        dev_addr.cast<double>("master_clock_rate", X300_DEFAULT_TICK_RATE),
        (rev.empty()? 0 : boost::lexical_cast<int>(rev)),
        dev_addr.cast<double>("lmk_pll_ref", X300_DEFAULT_PLL2REF_FREQ));
    _tree->create<double>(mb_path / "tick_rate")
        .publish(boost::bind(&x300_clock_ctrl::get_master_clock_rate, mb.clock));

    UHD_MSG(status) << "Radio 1x clock set to " << (mb.clock->get_master_clock_rate()/1e6) << std::dec << " MHz. Crystal is " <<
      (mb.clock->get_crystal_clock_rate()/1e6) << std::dec << " MHz" << std::endl;

    ////////////////////////////////////////////////////////////////////
    // Create the GPSDO control
    ////////////////////////////////////////////////////////////////////
    static const boost::uint32_t dont_look_for_gpsdo = 0x1234abcdul;

    //otherwise if not disabled, look for the internal GPSDO
    if (mb.zpu_ctrl->peek32(SR_ADDR(X300_FW_SHMEM_BASE, X300_FW_SHMEM_GPSDO_STATUS)) != dont_look_for_gpsdo)
    {
        UHD_MSG(status) << "Detecting internal GPSDO.... " << std::flush;
        try
        {
            mb.gps = gps_ctrl::make(x300_make_uart_iface(mb.zpu_ctrl));
        }
        catch(std::exception &e)
        {
            UHD_MSG(error) << "An error occurred making GPSDO control: " << e.what() << std::endl;
        }
        if (mb.gps and mb.gps->gps_detected())
        {
            UHD_MSG(status) << "found" << std::endl;
            BOOST_FOREACH(const std::string &name, mb.gps->get_sensors())
            {
                _tree->create<sensor_value_t>(mb_path / "sensors" / name)
                    .publish(boost::bind(&gps_ctrl::get_sensor, mb.gps, name));
            }
        }
        else
        {
            UHD_MSG(status) << "not found" << std::endl;
            mb.zpu_ctrl->poke32(SR_ADDR(X300_FW_SHMEM_BASE, X300_FW_SHMEM_GPSDO_STATUS), dont_look_for_gpsdo);
        }
    }

    ////////////////////////////////////////////////////////////////////
    //clear router?
    ////////////////////////////////////////////////////////////////////
    for (size_t i = 0; i < 512; i++)
        mb.zpu_ctrl->poke32(SR_ADDR(SETXB_BASE, i), 0);
    ////////////////////////////////////////////////////////////////////
    // setup radios
    ////////////////////////////////////////////////////////////////////
    UHD_MSG(status) << "Initialize Radio control..." << std::endl;
    this->setup_radio(mb_i, 0, DB_NAMES[0]);
    this->setup_radio(mb_i, 1, DB_NAMES[1]);

    ////////////////////////////////////////////////////////////////////
    // front panel gpio
    ////////////////////////////////////////////////////////////////////
    mb.fp_gpio = gpio_core_200::make(mb.radio_perifs[0].ctrl, TOREG(SR_FP_GPIO), RB32_FP_GPIO);
    const std::vector<std::string> GPIO_ATTRS = boost::assign::list_of("CTRL")("DDR")("OUT")("ATR_0X")("ATR_RX")("ATR_TX")("ATR_XX");
    BOOST_FOREACH(const std::string &attr, GPIO_ATTRS)
    {
        _tree->create<boost::uint64_t>(mb_path / "gpio" / "FP0" / attr)
            .set(0)
            .subscribe(boost::bind(&x300_impl::set_fp_gpio, this, mb_path, mb.fp_gpio, attr, _1));
    }
    _tree->create<boost::uint64_t>(mb_path / "gpio" / "FP0" / "READBACK")
        .publish(boost::bind(&x300_impl::get_fp_gpio, this, mb.fp_gpio, "READBACK"));

    ////////////////////////////////////////////////////////////////////
    // register the time keepers - only one can be the highlander
    ////////////////////////////////////////////////////////////////////
    _tree->create<time_spec_t>(mb_path / "time" / "now")
        .publish(boost::bind(&time_core_3000::get_time_now, mb.radio_perifs[0].time64))
        .subscribe(boost::bind(&time_core_3000::set_time_now, mb.radio_perifs[0].time64, _1))
        .subscribe(boost::bind(&time_core_3000::set_time_now, mb.radio_perifs[1].time64, _1));
    _tree->create<time_spec_t>(mb_path / "time" / "pps")
        .publish(boost::bind(&time_core_3000::get_time_last_pps, mb.radio_perifs[0].time64))
        .subscribe(boost::bind(&time_core_3000::set_time_next_pps, mb.radio_perifs[0].time64, _1))
        .subscribe(boost::bind(&time_core_3000::set_time_next_pps, mb.radio_perifs[1].time64, _1));
    //setup time source props
    _tree->create<std::string>(mb_path / "time_source" / "value")
        .subscribe(boost::bind(&x300_impl::update_time_source, this, boost::ref(mb), _1));
    _tree->create<bool>(mb_path / "time_source" / "output")
        .subscribe(boost::bind(&x300_impl::set_time_source_out, this, boost::ref(mb), _1))
        .set(true);
    static const std::vector<std::string> time_sources = boost::assign::list_of("internal")("external")("gpsdo");
    _tree->create<std::vector<std::string> >(mb_path / "time_source" / "options").set(time_sources);
    //setup reference source props
    _tree->create<std::string>(mb_path / "clock_source" / "value")
        .subscribe(boost::bind(&x300_impl::update_clock_source, this, boost::ref(mb), _1));
    _tree->create<bool>(mb_path / "clock_source" / "output")
        .subscribe(boost::bind(&x300_clock_ctrl::set_ref_out, mb.clock, _1))
        .set(true);
    static const std::vector<std::string> clock_sources = boost::assign::list_of("internal")("external")("gpsdo");
    _tree->create<std::vector<std::string> >(mb_path / "clock_source" / "options").set(clock_sources);

    ////////////////////////////////////////////////////////////////////
    // create frontend mapping
    ////////////////////////////////////////////////////////////////////
    _tree->create<subdev_spec_t>(mb_path / "rx_subdev_spec")
        .subscribe(boost::bind(&x300_impl::update_rx_subdev_spec, this, mb_i, _1));
    _tree->create<subdev_spec_t>(mb_path / "tx_subdev_spec")
        .subscribe(boost::bind(&x300_impl::update_tx_subdev_spec, this, mb_i, _1));

    ////////////////////////////////////////////////////////////////////
    // and do the misc mboard sensors
    ////////////////////////////////////////////////////////////////////
    _tree->create<sensor_value_t>(mb_path / "sensors" / "ref_locked")
        .publish(boost::bind(&x300_impl::get_ref_locked, this, mb.zpu_ctrl));

    ////////////////////////////////////////////////////////////////////
    // do some post-init tasks
    ////////////////////////////////////////////////////////////////////
    _tree->access<double>(mb_path / "tick_rate") //now subscribe the clock rate setter
        .subscribe(boost::bind(&x300_impl::set_tick_rate, this, boost::ref(mb), _1))
        .subscribe(boost::bind(&x300_impl::update_tick_rate, this, boost::ref(mb), _1))
        .set(mb.clock->get_master_clock_rate());

    subdev_spec_t rx_fe_spec, tx_fe_spec;
    rx_fe_spec.push_back(subdev_spec_pair_t("A", _tree->list(mb_path / "dboards" / "A" / "rx_frontends").at(0)));
    rx_fe_spec.push_back(subdev_spec_pair_t("B", _tree->list(mb_path / "dboards" / "B" / "rx_frontends").at(0)));
    tx_fe_spec.push_back(subdev_spec_pair_t("A", _tree->list(mb_path / "dboards" / "A" / "tx_frontends").at(0)));
    tx_fe_spec.push_back(subdev_spec_pair_t("B", _tree->list(mb_path / "dboards" / "B" / "tx_frontends").at(0)));

    _tree->access<subdev_spec_t>(mb_path / "rx_subdev_spec").set(rx_fe_spec);
    _tree->access<subdev_spec_t>(mb_path / "tx_subdev_spec").set(tx_fe_spec);

    //GPS installed: use external ref, time, and init time spec
    if (mb.gps and mb.gps->gps_detected())
    {
        UHD_MSG(status) << "Setting references to the internal GPSDO" << std::endl;
        _tree->access<std::string>(mb_path / "time_source" / "value").set("gpsdo");
        _tree->access<std::string>(mb_path / "clock_source" / "value").set("gpsdo");
        UHD_MSG(status) << "Initializing time to the internal GPSDO" << std::endl;
        const time_t tp = time_t(mb.gps->get_sensor("gps_time").to_int()+1);
        _tree->access<time_spec_t>(mb_path / "time" / "pps").set(time_spec_t(tp));
    }
    else
    {
        _tree->access<std::string>(mb_path / "time_source" / "value").set("external");
        _tree->access<std::string>(mb_path / "clock_source" / "value").set("external");
        boost::this_thread::sleep(boost::posix_time::milliseconds(100));
        if (this->get_ref_locked(mb.zpu_ctrl).to_bool())
        {
            UHD_MSG(status) << "Setting references to external sources" << std::endl;
        }
        else
        {
            UHD_MSG(status) << "Setting references to internal sources" << std::endl;
            _tree->access<std::string>(mb_path / "time_source" / "value").set("internal");
            _tree->access<std::string>(mb_path / "clock_source" / "value").set("internal");
        }
    }
}

x300_impl::~x300_impl(void)
{
    try
    {
        BOOST_FOREACH(mboard_members_t &mb, _mb)
        {
            mb.radio_perifs[0].ctrl->poke32(TOREG(SR_MISC_OUTS), (1 << 2)); //disable/reset ADC/DAC
            mb.radio_perifs[1].ctrl->poke32(TOREG(SR_MISC_OUTS), (1 << 2)); //disable/reset ADC/DAC

            //kill the claimer task and unclaim the device
            mb.claimer_task.reset();
            mb.zpu_ctrl->poke32(SR_ADDR(X300_FW_SHMEM_BASE, X300_FW_SHMEM_CLAIM_TIME), 0);
            mb.zpu_ctrl->poke32(SR_ADDR(X300_FW_SHMEM_BASE, X300_FW_SHMEM_CLAIM_SRC), 0);
        }
    }
    catch(...)
    {
        UHD_SAFE_CALL(throw;)
    }
}

static void check_adc(wb_iface::sptr iface, const boost::uint32_t val)
{
    boost::uint32_t adc_rb = iface->peek32(RB32_RX);
    adc_rb ^= 0xfffc0000; //adapt for I inversion in FPGA
    //UHD_MSG(status) << "adc_rb " << std::hex << adc_rb << "  val " << std::hex << val << std::endl;
    UHD_ASSERT_THROW(adc_rb == val);
}

void x300_impl::setup_radio(const size_t mb_i, const size_t i, const std::string &db_name)
{
    const fs_path mb_path = "/mboards/"+boost::lexical_cast<std::string>(mb_i);
    mboard_members_t &mb = _mb[mb_i];
    radio_perifs_t &perif = mb.radio_perifs[i];
    const size_t dspno = i;

    ////////////////////////////////////////////////////////////////////
    // radio control
    ////////////////////////////////////////////////////////////////////
    uint8_t dest = (i == 0)? X300_XB_DST_R0 : X300_XB_DST_R1;
    boost::uint32_t ctrl_sid;
    both_xports_t xport = this->make_transport(mb_i, dest, X300_RADIO_DEST_PREFIX_CTRL, device_addr_t(), ctrl_sid);
    perif.ctrl = radio_ctrl_core_3000::make(mb.if_pkt_is_big_endian, xport.recv, xport.send, ctrl_sid, db_name);
    perif.ctrl->poke32(TOREG(SR_MISC_OUTS), (1 << 2)); //reset adc + dac
    perif.ctrl->poke32(TOREG(SR_MISC_OUTS),  (1 << 1) | (1 << 0)); //out of reset + dac enable

    this->register_loopback_self_test(perif.ctrl);

    perif.spi = spi_core_3000::make(perif.ctrl, TOREG(SR_SPI), RB32_SPI);
    perif.adc = x300_adc_ctrl::make(perif.spi, DB_ADC_SEN);
    perif.dac = x300_dac_ctrl::make(perif.spi, DB_DAC_SEN, mb.clock->get_master_clock_rate());
    perif.leds = gpio_core_200_32wo::make(perif.ctrl, TOREG(SR_LEDS));

    ////////////////////////////////////////////////////////////////
    // ADC self test
    ////////////////////////////////////////////////////////////////
    perif.adc->set_test_word("ones", "ones"); check_adc(perif.ctrl, 0xfffcfffc);
    perif.adc->set_test_word("zeros", "zeros"); check_adc(perif.ctrl, 0x00000000);
    perif.adc->set_test_word("ones", "zeros"); check_adc(perif.ctrl, 0xfffc0000);
    perif.adc->set_test_word("zeros", "ones"); check_adc(perif.ctrl, 0x0000fffc);
    for (size_t k = 0; k < 14; k++)
    {
        perif.adc->set_test_word("zeros", "custom", 1 << k);
        check_adc(perif.ctrl, 1 << (k+2));
    }
    for (size_t k = 0; k < 14; k++)
    {
        perif.adc->set_test_word("custom", "zeros", 1 << k);
        check_adc(perif.ctrl, 1 << (k+18));
    }
    perif.adc->set_test_word("normal", "normal");

    ////////////////////////////////////////////////////////////////
    // Sync DAC's for MIMO
    ////////////////////////////////////////////////////////////////
    UHD_MSG(status) << "Sync DAC's." << std::endl;
    perif.dac->arm_dac_sync();               // Put DAC into data Sync mode
    perif.ctrl->poke32(TOREG(SR_DACSYNC), 0x1);  // Arm FRAMEP/N sync pulse


    ////////////////////////////////////////////////////////////////
    // create codec control objects
    ////////////////////////////////////////////////////////////////
    _tree->create<int>(mb_path / "rx_codecs" / db_name / "gains"); //phony property so this dir exists
    _tree->create<int>(mb_path / "tx_codecs" / db_name / "gains"); //phony property so this dir exists
    _tree->create<std::string>(mb_path / "rx_codecs" / db_name / "name").set("ads62p48");
    _tree->create<std::string>(mb_path / "tx_codecs" / db_name / "name").set("ad9146");

    _tree->create<meta_range_t>(mb_path / "rx_codecs" / db_name / "gains" / "digital" / "range").set(meta_range_t(0, 6.0, 0.5));
    _tree->create<double>(mb_path / "rx_codecs" / db_name / "gains" / "digital" / "value")
        .subscribe(boost::bind(&x300_adc_ctrl::set_gain, perif.adc, _1)).set(0);

    ////////////////////////////////////////////////////////////////////
    // front end corrections
    ////////////////////////////////////////////////////////////////////
    perif.rx_fe = rx_frontend_core_200::make(perif.ctrl, TOREG(SR_RX_FRONT));
    const fs_path rx_fe_path = mb_path / "rx_frontends" / db_name;
    _tree->create<std::complex<double> >(rx_fe_path / "dc_offset" / "value")
        .coerce(boost::bind(&rx_frontend_core_200::set_dc_offset, perif.rx_fe, _1))
        .set(std::complex<double>(0.0, 0.0));
    _tree->create<bool>(rx_fe_path / "dc_offset" / "enable")
        .subscribe(boost::bind(&rx_frontend_core_200::set_dc_offset_auto, perif.rx_fe, _1))
        .set(true);
    _tree->create<std::complex<double> >(rx_fe_path / "iq_balance" / "value")
        .subscribe(boost::bind(&rx_frontend_core_200::set_iq_balance, perif.rx_fe, _1))
        .set(std::complex<double>(0.0, 0.0));

    perif.tx_fe = tx_frontend_core_200::make(perif.ctrl, TOREG(SR_TX_FRONT));
    const fs_path tx_fe_path = mb_path / "tx_frontends" / db_name;
    _tree->create<std::complex<double> >(tx_fe_path / "dc_offset" / "value")
        .coerce(boost::bind(&tx_frontend_core_200::set_dc_offset, perif.tx_fe, _1))
        .set(std::complex<double>(0.0, 0.0));
    _tree->create<std::complex<double> >(tx_fe_path / "iq_balance" / "value")
        .subscribe(boost::bind(&tx_frontend_core_200::set_iq_balance, perif.tx_fe, _1))
        .set(std::complex<double>(0.0, 0.0));



    ////////////////////////////////////////////////////////////////////
    // create rx dsp control objects
    ////////////////////////////////////////////////////////////////////
    perif.framer = rx_vita_core_3000::make(perif.ctrl, TOREG(SR_RX_CTRL));
    perif.ddc = rx_dsp_core_3000::make(perif.ctrl, TOREG(SR_RX_DSP));
    perif.ddc->set_link_rate(10e9/8); //whatever
    _tree->access<double>(mb_path / "tick_rate")
        .subscribe(boost::bind(&rx_vita_core_3000::set_tick_rate, perif.framer, _1))
        .subscribe(boost::bind(&rx_dsp_core_3000::set_tick_rate, perif.ddc, _1));
    const fs_path rx_dsp_path = mb_path / "rx_dsps" / str(boost::format("%u") % dspno);
    _tree->create<meta_range_t>(rx_dsp_path / "rate" / "range")
        .publish(boost::bind(&rx_dsp_core_3000::get_host_rates, perif.ddc));
    _tree->create<double>(rx_dsp_path / "rate" / "value")
        .coerce(boost::bind(&rx_dsp_core_3000::set_host_rate, perif.ddc, _1))
        .subscribe(boost::bind(&x300_impl::update_rx_samp_rate, this, boost::ref(mb), dspno, _1))
        .set(1e6);
    _tree->create<double>(rx_dsp_path / "freq" / "value")
        .coerce(boost::bind(&rx_dsp_core_3000::set_freq, perif.ddc, _1))
        .set(0.0);
    _tree->create<meta_range_t>(rx_dsp_path / "freq" / "range")
        .publish(boost::bind(&rx_dsp_core_3000::get_freq_range, perif.ddc));
    _tree->create<stream_cmd_t>(rx_dsp_path / "stream_cmd")
        .subscribe(boost::bind(&rx_vita_core_3000::issue_stream_command, perif.framer, _1));

    ////////////////////////////////////////////////////////////////////
    // create tx dsp control objects
    ////////////////////////////////////////////////////////////////////
    perif.deframer = tx_vita_core_3000::make(perif.ctrl, TOREG(SR_TX_CTRL));
    perif.duc = tx_dsp_core_3000::make(perif.ctrl, TOREG(SR_TX_DSP));
    perif.duc->set_link_rate(10e9/8); //whatever
    _tree->access<double>(mb_path / "tick_rate")
        .subscribe(boost::bind(&tx_vita_core_3000::set_tick_rate, perif.deframer, _1))
        .subscribe(boost::bind(&tx_dsp_core_3000::set_tick_rate, perif.duc, _1));
    const fs_path tx_dsp_path = mb_path / "tx_dsps" / str(boost::format("%u") % dspno);
    _tree->create<meta_range_t>(tx_dsp_path / "rate" / "range")
        .publish(boost::bind(&tx_dsp_core_3000::get_host_rates, perif.duc));
    _tree->create<double>(tx_dsp_path / "rate" / "value")
        .coerce(boost::bind(&tx_dsp_core_3000::set_host_rate, perif.duc, _1))
        .subscribe(boost::bind(&x300_impl::update_tx_samp_rate, this, boost::ref(mb), dspno, _1))
        .set(1e6);
    _tree->create<double>(tx_dsp_path / "freq" / "value")
        .coerce(boost::bind(&tx_dsp_core_3000::set_freq, perif.duc, _1))
        .set(0.0);
    _tree->create<meta_range_t>(tx_dsp_path / "freq" / "range")
        .publish(boost::bind(&tx_dsp_core_3000::get_freq_range, perif.duc));

    ////////////////////////////////////////////////////////////////////
    // create time control objects
    ////////////////////////////////////////////////////////////////////
    time_core_3000::readback_bases_type time64_rb_bases;
    time64_rb_bases.rb_now = RB64_TIME_NOW;
    time64_rb_bases.rb_pps = RB64_TIME_PPS;
    perif.time64 = time_core_3000::make(perif.ctrl, TOREG(SR_TIME), time64_rb_bases);

    ////////////////////////////////////////////////////////////////////
    // create RF frontend interfacing
    ////////////////////////////////////////////////////////////////////
    const size_t j = (db_name == "B")? 0x2 : 0x0;
    _tree->create<dboard_eeprom_t>(mb_path / "dboards" / db_name / "rx_eeprom")
        .set(mb.db_eeproms[X300_DB0_RX_EEPROM | j])
        .subscribe(boost::bind(&x300_impl::set_db_eeprom, this, mb.zpu_i2c, (0x50 | X300_DB0_RX_EEPROM | j), _1));
    _tree->create<dboard_eeprom_t>(mb_path / "dboards" / db_name / "tx_eeprom")
        .set(mb.db_eeproms[X300_DB0_TX_EEPROM | j])
        .subscribe(boost::bind(&x300_impl::set_db_eeprom, this, mb.zpu_i2c, (0x50 | X300_DB0_TX_EEPROM | j), _1));
    _tree->create<dboard_eeprom_t>(mb_path / "dboards" / db_name / "gdb_eeprom")
        .set(mb.db_eeproms[X300_DB0_GDB_EEPROM | j])
        .subscribe(boost::bind(&x300_impl::set_db_eeprom, this, mb.zpu_i2c, (0x50 | X300_DB0_GDB_EEPROM | j), _1));

    //create a new dboard interface
    x300_dboard_iface_config_t db_config;
    db_config.gpio = gpio_core_200::make(perif.ctrl, TOREG(SR_GPIO), RB32_GPIO);
    db_config.spi = perif.spi;
    db_config.rx_spi_slaveno = DB_RX_SEN;
    db_config.tx_spi_slaveno = DB_TX_SEN;
    db_config.i2c = mb.zpu_i2c;
    db_config.clock = mb.clock;
    db_config.which_rx_clk = (db_name == "A")? X300_CLOCK_WHICH_DB0_RX : X300_CLOCK_WHICH_DB1_RX;
    db_config.which_tx_clk = (db_name == "A")? X300_CLOCK_WHICH_DB0_TX : X300_CLOCK_WHICH_DB1_TX;
    _dboard_ifaces[db_name] = x300_make_dboard_iface(db_config);

    //create a new dboard manager
    _tree->create<dboard_iface::sptr>(mb_path / "dboards" / db_name / "iface").set(_dboard_ifaces[db_name]);
    _dboard_managers[db_name] = dboard_manager::make(
        mb.db_eeproms[X300_DB0_RX_EEPROM | j].id,
        mb.db_eeproms[X300_DB0_TX_EEPROM | j].id,
        mb.db_eeproms[X300_DB0_GDB_EEPROM | j].id,
        _dboard_ifaces[db_name],
        _tree->subtree(mb_path / "dboards" / db_name)
    );

    //now that dboard is created -- register into rx antenna event
    const std::string fe_name = _tree->list(mb_path / "dboards" / db_name / "rx_frontends").front();
    _tree->access<std::string>(mb_path / "dboards" / db_name / "rx_frontends" / fe_name / "antenna" / "value")
        .subscribe(boost::bind(&x300_impl::update_atr_leds, this, mb.radio_perifs[i].leds, _1));
    this->update_atr_leds(mb.radio_perifs[i].leds, ""); //init anyway, even if never called

    //bind frontend corrections to the dboard freq props
    const fs_path db_rx_fe_path = mb_path / "dboards" / db_name / "rx_frontends";
    BOOST_FOREACH(const std::string &name, _tree->list(db_rx_fe_path))
    {
        _tree->access<double>(db_rx_fe_path / name / "freq" / "value")
            .subscribe(boost::bind(&x300_impl::set_rx_fe_corrections, this, mb_path, db_name, _1));
    }
}

void x300_impl::set_rx_fe_corrections(const uhd::fs_path &mb_path, const std::string &fe_name, const double lo_freq)
{
    apply_rx_fe_corrections(this->get_tree()->subtree(mb_path), fe_name, lo_freq);
}

boost::uint32_t get_pcie_dma_channel(boost::uint8_t destination, boost::uint8_t prefix)
{
    static const boost::uint32_t RADIO_GRP_SIZE = 3;
    static const boost::uint32_t RADIO0_GRP     = 0;
    static const boost::uint32_t RADIO1_GRP     = 1;

    boost::uint32_t radio_grp = (destination == X300_XB_DST_R0) ? RADIO0_GRP : RADIO1_GRP;
    return ((radio_grp * RADIO_GRP_SIZE) + prefix);
}


x300_impl::both_xports_t x300_impl::make_transport(
    const size_t mb_index,
    const uint8_t& destination,
    const uint8_t& prefix,
    const uhd::device_addr_t& args,
    boost::uint32_t& sid
)
{
    mboard_members_t &mb = _mb[mb_index];
    const std::string& addr = mb.addr;
    const std::string& xport_path = mb.xport_path;
    both_xports_t xports;

    sid_config_t config;
    config.router_addr_there    = X300_DEVICE_THERE;
    config.dst_prefix           = prefix;
    config.router_dst_there     = destination;
    config.router_dst_here      = mb.router_dst_here;
    sid = this->allocate_sid(mb, config);

    static const uhd::device_addr_t DEFAULT_XPORT_ARGS;

    const uhd::device_addr_t& xport_args =
        (prefix != X300_RADIO_DEST_PREFIX_CTRL) ? args : DEFAULT_XPORT_ARGS;

    zero_copy_xport_params default_buff_args;
    if (xport_path == "nirio") {
        default_buff_args.send_frame_size =
            (prefix == X300_RADIO_DEST_PREFIX_TX) ? X300_PCIE_DATA_FRAME_SIZE : X300_PCIE_MSG_FRAME_SIZE;
        default_buff_args.recv_frame_size =
            (prefix == X300_RADIO_DEST_PREFIX_RX) ? X300_PCIE_DATA_FRAME_SIZE : X300_PCIE_MSG_FRAME_SIZE;
        default_buff_args.num_send_frames =
            (prefix == X300_RADIO_DEST_PREFIX_TX) ? X300_PCIE_DATA_NUM_FRAMES : X300_PCIE_MSG_NUM_FRAMES;
        default_buff_args.num_recv_frames =
            (prefix == X300_RADIO_DEST_PREFIX_RX) ? X300_PCIE_DATA_NUM_FRAMES : X300_PCIE_MSG_NUM_FRAMES;

        xports.recv = nirio_zero_copy::make(
            mb.rio_fpga_interface, get_pcie_dma_channel(destination, prefix), default_buff_args, xport_args);
        xports.send = xports.recv;

        //For the nirio transport, buffer size is depends on the frame size and num frames
        xports.recv_buff_size = xports.recv->get_num_recv_frames() * xports.recv->get_recv_frame_size();
        xports.send_buff_size = xports.send->get_num_send_frames() * xports.send->get_send_frame_size();
    } else {

        default_buff_args.send_frame_size =
            (prefix == X300_RADIO_DEST_PREFIX_TX) ? X300_ETH_DATA_FRAME_SIZE : X300_ETH_MSG_FRAME_SIZE;
        default_buff_args.recv_frame_size =
            (prefix == X300_RADIO_DEST_PREFIX_RX) ? X300_ETH_DATA_FRAME_SIZE : X300_ETH_MSG_FRAME_SIZE;
        default_buff_args.num_send_frames =
            (prefix == X300_RADIO_DEST_PREFIX_TX) ? X300_ETH_DATA_NUM_FRAMES : X300_ETH_MSG_NUM_FRAMES;
        default_buff_args.num_recv_frames =
            (prefix == X300_RADIO_DEST_PREFIX_RX) ? X300_ETH_DATA_NUM_FRAMES : X300_ETH_MSG_NUM_FRAMES;

        //make a new transport - fpga has no idea how to talk to use on this yet
        udp_zero_copy::buff_params buff_params;
        xports.recv = udp_zero_copy::make(addr, BOOST_STRINGIZE(X300_VITA_UDP_PORT), default_buff_args, buff_params, xport_args);
        xports.send = xports.recv;

        //For the UDP transport the buffer size if the size of the socket buffer in the kernel
        xports.recv_buff_size = buff_params.recv_buff_size;
        xports.send_buff_size = buff_params.send_buff_size;
    }

    //always program the framer if this is a socket, even its caching was used
    if (xport_path != "nirio")
    {
        //clear the ethernet dispatcher's udp port
        //NOT clearing this, the dispatcher is now intelligent
        //_zpu_ctrl->poke32(SR_ADDR(SET0_BASE, (ZPU_SR_ETHINT0+8+3)), 0);

        //send a mini packet with SID into the ZPU
        //ZPU will reprogram the ethernet framer
        UHD_LOG << "programming packet for new xport on "
            << addr << std::hex << "sid 0x" << sid << std::dec << std::endl;
        //YES, get a __send__ buffer from the __recv__ socket
        //-- this is the only way to program the framer for recv:
        managed_send_buffer::sptr buff = xports.recv->get_send_buff();
        buff->cast<boost::uint32_t *>()[0] = 0; //eth dispatch looks for != 0
        buff->cast<boost::uint32_t *>()[1] = uhd::htonx(sid);
        buff->commit(8);
        buff.reset();

        //reprogram the ethernet dispatcher's udp port (should be safe to always set)
        UHD_LOG << "reprogram the ethernet dispatcher's udp port" << std::endl;
        mb.zpu_ctrl->poke32(SR_ADDR(SET0_BASE, (ZPU_SR_ETHINT0+8+3)), X300_VITA_UDP_PORT);
        mb.zpu_ctrl->poke32(SR_ADDR(SET0_BASE, (ZPU_SR_ETHINT1+8+3)), X300_VITA_UDP_PORT);

        //Do a peek to an arbitrary address to guarantee that the
        //ethernet framer has been programmed before we return.
        mb.zpu_ctrl->peek32(0);
    }
    return xports;
}


boost::uint32_t x300_impl::allocate_sid(mboard_members_t &mb, const sid_config_t &config)
{
    const std::string &xport_path = mb.xport_path;
    const boost::uint32_t stream = (config.dst_prefix | (config.router_dst_there << 2)) & 0xff;

    const boost::uint32_t sid = 0
        | (X300_DEVICE_HERE << 24)
        | (_sid_framer << 16)
        | (config.router_addr_there << 8)
        | (stream << 0)
    ;
    UHD_LOG << std::hex
        << " sid 0x" << sid
        << " framer 0x" << _sid_framer
        << " stream 0x" << stream
        << " router_dst_there 0x" << int(config.router_dst_there)
        << " router_addr_there 0x" << int(config.router_addr_there)
        << std::dec << std::endl;

    // Program the X300 to recognise it's own local address.
    mb.zpu_ctrl->poke32(SR_ADDR(SET0_BASE, ZPU_SR_XB_LOCAL), config.router_addr_there);
    // Program CAM entry for outgoing packets matching a X300 resource (for example a Radio)
    // This type of packet does matches the XB_LOCAL address and is looked up in the upper half of the CAM
    mb.zpu_ctrl->poke32(SR_ADDR(SETXB_BASE, 256 + (stream)), config.router_dst_there);
    // Program CAM entry for returning packets to us (for example GR host via Eth0)
    // This type of packet does not match the XB_LOCAL address and is looked up in the lower half of the CAM
    mb.zpu_ctrl->poke32(SR_ADDR(SETXB_BASE, 0 + (X300_DEVICE_HERE)), config.router_dst_here);

    if (xport_path == "nirio") {
        uint32_t router_config_word = ((_sid_framer & 0xff) << 16) |                                    //Return SID
                                      get_pcie_dma_channel(config.router_dst_there, config.dst_prefix); //Dest
        mb.rio_fpga_interface->get_kernel_proxy().poke(PCIE_ROUTER_REG(0), router_config_word);
    }

    UHD_LOG << std::hex
        << "done router config for sid 0x" << sid
        << std::dec << std::endl;

    //increment for next setup
    _sid_framer++;

    return sid;
}

void x300_impl::update_atr_leds(gpio_core_200_32wo::sptr leds, const std::string &rx_ant)
{
    const bool is_txrx = (rx_ant == "TX/RX");
    const int rx_led = (1 << 2);
    const int txrx_led = (1 << 1);
    const int tx_led = (1 << 0);
    leds->set_atr_reg(dboard_iface::ATR_REG_IDLE, 0);
    leds->set_atr_reg(dboard_iface::ATR_REG_RX_ONLY, is_txrx? txrx_led : rx_led);
    leds->set_atr_reg(dboard_iface::ATR_REG_TX_ONLY, tx_led);
    leds->set_atr_reg(dboard_iface::ATR_REG_FULL_DUPLEX, rx_led | tx_led);
}

void x300_impl::set_tick_rate(mboard_members_t &mb, const double rate)
{
    BOOST_FOREACH(radio_perifs_t &perif, mb.radio_perifs)
    {
        perif.time64->set_tick_rate(rate);
        perif.time64->self_test();
    }
}

void x300_impl::register_loopback_self_test(wb_iface::sptr iface)
{
    bool test_fail = false;
    UHD_MSG(status) << "Performing register loopback test... " << std::flush;
    size_t hash = time(NULL);
    for (size_t i = 0; i < 100; i++)
    {
        boost::hash_combine(hash, i);
        iface->poke32(TOREG(SR_TEST), boost::uint32_t(hash));
        test_fail = iface->peek32(RB32_TEST) != boost::uint32_t(hash);
        if (test_fail) break; //exit loop on any failure
    }
    UHD_MSG(status) << ((test_fail)? " fail" : "pass") << std::endl;
}

void x300_impl::set_time_source_out(mboard_members_t &mb, const bool enb)
{
    mb.clock_control_regs__pps_out_enb = enb? 1 : 0;
    this->update_clock_control(mb);
}

void x300_impl::update_clock_control(mboard_members_t &mb)
{
    const size_t reg = mb.clock_control_regs__clock_source
        | (mb.clock_control_regs__pps_select << 2)
        | (mb.clock_control_regs__pps_out_enb << 3)
        | (mb.clock_control_regs__tcxo_enb << 4)
        | (mb.clock_control_regs__gpsdo_pwr << 5)
    ;
    mb.zpu_ctrl->poke32(SR_ADDR(SET0_BASE, ZPU_SR_CLOCK_CTRL), reg);
}

void x300_impl::update_clock_source(mboard_members_t &mb, const std::string &source)
{
    mb.clock_control_regs__clock_source = 0;
    if (source == "internal") mb.clock_control_regs__clock_source = 0x2;
    else if (source == "external") mb.clock_control_regs__clock_source = 0x0;
    else if (source == "gpsdo") mb.clock_control_regs__clock_source = 0x3;
    else throw uhd::key_error("update_clock_source: unknown source: " + source);
    mb.clock_control_regs__tcxo_enb = (source == "internal")? 1 : 0;
    this->update_clock_control(mb);
}

void x300_impl::update_time_source(mboard_members_t &mb, const std::string &source)
{
    if (source == "internal"){}
    else if (source == "external"){}
    else if (source == "gpsdo"){}
    else throw uhd::key_error("update_time_source: unknown source: " + source);
    mb.clock_control_regs__pps_select = (source == "external")? 1 : 0;
    this->update_clock_control(mb);
}

sensor_value_t x300_impl::get_ref_locked(wb_iface::sptr ctrl)
{
    const bool lock = (ctrl->peek32(SR_ADDR(SET0_BASE, ZPU_RB_CLK_STATUS)) & (1 << 2)) != 0;
    return sensor_value_t("Ref", lock, "locked", "unlocked");
}

void x300_impl::set_db_eeprom(i2c_iface::sptr i2c, const size_t addr, const uhd::usrp::dboard_eeprom_t &db_eeprom)
{
    db_eeprom.store(*i2c, addr);
}

void x300_impl::set_mb_eeprom(i2c_iface::sptr i2c, const mboard_eeprom_t &mb_eeprom)
{
    i2c_iface::sptr eeprom16 = i2c->eeprom16();
    mb_eeprom.commit(*eeprom16, "X300");
}

boost::uint64_t x300_impl::get_fp_gpio(gpio_core_200::sptr gpio, const std::string &)
{
    return boost::uint64_t(gpio->read_gpio(dboard_iface::UNIT_RX));
}

template <typename T>
static T shadow_it(const T &shadow, const T &value, const T &mask)
{
    return (shadow & ~mask) | (value & mask);
}

void x300_impl::set_fp_gpio(const uhd::fs_path &mb_path, gpio_core_200::sptr gpio, const std::string &attr, const boost::uint64_t setting)
{
    const boost::uint32_t value = boost::uint32_t(setting >> 0);
    const boost::uint32_t mask = boost::uint32_t(setting >> 32);
    const boost::uint32_t shadow = boost::uint32_t(_tree->access<boost::uint64_t>(mb_path / "gpio" / "FP0" / attr).get());
    const boost::uint32_t new_value = shadow_it(shadow, value, mask);
    if (attr == "CTRL") return gpio->set_pin_ctrl(dboard_iface::UNIT_RX, new_value);
    if (attr == "DDR") return gpio->set_gpio_ddr(dboard_iface::UNIT_RX, new_value);
    if (attr == "OUT") return gpio->set_gpio_out(dboard_iface::UNIT_RX, new_value);
    if (attr == "ATR_0X") return gpio->set_atr_reg(dboard_iface::UNIT_RX, dboard_iface::ATR_REG_IDLE, new_value);
    if (attr == "ATR_RX") return gpio->set_atr_reg(dboard_iface::UNIT_RX, dboard_iface::ATR_REG_RX_ONLY, new_value);
    if (attr == "ATR_TX") return gpio->set_atr_reg(dboard_iface::UNIT_RX, dboard_iface::ATR_REG_TX_ONLY, new_value);
    if (attr == "ATR_XX") return gpio->set_atr_reg(dboard_iface::UNIT_RX, dboard_iface::ATR_REG_FULL_DUPLEX, new_value);
}

/***********************************************************************
 * claimer logic
 **********************************************************************/

void x300_impl::claimer_loop(wb_iface::sptr iface)
{
    iface->poke32(SR_ADDR(X300_FW_SHMEM_BASE, X300_FW_SHMEM_CLAIM_TIME), time(NULL));
    iface->poke32(SR_ADDR(X300_FW_SHMEM_BASE, X300_FW_SHMEM_CLAIM_SRC), get_process_hash());
    boost::this_thread::sleep(boost::posix_time::milliseconds(1500)); //1.5 seconds
}

bool x300_impl::is_claimed(wb_iface::sptr iface)
{
    //If timed out then device is definitely unclaimed
    if (iface->peek32(SR_ADDR(X300_FW_SHMEM_BASE, X300_FW_SHMEM_CLAIM_STATUS)) == 0)
        return false;

    //otherwise check claim src to determine if another thread with the same src has claimed the device
    return iface->peek32(SR_ADDR(X300_FW_SHMEM_BASE, X300_FW_SHMEM_CLAIM_SRC)) != get_process_hash();
}

/***********************************************************************
 * MTU detection
 **********************************************************************/
x300_impl::mtu_result_t x300_impl::determine_mtu(const std::string &addr, const mtu_result_t &user_mtu)
{
    udp_simple::sptr udp = udp_simple::make_connected(addr, BOOST_STRINGIZE(X300_MTU_DETECT_UDP_PORT));

    std::vector<boost::uint8_t> buffer(std::max(user_mtu.recv_mtu, user_mtu.send_mtu));
    x300_mtu_t *request = reinterpret_cast<x300_mtu_t *>(&buffer.front());
    static const double echo_timeout = 0.020; //20 ms

    //test holler - check if its supported in this fw version
    request->flags = uhd::htonx<boost::uint32_t>(X300_MTU_DETECT_ECHO_REQUEST);
    request->size = uhd::htonx<boost::uint32_t>(sizeof(x300_mtu_t));
    udp->send(boost::asio::buffer(buffer, sizeof(x300_mtu_t)));
    udp->recv(boost::asio::buffer(buffer), echo_timeout);
    if (!(uhd::ntohx<boost::uint32_t>(request->flags) & X300_MTU_DETECT_ECHO_REPLY))
        throw uhd::not_implemented_error("Holler protocol not implemented");

    size_t min_recv_mtu = sizeof(x300_mtu_t);
    size_t max_recv_mtu = user_mtu.recv_mtu;
    size_t min_send_mtu = sizeof(x300_mtu_t);
    size_t max_send_mtu = user_mtu.send_mtu;

    UHD_MSG(status) << "Determining receive MTU ... ";
    while (min_recv_mtu < max_recv_mtu)
    {
       size_t test_mtu = (max_recv_mtu/2 + min_recv_mtu/2 + 3) & ~3;

       request->flags = uhd::htonx<boost::uint32_t>(X300_MTU_DETECT_ECHO_REQUEST);
       request->size = uhd::htonx<boost::uint32_t>(test_mtu);
       udp->send(boost::asio::buffer(buffer, sizeof(x300_mtu_t)));

       size_t len = udp->recv(boost::asio::buffer(buffer), echo_timeout);

       if (len >= test_mtu)
           min_recv_mtu = test_mtu;
       else
           max_recv_mtu = test_mtu - 4;

    }
    UHD_MSG(status) << min_recv_mtu << std::endl;

    UHD_MSG(status) << "Determining send MTU ... ";
    while (min_send_mtu < max_send_mtu)
    {
        size_t test_mtu = (max_send_mtu/2 + min_send_mtu/2 + 3) & ~3;

        request->flags = uhd::htonx<boost::uint32_t>(X300_MTU_DETECT_ECHO_REQUEST);
        request->size = uhd::htonx<boost::uint32_t>(sizeof(x300_mtu_t));
        udp->send(boost::asio::buffer(buffer, test_mtu));

        size_t len = udp->recv(boost::asio::buffer(buffer), echo_timeout);
        if (len >= sizeof(x300_mtu_t))
            len = uhd::ntohx<boost::uint32_t>(request->size);

        if (len >= test_mtu)
            min_send_mtu = test_mtu;
        else
            max_send_mtu = test_mtu - 4;
    }
    UHD_MSG(status) << min_send_mtu << std::endl;

    mtu_result_t mtu;
    mtu.recv_mtu = min_recv_mtu;
    mtu.send_mtu = min_send_mtu;
    return mtu;
}

/***********************************************************************
 * compat checks
 **********************************************************************/

void x300_impl::check_fw_compat(const fs_path &mb_path, wb_iface::sptr iface)
{
    boost::uint32_t compat_num = iface->peek32(SR_ADDR(X300_FW_SHMEM_BASE, X300_FW_SHMEM_COMPAT_NUM));
    boost::uint32_t compat_major = (compat_num >> 16);
    boost::uint32_t compat_minor = (compat_num & 0xffff);

    if (compat_major != X300_FW_COMPAT_MAJOR)
    {
        throw uhd::runtime_error(str(boost::format(
            "Expected firmware compatibility number 0x%x, but got 0x%x.%x:\n"
            "The firmware build is not compatible with the host code build.\n"
            "%s"
        ) % int(X300_FW_COMPAT_MAJOR) % compat_major % compat_minor
          % print_images_error()));
    }
    _tree->create<std::string>(mb_path / "fw_version").set(str(boost::format("%u.%u")
                % compat_major % compat_minor));
}

void x300_impl::check_fpga_compat(const fs_path &mb_path, wb_iface::sptr iface)
{
    boost::uint32_t compat_num = iface->peek32(SR_ADDR(SET0_BASE, ZPU_RB_COMPAT_NUM));
    boost::uint32_t compat_major = (compat_num >> 16);
    boost::uint32_t compat_minor = (compat_num & 0xffff);

    if (compat_major != X300_FPGA_COMPAT_MAJOR)
    {
        throw uhd::runtime_error(str(boost::format(
            "Expected FPGA compatibility number 0x%x, but got 0x%x.%x:\n"
            "The FPGA build is not compatible with the host code build.\n"
            "%s"
        ) % int(X300_FPGA_COMPAT_MAJOR) % compat_major % compat_minor
          % print_images_error()));
    }
    _tree->create<std::string>(mb_path / "fpga_version").set(str(boost::format("%u.%u")
                % compat_major % compat_minor));
}
