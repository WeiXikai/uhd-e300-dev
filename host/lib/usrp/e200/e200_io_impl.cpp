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

#include "e200_regs.hpp"
#include "e200_impl.hpp"
#include "validate_subdev_spec.hpp"
#include "../../transport/super_recv_packet_handler.hpp"
#include "../../transport/super_send_packet_handler.hpp"
#include "async_packet_handler.hpp"
#include <uhd/transport/bounded_buffer.hpp>
#include <boost/bind.hpp>
#include <uhd/utils/tasks.hpp>
#include <boost/foreach.hpp>
#include <boost/make_shared.hpp>

#define E200_TX_FC_PKT_WINDOW 120 //TODO better constants dude

using namespace uhd;
using namespace uhd::usrp;
using namespace uhd::transport;

/***********************************************************************
 * update streamer rates
 **********************************************************************/
void e200_impl::update_tick_rate(const double rate)
{
    BOOST_FOREACH(radio_perifs_t &perif, _radio_perifs)
    {
        boost::shared_ptr<sph::recv_packet_streamer> my_streamer =
            boost::dynamic_pointer_cast<sph::recv_packet_streamer>(perif.rx_streamer.lock());
        if (my_streamer) my_streamer->set_tick_rate(rate);
        perif.framer->set_tick_rate(_tick_rate);
    }
    BOOST_FOREACH(radio_perifs_t &perif, _radio_perifs)
    {
        boost::shared_ptr<sph::send_packet_streamer> my_streamer =
            boost::dynamic_pointer_cast<sph::send_packet_streamer>(perif.tx_streamer.lock());
        if (my_streamer) my_streamer->set_tick_rate(rate);
        perif.deframer->set_tick_rate(_tick_rate);
    }
}

void e200_impl::update_rx_samp_rate(const size_t dspno, const double rate)
{
    boost::shared_ptr<sph::recv_packet_streamer> my_streamer =
        boost::dynamic_pointer_cast<sph::recv_packet_streamer>(_radio_perifs[dspno].rx_streamer.lock());
    if (my_streamer) my_streamer->set_samp_rate(rate);
}

void e200_impl::update_tx_samp_rate(const size_t dspno, const double rate)
{
    boost::shared_ptr<sph::send_packet_streamer> my_streamer =
        boost::dynamic_pointer_cast<sph::send_packet_streamer>(_radio_perifs[dspno].tx_streamer.lock());
    if (my_streamer) my_streamer->set_samp_rate(rate);
}

/***********************************************************************
 * frontend selection
 **********************************************************************/
void e200_impl::update_rx_subdev_spec(const uhd::usrp::subdev_spec_t &spec)
{
    //sanity checking
    if (spec.size()) validate_subdev_spec(_tree, spec, "rx");
    UHD_ASSERT_THROW(spec.size() <= 2);

    _fe_control_settings[0].rx_enb = false;
    _fe_control_settings[1].rx_enb = false;

    if (spec.size() == 1)
    {
        UHD_ASSERT_THROW(spec[0].db_name == "A");
        _fe_control_settings[0].rx_enb = spec[0].sd_name == "RX1";
        _fe_control_settings[1].rx_enb = spec[0].sd_name == "RX2";
    }
    if (spec.size() == 2)
    {
        //TODO we can support swapping at a later date, only this combo is supported
        UHD_ASSERT_THROW(spec[0].db_name == "A");
        UHD_ASSERT_THROW(spec[0].sd_name == "RX1");
        UHD_ASSERT_THROW(spec[1].db_name == "A");
        UHD_ASSERT_THROW(spec[1].sd_name == "RX2");
        _fe_control_settings[0].rx_enb = true;
        _fe_control_settings[1].rx_enb = true;
    }

    this->update_active_frontends();
}

