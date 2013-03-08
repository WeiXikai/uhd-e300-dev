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

#ifndef INCLUDED_B250_CODEC_CTRL_HPP
#define INCLUDED_B250_CODEC_CTRL_HPP

#include <uhd/types/serial.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/utility.hpp>

class b250_adc_ctrl : boost::noncopyable
{
public:
    typedef boost::shared_ptr<b250_adc_ctrl> sptr;

    /*!
     * Make a codec control for theADC.
     * \param iface a pointer to the usrp2 interface object
     * \param spiface the interface to spi
     * \return a new codec control object
     */
    static sptr make(uhd::spi_iface::sptr iface, const size_t slaveno);

    /*!
     * Set the analog preamplifier on the USRP2+ ADC (ADS62P44).
     * \param gain enable or disable the 3.5dB preamp
     */

    virtual void set_rx_analog_gain(bool gain) = 0;

    /*!
     * Set the digital gain on the USRP2+ ADC (ADS62P44).
     * \param gain from 0-6dB
     */

    virtual void set_rx_digital_gain(double gain) = 0;

    /*!
     * Set the digital gain correction on the USRP2+ ADC (ADS62P44).
     * \param gain from 0-0.5dB
     */

    virtual void set_rx_digital_fine_gain(double gain) = 0;

};

#endif /* INCLUDED_B250_CODEC_CTRL_HPP */
