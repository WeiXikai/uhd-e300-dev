//
// Copyright 2013-2014 Ettus Research LLC
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

#ifndef INCLUDED_E300_IMPL_HPP
#define INCLUDED_E300_IMPL_HPP

#include <uhd/device.hpp>
#include <uhd/property_tree.hpp>
#include <uhd/usrp/subdev_spec.hpp>
#include <uhd/usrp/mboard_eeprom.hpp>
#include <uhd/usrp/dboard_eeprom.hpp>
#include <uhd/transport/bounded_buffer.hpp>
#include <uhd/types/serial.hpp>
#include <uhd/types/sensors.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/thread/mutex.hpp>
#include "e300_fifo_config.hpp"
#include "radio_ctrl_core_3000.hpp"
#include "rx_vita_core_3000.hpp"
#include "tx_vita_core_3000.hpp"
#include "time_core_3000.hpp"
#include "rx_dsp_core_3000.hpp"
#include "tx_dsp_core_3000.hpp"
#include "ad9361_ctrl.hpp"
#include "gpio_core_200.hpp"

#include "e300_global_regs.hpp"
#include "e300_i2c.hpp"
#include "e300_eeprom_manager.hpp"

namespace uhd { namespace usrp { namespace e300 {

static const std::string E300_FPGA_FILE_NAME = "usrp_e300_fpga.bit";
static const std::string E300_TEMP_SYSFS = "iio:device0";
static const std::string E300_SPIDEV_DEVICE  = "/dev/spidev0.1";
static const std::string E300_I2CDEV_DEVICE  = "/dev/i2c-0";

static std::string E300_SERVER_RX_PORT0 = "321756";
static std::string E300_SERVER_TX_PORT0 = "321757";
static std::string E300_SERVER_CTRL_PORT0 = "321758";

static std::string E300_SERVER_RX_PORT1 = "321856";
static std::string E300_SERVER_TX_PORT1 = "321857";
static std::string E300_SERVER_CTRL_PORT1 = "321858";


static std::string E300_SERVER_CODEC_PORT = "321759";
static std::string E300_SERVER_GREGS_PORT = "321760";
static std::string E300_SERVER_I2C_PORT   = "321761";

static const double E300_DEFAULT_TICK_RATE = 32e6;

static const double E300_RX_SW_BUFF_FULLNESS = 0.5;        //Buffer should be half full

// crossbar settings
static const boost::uint8_t E300_RADIO_DEST_PREFIX_TX   = 0;
static const boost::uint8_t E300_RADIO_DEST_PREFIX_CTRL = 1;
static const boost::uint8_t E300_RADIO_DEST_PREFIX_RX   = 2;

static const boost::uint8_t E300_XB_DST_AXI = 0;
static const boost::uint8_t E300_XB_DST_R0  = 1;
static const boost::uint8_t E300_XB_DST_R1  = 2;
static const boost::uint8_t E300_XB_DST_CE0 = 3;
static const boost::uint8_t E300_XB_DST_CE1 = 4;

static const boost::uint8_t E300_DEVICE_THERE = 2;
static const boost::uint8_t E300_DEVICE_HERE  = 0;

static const size_t E300_R0_CTRL_STREAM    = (0 << 2) | E300_RADIO_DEST_PREFIX_CTRL;
static const size_t E300_R0_TX_DATA_STREAM = (0 << 2) | E300_RADIO_DEST_PREFIX_TX;
static const size_t E300_R0_RX_DATA_STREAM = (0 << 2) | E300_RADIO_DEST_PREFIX_RX;

static const size_t E300_R1_CTRL_STREAM    = (1 << 2) | E300_RADIO_DEST_PREFIX_CTRL;
static const size_t E300_R1_TX_DATA_STREAM = (1 << 2) | E300_RADIO_DEST_PREFIX_TX;
static const size_t E300_R1_RX_DATA_STREAM = (1 << 2) | E300_RADIO_DEST_PREFIX_RX;


/*!
 * USRP-E300 implementation guts:
 * The implementation details are encapsulated here.
 * Handles properties on the mboard, dboard, dsps...
 */
class e300_impl : public uhd::device
{
public:
    //structors
    e300_impl(const uhd::device_addr_t &);
    virtual ~e300_impl(void);

