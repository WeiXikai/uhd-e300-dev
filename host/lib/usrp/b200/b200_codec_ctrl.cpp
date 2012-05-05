//
// Copyright 2012 Ettus Research LLC
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

#include "b200_codec_ctrl.hpp"

using namespace uhd;
using namespace uhd::transport;

/***********************************************************************
 * The implementation class
 **********************************************************************/
class b200_codec_ctrl_impl : public b200_codec_ctrl{
public:

    b200_codec_ctrl_impl(b200_iface::sptr iface)
    {
        _b200_iface = iface;

        //reset
        write_reg(0x000,0x01);
        //clear reset
        write_reg(0x000,0x00);

        /********setup basic stuff (chip level setup 0-7)*/
        //enable RX1, TX1 @ 8Msps
        //TX1 en, THB3 interp x2, THB2 interp x2 fil. en, THB1 en, TX FIR interp 4 en
        write_reg(0x002, 0b01011111);
        //RX1 en, RHB3 decim x2, RHB2 decim x2 fil. en, RHB1 en, RX FIR decim 4 en
        write_reg(0x003, 0b01011111);
        //select TX1A/TX2A, RX antennas in balanced mode on ch. A
        write_reg(0x004, 0b00000011);

        /**set up clock interface*/
        //enable BBPLL, clocks, external clk
        write_reg(0x009, 0b00010111);

        /**set up BBPLL*/
        //BBPLL div 4, clkout enable, dac clk = adc clk, BBPLL div 4
        write_reg(0x00A, 0b00010010);

        /********setup data ports (FDD dual port DDR CMOS)*/
        //FDD dual port DDR CMOS no swap
        write_reg(0x010, 0b00001000);
        write_reg(0x011, 0b00000000);
        write_reg(0x012, 0b00000010); //force TX on one port, RX on the other, come back to this one
        write_reg(0x013, 0b00000001); //enable ENSM
        write_reg(0x014, 0b00001000); //use SPI for TXNRX ctrl
        write_reg(0x015, 0b10000111); //dual synth mode, synth en ctrl en

        /**initial VCO setup*/
        write_reg(0x261,0x00); //RX LO power
        write_reg(0x2a1,0x00); //TX LO power
        write_reg(0x248,0x0b); //en RX VCO LDO
        write_reg(0x288,0x0b); //en TX VCO LDO
        write_reg(0x246,0x02); //pd RX cal Tcf
        write_reg(0x286,0x02); //pd TX cal Tcf
        write_reg(0x243,0x0d); //set rx prescaler bias
        write_reg(0x283,0x0d); //"" TX
        write_reg(0x245,0x00); //set RX VCO cal ref Tcf
        write_reg(0x250,0x70); //set RX VCO varactor ref Tcf
        write_reg(0x285,0x00); //"" TX
        write_reg(0x290,0x70); //"" TX
        write_reg(0x239,0xc1); //init RX ALC
        write_reg(0x279,0xc1); //"" TX
        write_reg(0x23b,0x80); //set RX MSB?
        write_reg(0x27b,0x80); //"" TX
        write_reg(0x23d,0x00); //clear RX 1/2 VCO cal clk
        write_reg(0x27d,0x00); //"" TX

        

        //set_clock_rate(40e6); //init ref clk (done above)
    }

    std::vector<std::string> get_gain_names(const std::string &which)
    {
        //TODO
        return std::vector<std::string>();
    }

    double set_gain(const std::string &which, const std::string &name, const double value)
    {
        //TODO
        return 0.0;
    }

    uhd::meta_range_t get_gain_range(const std::string &which, const std::string &name)
    {
        //TODO
        return uhd::meta_range_t(0.0, 0.0);
    }

    double set_clock_rate(const double rate)
    {
        //clock rate is just the output clock rate to the FPGA
        //for Catalina sample rate, see set_sample_rate
        //set ref to refclk in via XTAL_N
        //set ref to 40e6
        //BBPLL input scale keep between 35-70MHz
        //RXPLL input scale keep between 40-150MHz?
        //enable CLK_OUT=REF_CLK_IN (could use div. of ADC clk but that'll happen later)
        
        return 40e6;
    }

    double tune(const std::string &which, const double value)
    {
        //setup charge pump based on horrible PLL lock doc
        //setup VCO/RFPLL based on rx/tx freq
        //VCO cal

        //this is the example 800MHz/850MHz stuff from the tuning document
        //set rx to 800, tx to 850
        if(which == "RX") {
            //set up synth
            write_reg(0x23a, 0x4a);//vco output level
            write_reg(0x239, 0xc3);//init ALC value and VCO varactor
            write_reg(0x242, 0x1f);//vco bias and bias ref
            write_reg(0x238, 0x78);//vco cal offset
            write_reg(0x245, 0x00);//vco cal ref tcf
            write_reg(0x251, 0x0c);//varactor ref
            write_reg(0x250, 0x70);//vco varactor ref tcf
            write_reg(0x240, 0x09);//rx synth loop filter r3
            write_reg(0x23f, 0xdf);//r1 and c3
            write_reg(0x23e, 0xd4);//c2 and c1
            write_reg(0x23b, 0x92);//Icp

            //tune that shit
            write_reg(0x233, 0x00);
            write_reg(0x234, 0x00);
            write_reg(0x235, 0x00);
            write_reg(0x232, 0x00);
            write_reg(0x231, 0x50);
            write_reg(0x005, 0x22);
            
            //TODO: read reg 0x247, verify bit 1 (RX PLL locked)
            return 800.0e6;
        } else {
            write_reg(0x27a, 0x4a);//vco output level
            write_reg(0x279, 0xc1);//init ALC value and VCO varactor
            write_reg(0x282, 0x17);//vco bias and bias ref
            write_reg(0x278, 0x70);//vco cal offset
            write_reg(0x285, 0x00);//vco cal ref tcf
            write_reg(0x291, 0x0e);//varactor ref
            write_reg(0x290, 0x70);//vco varactor ref tcf
            write_reg(0x280, 0x09);//rx synth loop filter r3
            write_reg(0x27f, 0xdf);//r1 and c3
            write_reg(0x27e, 0xd4);//c2 and c1
            write_reg(0x27b, 0x98);//Icp

            //tuning yo
            write_reg(0x273, 0x00);
            write_reg(0x274, 0x00);
            write_reg(0x275, 0x00);
            write_reg(0x272, 0x00);
            write_reg(0x271, 0x55);
            write_reg(0x005, 0x22);
            
            //TODO: read reg 0x287, verify bit 1 (TX PLL locked)
            return 850.0e6;
        }
    }

    virtual double set_sample_rate(const double rate)
    {
        //set up BBPLL
    }

    virtual double set_filter_bw(const std::string &which, const double bw)
    {
        //set up BB filter
    }

private:
    b200_iface::sptr _b200_iface;

    void write_reg(uint16_t reg, uint8_t val)
    {
        uint8_t buf[4];
        buf[0] = (reg >> 8) & 0x3F;
        buf[1] = (reg & 0xFF);
        buf[2] = val;
        _b200_iface->transact_spi(buf, 24, false);
    }
};

/***********************************************************************
 * Make an instance of the implementation
 **********************************************************************/
b200_codec_ctrl::sptr b200_codec_ctrl::make(b200_iface::sptr iface)
{
    return sptr(new b200_codec_ctrl_impl(iface));
}
