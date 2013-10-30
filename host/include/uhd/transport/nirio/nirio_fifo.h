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


#ifndef NIRIO_FIFO_H_
#define NIRIO_FIFO_H_

#include <uhd/transport/nirio/nirio_interface.h>
#include <uhd/transport/nirio/status.h>
#include <boost/noncopyable.hpp>
#include <boost/smart_ptr.hpp>
#include <string>
#include <boost/thread/recursive_mutex.hpp>

namespace nirio_interface {

enum fifo_direction_t {
	INPUT_FIFO,
	OUTPUT_FIFO
};

enum nirio_scalar_t {
   SCALAR_I8 	= 1UL,
   SCALAR_I16 	= 2UL,
   SCALAR_I32 	= 3UL,
   SCALAR_I64 	= 4UL,
   SCALAR_U8 	= 5UL,
   SCALAR_U16 	= 6UL,
   SCALAR_U32 	= 7UL,
   SCALAR_U64 	= 8UL
};

struct datatype_info_t {
	datatype_info_t(nirio_scalar_t t, uint32_t w):scalar_type(t),width(w) {}
	nirio_scalar_t 	scalar_type;
	uint32_t		width;
};

template <typename data_t>
class nirio_fifo : private boost::noncopyable
{
public:
    typedef boost::shared_ptr< nirio_fifo<data_t> > sptr;

    nirio_fifo(
		niriok_proxy& riok_proxy,
		fifo_direction_t direction,
		const std::string& name,
		uint32_t fifo_instance);
	virtual ~nirio_fifo();

	nirio_status initialize(
		const size_t requested_depth,
		size_t& actual_depth,
		size_t& actual_size);

	void finalize();

	inline const std::string& get_name() const { return _name; }
	inline uint32_t get_channel() const { return _fifo_channel; }
	inline uint32_t get_direction() const { return _fifo_direction; }
	inline uint32_t get_scalar_type() const { return _datatype_info.scalar_type; }

	nirio_status start();

	nirio_status stop();

	nirio_status acquire(
		data_t*& elements,
		const size_t elements_requested,
		const uint32_t timeout,
		size_t& elements_acquired,
		size_t& elements_remaining);

	nirio_status release(const size_t elements);

	nirio_status read(
		data_t* buf,
		const uint32_t num_elements,
		uint32_t timeout,
		uint32_t& num_read,
		uint32_t& num_remaining);

	nirio_status write(
		const data_t* buf,
		const uint32_t num_elements,
		uint32_t timeout,
		uint32_t& num_remaining);

private:	//Methods
	bool _is_initialized();
	datatype_info_t _get_datatype_info();

private:	//Members
	std::string							_name;
	fifo_direction_t					_fifo_direction;
	uint32_t							_fifo_channel;
	datatype_info_t						_datatype_info;
    size_t                              _acquired_pending;
	nirio_driver_iface::rio_mmap_t		_mem_map;
	boost::recursive_mutex              _mutex;

	niriok_proxy* 						_riok_proxy_ptr;

	static const uint32_t FIFO_LOCK_TIMEOUT_IN_MS = 5000;
};

#include "nirio_fifo.ipp"

}

#endif /* NIRIO_FIFO_H_ */