void e200_impl::update_tx_subdev_spec(const uhd::usrp::subdev_spec_t &spec)
{
    //sanity checking
    if (spec.size()) validate_subdev_spec(_tree, spec, "tx");
    UHD_ASSERT_THROW(spec.size() <= 2);

    _fe_control_settings[0].tx_enb = false;
    _fe_control_settings[1].tx_enb = false;

    if (spec.size() == 1)
    {
        UHD_ASSERT_THROW(spec[0].db_name == "A");
        _fe_control_settings[0].tx_enb = spec[0].sd_name == "TX1";
        _fe_control_settings[1].tx_enb = spec[0].sd_name == "TX2";
    }
    if (spec.size() == 2)
    {
        //TODO we can support swapping at a later date, only this combo is supported
        UHD_ASSERT_THROW(spec[0].db_name == "A");
        UHD_ASSERT_THROW(spec[0].sd_name == "TX1");
        UHD_ASSERT_THROW(spec[1].db_name == "A");
        UHD_ASSERT_THROW(spec[1].sd_name == "TX2");
        _fe_control_settings[0].tx_enb = true;
        _fe_control_settings[1].tx_enb = true;
    }

    this->update_active_frontends();
}

/***********************************************************************
 * VITA stuff
 **********************************************************************/
static void e200_if_hdr_unpack_le(
    const boost::uint32_t *packet_buff,
    vrt::if_packet_info_t &if_packet_info
){
    if_packet_info.link_type = vrt::if_packet_info_t::LINK_TYPE_CHDR;
    return vrt::if_hdr_unpack_le(packet_buff, if_packet_info);
}

static void e200_if_hdr_pack_le(
    boost::uint32_t *packet_buff,
    vrt::if_packet_info_t &if_packet_info
){
    if_packet_info.link_type = vrt::if_packet_info_t::LINK_TYPE_CHDR;
    return vrt::if_hdr_pack_le(packet_buff, if_packet_info);
}

/***********************************************************************
 * RX flow control handler
 **********************************************************************/
static void handle_rx_flowctrl(const boost::uint32_t sid, zero_copy_if::sptr xport, const size_t last_seq)
{
    managed_send_buffer::sptr buff = xport->get_send_buff(1.0);
    if (not buff)
    {
        throw uhd::runtime_error("handle_rx_flowctrl timed out getting a send buffer");
    }
    boost::uint32_t *pkt = buff->cast<boost::uint32_t *>();

    //load packet info
    vrt::if_packet_info_t packet_info;
    packet_info.packet_type = vrt::if_packet_info_t::PACKET_TYPE_EXTENSION;
    packet_info.num_payload_words32 = 2;
    packet_info.num_payload_bytes = packet_info.num_payload_words32*sizeof(boost::uint32_t);
    packet_info.packet_count = 0;
    packet_info.sob = false;
    packet_info.eob = false;
    packet_info.sid = sid;
    packet_info.has_sid = true;
    packet_info.has_cid = false;
    packet_info.has_tsi = false;
    packet_info.has_tsf = false;
    packet_info.has_tlr = false;

    //load header
    e200_if_hdr_pack_le(pkt, packet_info);

    //load payload
    pkt[packet_info.num_header_words32+0] = uhd::htowx<boost::uint32_t>(~0); //off
    pkt[packet_info.num_header_words32+1] = uhd::htowx<boost::uint32_t>(last_seq + 16/*TODO*/);

    //send the buffer over the interface
    buff->commit(sizeof(boost::uint32_t)*(packet_info.num_packet_words32));
}


/***********************************************************************
 * TX flow control handler
 **********************************************************************/
struct tx_fc_guts_t
{
    tx_fc_guts_t(void):
        last_seq_out(0),
        last_seq_ack(0),
        seq_queue(1),
        async_queue(1000){}
    size_t last_seq_out;
    size_t last_seq_ack;
    bounded_buffer<size_t> seq_queue;
    bounded_buffer<async_metadata_t> async_queue;
};

static void handle_tx_async_msgs(boost::shared_ptr<tx_fc_guts_t> guts, zero_copy_if::sptr xport)
{
    managed_recv_buffer::sptr buff = xport->get_recv_buff();
    if (not buff) return;

    //extract packet info
    vrt::if_packet_info_t if_packet_info;
    if_packet_info.num_packet_words32 = buff->size()/sizeof(boost::uint32_t);
    const boost::uint32_t *packet_buff = buff->cast<const boost::uint32_t *>();

    //unpacking can fail
    try
    {
        e200_if_hdr_unpack_le(packet_buff, if_packet_info);
    }
    catch(const std::exception &ex)
    {
        UHD_MSG(error) << "Error parsing async message packet: " << ex.what() << std::endl;
        return;
    }

    //catch the flow control packets and react
    if (uhd::wtohx(packet_buff[if_packet_info.num_header_words32+0]) == 0)
    {
        const size_t seq = uhd::wtohx(packet_buff[if_packet_info.num_header_words32+1]);
        guts->seq_queue.push_with_haste(seq);
        return;
    }

    //fill in the async metadata
    async_metadata_t metadata;
    load_metadata_from_buff(uhd::wtohx<boost::uint32_t>, metadata, if_packet_info, packet_buff, 61.44e6/*FIXME set from rate update*/);
    guts->async_queue.push_with_pop_on_full(metadata);
    standard_async_msg_prints(metadata);
}

