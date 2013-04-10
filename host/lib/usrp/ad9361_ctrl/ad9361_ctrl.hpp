//
// Copyright 2012-2013 Ettus Research LLC
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

#ifndef INCLUDED_AD9361_CTRL_HPP
#define INCLUDED_AD9361_CTRL_HPP

#include <uhd/types/serial.hpp>
#include <uhd/types/ranges.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/utility.hpp>
#include <boost/function.hpp>
#include <vector>
#include <string>

typedef boost::function<void(unsigned char *, size_t, unsigned char *, size_t)> ad9361_ctrl_cb_type;

class ad9361_ctrl : boost::noncopyable{
public:
    typedef boost::shared_ptr<ad9361_ctrl> sptr;

    //! make a new codec control object
    static sptr make(ad9361_ctrl_cb_type callback);

    //! Get a list of gain names for RX or TX
    static std::vector<std::string> get_gain_names(const std::string &/*which*/)
    {
        return std::vector<std::string>(1, "PGA");
    }

    //! get the gain range for a particular gain element
    static uhd::meta_range_t get_gain_range(const std::string &which)
    {
        if(which[0] == 'R') {
            return uhd::meta_range_t(0.0, 73.0, 1.0);
        } else {
            return uhd::meta_range_t(0.0, 89.75, 0.25);
        }
    }

    //! get the freq range for the frontend which
    static uhd::meta_range_t get_rf_freq_range(void)
    {
        return uhd::meta_range_t(30e6, 6e9);
    }

    //! get the filter range for the frontend which
    static uhd::meta_range_t get_bw_filter_range(const std::string &/*which*/)
    {
        return uhd::meta_range_t(200e3, 56e6);
    }

    //! get the filter range for the frontend which
    static uhd::meta_range_t get_samp_rate_range(void)
    {
        return uhd::meta_range_t(220e3, 61.44e6);
    }

    //! set the filter bandwidth for the frontend
    double set_bw_filter(const std::string &/*which*/, const double /*bw*/)
    {
        return 0.0; //TODO
    }

    //! set the gain for a particular gain element
    virtual double set_gain(const std::string &which, const double value) = 0;

    //! set a new clock rate, return the exact value
    virtual double set_clock_rate(const double rate) = 0;

    //! set which RX and TX chains/antennas are active
    virtual void set_active_chains(bool tx1, bool tx2, bool rx1, bool rx2) = 0;

    //! tune the given frontend, return the exact value
    virtual double tune(const std::string &which, const double value) = 0;

    //! output a ~480 kHz test tone at 800 MHz
    virtual void output_test_tone(void) = 0;

    //! turn on/off Catalina's data port loopback
    virtual void data_port_loopback_on(void) = 0;
    virtual void data_port_loopback_off(void) = 0;
};

#endif /* INCLUDED_AD9361_CTRL_HPP */
