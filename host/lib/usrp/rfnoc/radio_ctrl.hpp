//
// Copyright 2014 Ettus Research LLC
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

#ifndef INCLUDED_LIBUHD_RFNOC_RADIO_CTRL_HPP
#define INCLUDED_LIBUHD_RFNOC_RADIO_CTRL_HPP

#include <uhd/usrp/rfnoc/block_ctrl_base.hpp>
#include "rx_vita_core_3000.hpp"
#include "tx_vita_core_3000.hpp"
#include "time_core_3000.hpp"
#include "rx_dsp_core_3000.hpp"
#include "tx_dsp_core_3000.hpp"

namespace uhd {
    namespace rfnoc {

/*! \brief Provide access to a radio.
 *
 */
class radio_ctrl : virtual public block_ctrl_base
{
public:
    UHD_RFNOC_BLOCK_OBJECT(radio_ctrl)

    //! Pass stream commands to the radio
    virtual void issue_stream_cmd(const uhd::stream_cmd_t &stream_cmd) = 0;

    virtual bool set_bytes_per_output_packet(
            size_t bpp,
            size_t out_block_port=0
    ) = 0;

    virtual size_t get_bytes_per_output_packet(size_t out_block_port=0) = 0;

    //! This must be overridden because as a true source, we must also
    // set the source address.
    virtual void set_destination(boost::uint32_t next_address, size_t output_block_port = 0) = 0;

    virtual void set_perifs(
        time_core_3000::sptr    time64,
        rx_vita_core_3000::sptr framer,
        rx_dsp_core_3000::sptr  ddc,
        tx_vita_core_3000::sptr deframer,
        tx_dsp_core_3000::sptr  duc
    ) = 0;

}; /* class radio_ctrl*/

}} /* namespace uhd::rfnoc */

#endif /* INCLUDED_LIBUHD_RFNOC_RADIO_CTRL_HPP */
// vim: sw=4 et:
