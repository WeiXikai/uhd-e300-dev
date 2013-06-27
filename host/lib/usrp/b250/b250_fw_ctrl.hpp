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

#ifndef INCLUDED_B250_FW_CTRL_HPP
#define INCLUDED_B250_FW_CTRL_HPP

#include "wb_iface.hpp"
#include <uhd/transport/udp_simple.hpp>
#include <uhd/utils/byteswap.hpp>
#include <uhd/utils/msg.hpp>
#include <uhd/exception.hpp>
#include <boost/format.hpp>
#include <boost/thread/mutex.hpp>
#include <uhd/transport/nirio/status.h>
#include <uhd/transport/nirio/nirio_interface.h>
#include "b250_regs.hpp"
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/thread/thread.hpp>

class b250_ctrl_iface : public wb_iface
{
public:
    enum {num_retries = 3};

    void flush(void)
    {
        __flush();
    }

    void poke32(const wb_addr_type addr, const boost::uint32_t data)
    {
        for (size_t i = 1; i <= num_retries; i++)
        {
            try
            {
//                UHD_MSG(status) << boost::format("b250_ctrl_iface::poke32(0x%x) <= 0x%x") % addr % data << std::endl;
                return this->__poke32(addr, data);
            }
            catch(const std::exception &ex)
            {
                const std::string error_msg = str(boost::format(
                    "b250 fw communication failure #%u\n%s") % i % ex.what());
                UHD_MSG(error) << error_msg << std::endl;
                if (i == num_retries) throw uhd::io_error(error_msg);
            }
        }
    }

    boost::uint32_t peek32(const wb_addr_type addr)
    {
        for (size_t i = 1; i <= num_retries; i++)
        {
            try
            {
                boost::uint32_t data = this->__peek32(addr);
//                UHD_MSG(status) << boost::format("b250_ctrl_iface::peek32(0x%x) == 0x%x") % addr % data << std::endl;
                return data;
            }
            catch(const std::exception &ex)
            {
                const std::string error_msg = str(boost::format(
                    "b250 fw communication failure #%u\n%s") % i % ex.what());
                UHD_MSG(error) << error_msg << std::endl;
                if (i == num_retries) throw uhd::io_error(error_msg);
            }
        }
        return 0;
    }

protected:
    virtual void __poke32(const wb_addr_type addr, const boost::uint32_t data) = 0;
    virtual boost::uint32_t __peek32(const wb_addr_type addr) = 0;
    virtual void __flush() { /* NOP */}

    boost::mutex mutex;
};


//-----------------------------------------------------
// Ethernet impl
//-----------------------------------------------------
class b250_ctrl_iface_enet : public b250_ctrl_iface
{
public:
    b250_ctrl_iface_enet(uhd::transport::udp_simple::sptr udp):
        udp(udp), seq(0)
    {
        try
        {
            this->peek32(0);
        }
        catch(...){}
    }

protected:
    virtual void __poke32(const wb_addr_type addr, const boost::uint32_t data)
    {
        boost::mutex::scoped_lock lock(mutex);

        //load request struct
        b250_fw_comms_t request = b250_fw_comms_t();
        request.flags = uhd::htonx<boost::uint32_t>(B250_FW_COMMS_FLAGS_ACK | B250_FW_COMMS_FLAGS_POKE32);
        request.sequence = uhd::htonx<boost::uint32_t>(seq++);
        request.addr = uhd::htonx(addr);
        request.data = uhd::htonx(data);

        //send request
        this->flush();
        udp->send(boost::asio::buffer(&request, sizeof(request)));

        //recv reply
        b250_fw_comms_t reply = b250_fw_comms_t();
        const size_t nbytes = udp->recv(boost::asio::buffer(&reply, sizeof(reply)), 1.0);
        if (nbytes == 0) throw uhd::io_error("b250 fw poke32 - reply timed out");

        //sanity checks
        const size_t flags = uhd::ntohx<boost::uint32_t>(reply.flags);
        UHD_ASSERT_THROW(nbytes == sizeof(reply));
        UHD_ASSERT_THROW(not (flags & B250_FW_COMMS_FLAGS_ERROR));
        UHD_ASSERT_THROW(flags & B250_FW_COMMS_FLAGS_POKE32);
        UHD_ASSERT_THROW(flags & B250_FW_COMMS_FLAGS_ACK);
        UHD_ASSERT_THROW(reply.sequence == request.sequence);
        UHD_ASSERT_THROW(reply.addr == request.addr);
        UHD_ASSERT_THROW(reply.data == request.data);
    }

    virtual boost::uint32_t __peek32(const wb_addr_type addr)
    {
        boost::mutex::scoped_lock lock(mutex);

        //load request struct
        b250_fw_comms_t request = b250_fw_comms_t();
        request.flags = uhd::htonx<boost::uint32_t>(B250_FW_COMMS_FLAGS_ACK | B250_FW_COMMS_FLAGS_PEEK32);
        request.sequence = uhd::htonx<boost::uint32_t>(seq++);
        request.addr = uhd::htonx(addr);
        request.data = 0;

        //send request
        this->flush();
        udp->send(boost::asio::buffer(&request, sizeof(request)));

        //recv reply
        b250_fw_comms_t reply = b250_fw_comms_t();
        const size_t nbytes = udp->recv(boost::asio::buffer(&reply, sizeof(reply)), 1.0);
        if (nbytes == 0) throw uhd::io_error("b250 fw peek32 - reply timed out");

        //sanity checks
        const size_t flags = uhd::ntohx<boost::uint32_t>(reply.flags);
        UHD_ASSERT_THROW(nbytes == sizeof(reply));
        UHD_ASSERT_THROW(not (flags & B250_FW_COMMS_FLAGS_ERROR));
        UHD_ASSERT_THROW(flags & B250_FW_COMMS_FLAGS_PEEK32);
        UHD_ASSERT_THROW(flags & B250_FW_COMMS_FLAGS_ACK);
        UHD_ASSERT_THROW(reply.sequence == request.sequence);
        UHD_ASSERT_THROW(reply.addr == request.addr);

        //return result!
        return uhd::ntohx<boost::uint32_t>(reply.data);
    }

    virtual void __flush(void)
    {
        char buff[B250_FW_COMMS_MTU] = {};
        while (udp->recv(boost::asio::buffer(buff), 0.0)){} //flush
    }

private:
    uhd::transport::udp_simple::sptr udp;
    size_t seq;
};


//-----------------------------------------------------
// PCIe impl
//-----------------------------------------------------
class b250_ctrl_iface_pcie : public b250_ctrl_iface
{
public:
    b250_ctrl_iface_pcie(nirio_interface::niriok_proxy& drv_proxy):
        _drv_proxy(drv_proxy)
    {
        UHD_ASSERT_THROW(nirio_status_not_fatal(
            _drv_proxy.set_attribute(kRioAddressSpace, kRioAddressSpaceBusInterface)));
    }

protected:
    virtual void __poke32(const wb_addr_type addr, const boost::uint32_t data)
    {
        //@TODO: Find a better way to throttle
        __peek32(0xa000);
        boost::this_thread::sleep(boost::posix_time::microsec(200));
        boost::mutex::scoped_lock lock(mutex);

        UHD_ASSERT_THROW(
            nirio_status_not_fatal(_drv_proxy.poke(PCIE_ZPU_DATA_REG(addr), data)));
    }

    virtual boost::uint32_t __peek32(const wb_addr_type addr)
    {
        boost::mutex::scoped_lock lock(mutex);

        boost::uint32_t reg_data = 0xFFFFFFFF;
        boost::posix_time::ptime start_time = boost::posix_time::microsec_clock::local_time();
        boost::posix_time::time_duration elapsed;

        nirio_status status = 0;
        nirio_status_chain(_drv_proxy.peek(PCIE_ZPU_READ_REG(addr), reg_data), status);

        if (nirio_status_not_fatal(status)) {
            do {
                nirio_status_chain(_drv_proxy.peek(PCIE_ZPU_READ_REG(addr), reg_data), status);
                elapsed = boost::posix_time::microsec_clock::local_time() - start_time;
                boost::this_thread::sleep(boost::posix_time::microsec(100));
            } while (nirio_status_not_fatal(status) && reg_data != 0 &&
                     elapsed.total_milliseconds() < READ_TIMEOUT_IN_MS);
        }

        UHD_ASSERT_THROW(
            nirio_status_not_fatal(_drv_proxy.peek(PCIE_ZPU_DATA_REG(addr), reg_data)));
        return reg_data;
    }

private:
    nirio_interface::niriok_proxy& _drv_proxy;
    static const uint32_t READ_TIMEOUT_IN_MS = 50;
};

#endif /* INCLUDED_B250_FW_CTRL_HPP */