    //the io interface
    boost::mutex _stream_spawn_mutex;
    uhd::rx_streamer::sptr get_rx_stream(const uhd::stream_args_t &);
    uhd::tx_streamer::sptr get_tx_stream(const uhd::stream_args_t &);

    typedef uhd::transport::bounded_buffer<uhd::async_metadata_t> async_md_type;
    boost::shared_ptr<async_md_type> _async_md;

    bool recv_async_msg(uhd::async_metadata_t &, double);

private: // types
    // sid convenience struct
    struct sid_config_t
    {
        boost::uint8_t router_addr_there;
        boost::uint8_t dst_prefix; //2bits
        boost::uint8_t router_dst_there;
        boost::uint8_t router_dst_here;
    };

    // perifs in the radio core
    struct radio_perifs_t
    {
        radio_ctrl_core_3000::sptr ctrl;
        gpio_core_200_32wo::sptr atr;
        time_core_3000::sptr time64;
        rx_vita_core_3000::sptr framer;
        rx_dsp_core_3000::sptr ddc;
        tx_vita_core_3000::sptr deframer;
        tx_dsp_core_3000::sptr duc;

        uhd::transport::zero_copy_if::sptr send_ctrl_xport;
        uhd::transport::zero_copy_if::sptr recv_ctrl_xport;
        uhd::transport::zero_copy_if::sptr tx_data_xport;
        uhd::transport::zero_copy_if::sptr tx_flow_xport;
        uhd::transport::zero_copy_if::sptr rx_data_xport;
        uhd::transport::zero_copy_if::sptr rx_flow_xport;
        boost::weak_ptr<uhd::rx_streamer> rx_streamer;
        boost::weak_ptr<uhd::tx_streamer> tx_streamer;
    };

    //frontend cache so we can update gpios
    struct fe_control_settings_t
    {
        fe_control_settings_t(void)
        {
            rx_ant = "RX2";
            tx_enb = false;
            rx_enb = false;
            rx_freq = 1e9;
            tx_freq = 1e9;
        }
        std::string rx_ant;
        bool tx_enb;
        bool rx_enb;
        double rx_freq;
        double tx_freq;
    };

private: // methods
    void _load_fpga_image(const std::string &path);

    void _register_loopback_self_test(uhd::wb_iface::sptr iface);

    void _setup_radio(const size_t which_radio);

    boost::uint32_t _allocate_sid(const sid_config_t &config);

    void _setup_dest_mapping(
        const boost::uint32_t sid,
        const size_t which_stream);

    double _get_tick_rate(void){return _tick_rate;}
    double _set_tick_rate(const double rate);

    void _update_tick_rate(const double);
    void _update_rx_samp_rate(const size_t, const double);
    void _update_tx_samp_rate(const size_t, const double);

    void _update_time_source(const std::string &source);
    void _update_clock_source(const std::string &);

    void _update_rx_subdev_spec(const uhd::usrp::subdev_spec_t &spec);
    void _update_tx_subdev_spec(const uhd::usrp::subdev_spec_t &spec);

    void _codec_loopback_self_test(uhd::wb_iface::sptr iface);

    void _update_atrs(const size_t &fe);
    void _update_antenna_sel(const std::string &fe, const std::string &ant);
    void _update_fe_lo_freq(const std::string &fe, const double freq);
    void _update_active_frontends(void);

    // sensors
    uhd::sensor_value_t _get_mb_temp(void);

    // internal gpios
    boost::uint8_t _get_internal_gpio(
        gpio_core_200::sptr,
        const std::string &);

    void _set_internal_gpio(
        gpio_core_200::sptr gpio,
        const std::string &attr,
        const boost::uint32_t value);

    // server stuff for network access
    void _run_server(
        const std::string &port,
        const std::string &what,
        const size_t fe);

private: // members
    bool                        _network_mode;
    e300_fifo_interface::sptr   _fifo_iface;
    size_t                      _sid_framer;
    radio_perifs_t              _radio_perifs[2];
    double                      _tick_rate;
    ad9361_ctrl_transport::sptr _codec_xport;
    ad9361_ctrl::sptr           _codec_ctrl;
    fe_control_settings_t       _fe_control_settings[2];
    global_regs::sptr           _global_regs;
    i2c::sptr                   _i2c;
    e300_eeprom_manager::sptr   _eeprom_manager;
};

}}} // namespace

#endif /* INCLUDED_E300_IMPL_HPP */
