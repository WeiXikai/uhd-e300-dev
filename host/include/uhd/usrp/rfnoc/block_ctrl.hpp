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

#ifndef INCLUDED_LIBUHD_RFNOC_BLOCK_CTRL_HPP
#define INCLUDED_LIBUHD_RFNOC_BLOCK_CTRL_HPP

#include <uhd/usrp/rfnoc/block_ctrl_base.hpp>

namespace uhd {
    namespace rfnoc {

/*! \brief Provide access basic to functionality of an RFNoC block.
 */
class UHD_API block_ctrl : virtual public block_ctrl_base
{
public:
    typedef boost::shared_ptr<block_ctrl> sptr;

    static sptr make(
            uhd::wb_iface::sptr ctrl_iface,
            uhd::sid_t ctrl_sid,
            size_t device_index,
            uhd::property_tree::sptr tree
    );

    // Nothing else here -- all function definitions are in block_ctrl_base

}; /* class block_ctrl*/

}} /* namespace uhd::rfnoc */

#endif /* INCLUDED_LIBUHD_RFNOC_BLOCK_CTRL_HPP */
// vim: sw=4 et:
