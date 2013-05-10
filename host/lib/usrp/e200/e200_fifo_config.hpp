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

#ifndef INCLUDED_E200_FIFO_CONFIG_HPP
#define INCLUDED_E200_FIFO_CONFIG_HPP

#include <uhd/types/device_addr.hpp>
#include <uhd/transport/zero_copy.hpp>

struct e200_fifo_config_t
{
    size_t ctrl_length;
    size_t buff_length;
    size_t phys_addr;
};

e200_fifo_config_t e200_read_sysfs(void);

struct e200_fifo_interface
{
    typedef boost::shared_ptr<e200_fifo_interface> sptr;
    static sptr make(const e200_fifo_config_t &config);
    virtual uhd::transport::zero_copy_if::sptr make_recv_xport(const size_t which_stream, const uhd::device_addr_t &args) = 0;
    virtual uhd::transport::zero_copy_if::sptr make_send_xport(const size_t which_stream, const uhd::device_addr_t &args) = 0;
};

#endif /* INCLUDED_E200_FIFO_CONFIG_HPP */