static managed_send_buffer::sptr get_tx_buff_with_flowctrl(
    task::sptr /*holds ref*/,
    boost::shared_ptr<tx_fc_guts_t> guts,
    zero_copy_if::sptr xport,
    const bool enable_fc,
    const double timeout
){

    if (not enable_fc) return xport->get_send_buff(timeout); //NO FC so this is OK

    while (true)
    {
        const size_t delta = (guts->last_seq_out & 0xfff) - (guts->last_seq_ack & 0xfff);
        if ((delta & 0xfff) <= E200_TX_FC_PKT_WINDOW) break;

        const bool ok = guts->seq_queue.pop_with_timed_wait(guts->last_seq_ack, timeout);
        if (not ok) return managed_send_buffer::sptr(); //timeout waiting for flow control
    }

    managed_send_buffer::sptr buff = xport->get_send_buff(timeout);
    if (buff) guts->last_seq_out++; //update seq, this will actually be a send
    return buff;
}

/***********************************************************************
 * Async Data
 **********************************************************************/
bool e200_impl::recv_async_msg(
    async_metadata_t &async_metadata, double timeout
){
    boost::shared_ptr<sph::send_packet_streamer> my_streamer =
        boost::dynamic_pointer_cast<sph::send_packet_streamer>(_radio_perifs[0].tx_streamer.lock());
    if (my_streamer) return my_streamer->recv_async_msg(async_metadata, timeout);
    return false;
}

/***********************************************************************
 * Receive streamer
 **********************************************************************/
rx_streamer::sptr e200_impl::get_rx_stream(const uhd::stream_args_t &args_)
{
    stream_args_t args = args_;

    //setup defaults for unspecified values
    if (not args.otw_format.empty() and args.otw_format != "sc16")
    {
        throw uhd::value_error("e200_impl::get_rx_stream only supports otw_format sc16");
    }
    args.otw_format = "sc16";
    args.channels = args.channels.empty()? std::vector<size_t>(1, 0) : args.channels;
    const boost::uint32_t data_sid = 2 | (2 << 16);

    const size_t i = args.channels[0];
    radio_perifs_t &perif = _radio_perifs[i];

    //calculate packet size
    static const size_t hdr_size = 0
        + vrt::num_vrl_words32*sizeof(boost::uint32_t)
        + vrt::max_if_hdr_words32*sizeof(boost::uint32_t)
        + sizeof(vrt::if_packet_info_t().tlr) //forced to have trailer
        - sizeof(vrt::if_packet_info_t().cid) //no class id ever used
        - sizeof(vrt::if_packet_info_t().tsi) //no int time ever used
    ;
    const size_t bpp = perif.rx_data_xport->get_recv_frame_size() - hdr_size;
    const size_t bpi = convert::get_bytes_per_item(args.otw_format);
    const size_t spp = unsigned(args.args.cast<double>("spp", bpp/bpi));

    //make the new streamer given the samples per packet
    boost::shared_ptr<sph::recv_packet_streamer> my_streamer = boost::make_shared<sph::recv_packet_streamer>(spp);

    //init some streamer stuff
    my_streamer->set_vrt_unpacker(&e200_if_hdr_unpack_le);

    //set the converter
    uhd::convert::id_type id;
    id.input_format = args.otw_format + "_item32_le";
    id.num_inputs = 1;
    id.output_format = args.cpu_format;
    id.num_outputs = 1;
    my_streamer->set_converter(id);

    perif.framer->set_nsamps_per_packet(spp); //seems to be a good place to set this
    perif.framer->set_sid((data_sid << 16) | (data_sid >> 16));
    perif.framer->setup(args);
    my_streamer->set_xport_chan_get_buff(0, boost::bind(
        &zero_copy_if::get_recv_buff, perif.rx_data_xport, _1
    ), true /*flush*/);
    my_streamer->set_overflow_handler(0, boost::bind(
        &rx_vita_core_3000::handle_overflow, perif.framer
    ));
    //my_streamer->set_xport_handle_flowctrl(0, boost::bind(
    //    &handle_rx_flowctrl, data_sid, _rx_flow_xport, _1
    //), _rx_data_xport->get_num_recv_frames()/8, true/*init*/);
    handle_rx_flowctrl(data_sid, perif.rx_flow_xport, 0); //init off

    my_streamer->set_issue_stream_cmd(0, boost::bind(
        &rx_vita_core_3000::issue_stream_command, perif.framer, _1
    ));
    perif.rx_streamer = my_streamer; //store weak pointer

    //sets all tick and samp rates on this streamer
    this->update_tick_rate(this->get_tick_rate());
    _tree->access<double>(str(boost::format("/mboards/0/rx_dsps/%u/rate/value") % 0)).update();

    return my_streamer;
}

