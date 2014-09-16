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

#ifndef INCLUDED_LIBUHD_RFNOC_WINDOW_BLOCK_CTRL_HPP
#define INCLUDED_LIBUHD_RFNOC_WINDOW_BLOCK_CTRL_HPP

#include <uhd/usrp/rfnoc/rx_block_ctrl_base.hpp>
#include <uhd/usrp/rfnoc/tx_block_ctrl_base.hpp>

namespace uhd {
    namespace rfnoc {

/*! \brief Block controller for the standard windowing RFNoC block.
 *
 * The standard windowing block has the following features:
 * - One input- and output-port
 * - Configurable window length and coefficients
 * - Supports data type sc16 (16-Bit fix-point complex samples)
 *
 * This block requires packets to be the same size as the downstream FFT length.
 * It will perform one window operation per incoming packet, treating it
 * as a vector of samples.
 */
class UHD_API window_block_ctrl : public rx_block_ctrl_base, public tx_block_ctrl_base
{
public:
    UHD_RFNOC_BLOCK_OBJECT(window_block_ctrl)

    //! Configure the window coefficients
    //
    // \p coeffs size determines the window length. If it longer than
    // the maximum window length, throws a uhd::value_error.
    virtual void set_window(const std::vector<int> &coeffs) = 0;

    //! Returns the maximum window length.
    virtual size_t get_max_len() const = 0;

    //! Returns the current window length.
    virtual size_t get_window_len() const = 0;

    //! In this block, the input signature may not be changed.
    //
    // Will return false if \p stream_sig does not match the current stream sig.
    virtual bool set_input_signature(const stream_sig_t &stream_sig, size_t port=0) = 0;

    //! In this block, the output signature may not be changed.
    //
    // Will return false if \p stream_sig does not match the current stream sig.
    virtual bool set_output_signature(const stream_sig_t &stream_sig, size_t port=0) = 0;

protected:
    //! Checks the args \p window_len and \p spp are OK.
    //
    //  If spp is given, it must match the window length, or we throw.
    virtual void _set_args() = 0;

    //! Check stream args match window length
    virtual void _init_rx(uhd::stream_args_t &args) = 0;

    //! Check stream args match window length
    virtual void _init_tx(uhd::stream_args_t &args) = 0;
}; /* class window_block_ctrl*/

}} /* namespace uhd::rfnoc */

#endif /* INCLUDED_LIBUHD_RFNOC_WINDOW_BLOCK_CTRL_HPP */
