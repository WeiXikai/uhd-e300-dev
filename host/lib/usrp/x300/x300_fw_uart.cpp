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

#include "x300_impl.hpp"
#include <uhd/types/wb_iface.hpp>
#include "x300_regs.hpp"
#include <uhd/utils/msg.hpp>
#include <uhd/types/serial.hpp>
#include <uhd/exception.hpp>
#include <boost/format.hpp>
#include <boost/foreach.hpp>
#include <boost/thread/thread.hpp>

using namespace uhd;

struct x300_uart_iface : uart_iface
{
    x300_uart_iface(wb_iface::sptr iface):
        rxoffset(0), txoffset(0), txword32(0), rxpool(0), txpool(0), poolsize(0)
    {
        _iface = iface;
        rxoffset = _iface->peek32(SR_ADDR(X300_FW_SHMEM_BASE, X300_FW_SHMEM_UART_RX_INDEX));
        txoffset = _iface->peek32(SR_ADDR(X300_FW_SHMEM_BASE, X300_FW_SHMEM_UART_TX_INDEX));
        rxpool = _iface->peek32(SR_ADDR(X300_FW_SHMEM_BASE, X300_FW_SHMEM_UART_RX_ADDR));
        txpool = _iface->peek32(SR_ADDR(X300_FW_SHMEM_BASE, X300_FW_SHMEM_UART_TX_ADDR));
        poolsize = _iface->peek32(SR_ADDR(X300_FW_SHMEM_BASE, X300_FW_SHMEM_UART_WORDS32));
        //this->write_uart("HELLO UART\n");
        //this->read_uart(0.1);
    }

    void putchar(const char ch)
    {
        txoffset = (txoffset + 1) % (poolsize*4);
        const int shift = ((txoffset%4) * 8);
        if (shift == 0) txword32 = 0;
        txword32 |= boost::uint32_t(ch) << shift;
        _iface->poke32(SR_ADDR(txpool, txoffset/4), txword32);
        _iface->poke32(SR_ADDR(X300_FW_SHMEM_BASE, X300_FW_SHMEM_UART_TX_INDEX), txoffset);
    }

    void write_uart(const std::string &buff)
    {
        BOOST_FOREACH(const char ch, buff)
        {
            if (ch == '\n') this->putchar('\r');
            this->putchar(ch);
        }
    }

    int getchar(void)
    {
        if (_iface->peek32(SR_ADDR(X300_FW_SHMEM_BASE, X300_FW_SHMEM_UART_RX_INDEX)) != rxoffset)
        {
            const int shift = ((rxoffset%4) * 8);
            const char ch = _iface->peek32(SR_ADDR(rxpool, rxoffset/4)) >> shift;
            rxoffset = (rxoffset + 1) % (poolsize*4);
            return ch;
        }
        return -1;
    }

    std::string read_uart(double timeout)
    {
        const boost::system_time exit_time = boost::get_system_time() + boost::posix_time::microseconds(long(timeout*1e6));
        std::string buff;
        while (true)
        {
            const int ch = this->getchar();
            if (ch == -1)
            {
                if (boost::get_system_time() > exit_time) break;
                boost::this_thread::sleep(boost::posix_time::milliseconds(1));
                continue;
            }
            if (ch == '\r') continue;
            buff += std::string(1, (char)ch);
            if (ch == '\n') break;
        }
        //UHD_VAR(buff);
        return buff;
    }

    wb_iface::sptr _iface;
    boost::uint32_t rxoffset, txoffset, txword32, rxpool, txpool, poolsize;
};

uart_iface::sptr x300_make_uart_iface(wb_iface::sptr iface)
{
    return uart_iface::sptr(new x300_uart_iface(iface));
}
