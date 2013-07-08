//
// Copyright 2010-2013 Ettus Research LLC
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

#include "b250_adc_ctrl.hpp"
#include "ads62p48_regs.hpp"
#include <uhd/types/ranges.hpp>
#include <uhd/utils/log.hpp>
#include <uhd/utils/safe_call.hpp>
#include <uhd/exception.hpp>
#include <boost/foreach.hpp>

using namespace uhd;

/*!
 * A B250 codec control specific to the ads62p48 ic.
 */
class b250_adc_ctrl_impl : public b250_adc_ctrl
{
public:
    b250_adc_ctrl_impl(uhd::spi_iface::sptr iface, const size_t slaveno):
        _iface(iface), _slaveno(slaveno)
    {
        //power-up adc
       _ads62p48_regs.reset = 1;
        this->send_ads62p48_reg(0x00); //issue a reset to the ADC
       _ads62p48_regs.reset = 0;

        _ads62p48_regs.enable_low_speed_mode = 0;
        _ads62p48_regs.ref = ads62p48_regs_t::REF_INTERNAL;
        _ads62p48_regs.standby = ads62p48_regs_t::STANDBY_NORMAL;
        _ads62p48_regs.power_down = ads62p48_regs_t::POWER_DOWN_NORMAL;
        _ads62p48_regs.lvds_cmos = ads62p48_regs_t::LVDS_CMOS_DDR_LVDS;
        _ads62p48_regs.channel_control = ads62p48_regs_t::CHANNEL_CONTROL_COMMON;
        _ads62p48_regs.data_format = ads62p48_regs_t::DATA_FORMAT_2S_COMPLIMENT;

        this->send_ads62p48_reg(0);
        this->send_ads62p48_reg(0x20);
        this->send_ads62p48_reg(0x3f);
        this->send_ads62p48_reg(0x40);
        this->send_ads62p48_reg(0x41);
        this->send_ads62p48_reg(0x44);
        this->send_ads62p48_reg(0x50);
        this->send_ads62p48_reg(0x51);
        this->send_ads62p48_reg(0x52);
        this->send_ads62p48_reg(0x53);
        this->send_ads62p48_reg(0x55);
        this->send_ads62p48_reg(0x57);
        this->send_ads62p48_reg(0x62);
        this->send_ads62p48_reg(0x63);
        this->send_ads62p48_reg(0x66);
        this->send_ads62p48_reg(0x68);
        this->send_ads62p48_reg(0x6a);
        this->send_ads62p48_reg(0x75);
        this->send_ads62p48_reg(0x76);

    }

    double set_gain(const double &gain)
    {
        const meta_range_t gain_range = meta_range_t(0, 6.0, 0.5);
        const int gain_bits = int((gain_range.clip(gain)*2.0) + 0.5);
        _ads62p48_regs.gain_chA = gain_bits;
        _ads62p48_regs.gain_chB = gain_bits;
        this->send_ads62p48_reg(0x55);
        this->send_ads62p48_reg(0x68);
        return gain_bits/2;
    }

    ~b250_adc_ctrl_impl(void)
    {
        _ads62p48_regs.power_down = ads62p48_regs_t::POWER_DOWN_GLOBAL;
        UHD_SAFE_CALL
        (
            this->send_ads62p48_reg(0x40);
        )
    }

private:
    ads62p48_regs_t _ads62p48_regs;
    uhd::spi_iface::sptr _iface;
    const size_t _slaveno;

    void send_ads62p48_reg(boost::uint8_t addr)
    {
        boost::uint16_t reg = _ads62p48_regs.get_write_reg(addr);
        _iface->write_spi(_slaveno, spi_config_t::EDGE_FALL, reg, 16);
    }
};

/***********************************************************************
 * Public make function for the ADC control
 **********************************************************************/
b250_adc_ctrl::sptr b250_adc_ctrl::make(uhd::spi_iface::sptr iface, const size_t slaveno)
{
    return sptr(new b250_adc_ctrl_impl(iface, slaveno));
}
