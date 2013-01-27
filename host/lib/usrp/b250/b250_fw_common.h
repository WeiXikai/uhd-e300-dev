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

#ifndef INCLUDED_B250_FW_COMMON_H
#define INCLUDED_B250_FW_COMMON_H

#include <stdint.h>

/*!
 * Structs and constants for b250 communication.
 * This header is shared by the firmware and host code.
 * Therefore, this header may only contain valid C code.
 */
#ifdef __cplusplus
extern "C" {
#endif

#define B250_FW_NUM_BYTES (1 << 14) //16k
#define B250_FW_COMMS_MTU (1 << 13) //8k
#define B250_FW_COMMS_UDP_PORT 49152

#define B250_FW_COMMS_FLAGS_ACK        (1 << 0)
#define B250_FW_COMMS_FLAGS_ERROR      (1 << 1)
#define B250_FW_COMMS_FLAGS_POKE32     (1 << 2)
#define B250_FW_COMMS_FLAGS_PEEK32     (1 << 3)

typedef struct
{
    uint32_t flags;
    uint32_t sequence;
    uint32_t addr;
    uint32_t data;
} b250_fw_comms_t;

#ifdef __cplusplus
}
#endif

#endif /* INCLUDED_B250_FW_COMMON_H */
