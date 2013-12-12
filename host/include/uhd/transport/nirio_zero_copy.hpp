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

#ifndef INCLUDED_UHD_TRANSPORT_NIRIO_ZERO_COPY_HPP
#define INCLUDED_UHD_TRANSPORT_NIRIO_ZERO_COPY_HPP

#include <uhd/transport/nirio/niusrprio_session.h>
#include <uhd/config.hpp>
#include <uhd/transport/zero_copy.hpp>
#include <uhd/types/device_addr.hpp>
#include <boost/shared_ptr.hpp>

namespace uhd{ namespace transport{

struct nirio_zero_copy_buff_args {
    size_t recv_frame_size;
    size_t send_frame_size;
    size_t num_recv_frames;
    size_t num_send_frames;
};

class UHD_API nirio_zero_copy : public virtual zero_copy_if{
public:
    typedef boost::shared_ptr<nirio_zero_copy> sptr;

    static sptr make(
        uhd::niusrprio::niusrprio_session::sptr fpga_session,
        const uint32_t instance,
        const nirio_zero_copy_buff_args &default_buff_args,
        const device_addr_t &hints = device_addr_t()
    );
};

}} //namespace

#endif /* INCLUDED_UHD_TRANSPORT_NIRIO_ZERO_COPY_HPP */