/***********************************************************************
 * Transmit streamer
 **********************************************************************/
tx_streamer::sptr e200_impl::get_tx_stream(const uhd::stream_args_t &args_)
{
    stream_args_t args = args_;

    //setup defaults for unspecified values
    if (not args.otw_format.empty() and args.otw_format != "sc16")
    {
        throw uhd::value_error("e200_impl::get_rx_stream only supports otw_format sc16");
    }
    args.otw_format = "sc16";
    args.channels = args.channels.empty()? std::vector<size_t>(1, 0) : args.channels;
    const boost::uint32_t data_sid = 0 | (0 << 16);

    const size_t i = args.channels[0];
    radio_perifs_t &perif = _radio_perifs[i];

    //calculate packet size
    static const size_t hdr_size = 0
        + vrt::num_vrl_words32*sizeof(boost::uint32_t)
        + vrt::max_if_hdr_words32*sizeof(boost::uint32_t)
        //+ sizeof(vrt::if_packet_info_t().tlr) //forced to have trailer
        - sizeof(vrt::if_packet_info_t().cid) //no class id ever used
        - sizeof(vrt::if_packet_info_t().tsi) //no int time ever used
    ;
    const size_t bpp = perif.tx_data_xport->get_send_frame_size() - hdr_size;
    const size_t bpi = convert::get_bytes_per_item(args.otw_format);
    const size_t spp = unsigned(args.args.cast<double>("spp", bpp/bpi));

    //make the new streamer given the samples per packet
    boost::shared_ptr<sph::send_packet_streamer> my_streamer = boost::make_shared<sph::send_packet_streamer>(spp);

    //init some streamer stuff
    my_streamer->set_vrt_packer(&e200_if_hdr_pack_le);

    //set the converter
    uhd::convert::id_type id;
    id.input_format = args.cpu_format;
    id.num_inputs = 1;
    id.output_format = args.otw_format + "_item32_le";
    id.num_outputs = 1;
    my_streamer->set_converter(id);

    perif.deframer->setup(args);

    //flow control setup
    if (_network_mode) perif.deframer->configure_flow_control(0/*cycs off*/, E200_TX_FC_PKT_WINDOW/8/*pkts*/);
    boost::shared_ptr<tx_fc_guts_t> guts(new tx_fc_guts_t());
    task::sptr task = task::make(boost::bind(&handle_tx_async_msgs, guts, perif.tx_flow_xport));

    my_streamer->set_xport_chan_get_buff(0, boost::bind(
        &get_tx_buff_with_flowctrl, task, guts, perif.tx_data_xport, _network_mode/*fc on*/, _1
    ));
    my_streamer->set_async_receiver(boost::bind(
        &bounded_buffer<async_metadata_t>::pop_with_timed_wait, &(guts->async_queue), _1, _2
    ));
    my_streamer->set_xport_chan_sid(0, true, data_sid);
    my_streamer->set_enable_trailer(false); //TODO not implemented trailer support yet
    perif.tx_streamer = my_streamer; //store weak pointer

    //sets all tick and samp rates on this streamer
    this->update_tick_rate(this->get_tick_rate());
    _tree->access<double>(str(boost::format("/mboards/0/tx_dsps/%u/rate/value") % 0)).update();

    return my_streamer;
}
