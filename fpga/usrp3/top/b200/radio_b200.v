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

// radio top level module for b200
//  Contains all clock-rate DSP components, all radio and hardware controls and settings

module radio_b200
  #(
    parameter RADIO_FIFO_SIZE = 13,
    parameter SAMPLE_FIFO_SIZE = 11
  )
  (input radio_clk, input radio_rst,
   input [31:0] rx, output [31:0] tx,
   inout [31:0] fe_atr, input pps,

   input bus_clk, input bus_rst,
   input [63:0] tx_tdata, input tx_tlast, input tx_tvalid, output tx_tready,
   output [63:0] rx_tdata, output rx_tlast, output rx_tvalid, input rx_tready,
   input [63:0] ctrl_tdata, input ctrl_tlast, input ctrl_tvalid, output ctrl_tready,
   output [63:0] resp_tdata, output resp_tlast, output resp_tvalid, input resp_tready,

   output [63:0] debug
   );
   

   // ///////////////////////////////////////////////////////////////////////////////
   // FIFO Interfacing to the bus clk domain
   // in_tdata splits to tx_tdata and ctrl_tdata
   // rx_tdata and resp_tdata get muxed to out_tdata
   // Everything except rx flow control must cross in to radio_clk domain before further use
   // _b signifies bus_clk domain, _r signifies radio_clk domain
  
   wire [63:0] 	 ctrl_tdata_r;
   wire 	 ctrl_tready_r, ctrl_tvalid_r;
   wire 	 ctrl_tlast_r;
   
   wire [63:0] 	 resp_tdata_r;
   wire 	 resp_tready_r, resp_tvalid_r;
   wire 	 resp_tlast_r;
   
   wire [63:0] 	 rx_tdata_r;
   wire 	 rx_tready_r, rx_tvalid_r;
   wire 	 rx_tlast_r;

   wire [63:0] 	 rx_mux_tdata_r;
   wire 	 rx_mux_tready_r, rx_mux_tvalid_r;
   wire 	 rx_mux_tlast_r;

   wire [63:0] 	 rx_err_tdata_r;
   wire 	 rx_err_tready_r, rx_err_tvalid_r;
   wire 	 rx_err_tlast_r;

   wire [63:0] 	 tx_tdata_r;
   wire 	 tx_tready_r, tx_tvalid_r;
   wire 	 tx_tlast_r;

   wire [63:0] 	 txresp_tdata, txresp_tdata_r;
   wire 	 txresp_tready, txresp_tready_r, txresp_tvalid, txresp_tvalid_r;
   wire 	 txresp_tlast, txresp_tlast_r;

   wire [63:0] 	 rmux_tdata_r;
   wire 	 rmux_tlast_r, rmux_tvalid_r, rmux_tready_r;

   wire [63:0] 	 rx_tdata_int;
   wire 	 rx_tready_int, rx_tvalid_int;
   wire 	 rx_tlast_int;

   axi_fifo_2clk #(.WIDTH(65), .SIZE(0/*minimal*/)) ctrl_fifo
     (.reset(bus_rst),
      .i_aclk(bus_clk), .i_tvalid(ctrl_tvalid), .i_tready(ctrl_tready), .i_tdata({ctrl_tlast, ctrl_tdata}), 
      .o_aclk(radio_clk), .o_tvalid(ctrl_tvalid_r), .o_tready(ctrl_tready_r), .o_tdata({ctrl_tlast_r, ctrl_tdata_r}));
   
   axi_fifo_2clk #(.WIDTH(65), .SIZE(RADIO_FIFO_SIZE)) tx_fifo
     (.reset(bus_rst),
      .i_aclk(bus_clk), .i_tvalid(tx_tvalid), .i_tready(tx_tready), .i_tdata({tx_tlast, tx_tdata}), 
      .o_aclk(radio_clk), .o_tvalid(tx_tvalid_r), .o_tready(tx_tready_r), .o_tdata({tx_tlast_r, tx_tdata_r}));
   
   axi_fifo_2clk #(.WIDTH(65), .SIZE(0/*minimal*/)) resp_fifo
     (.reset(radio_rst),
      .i_aclk(radio_clk), .i_tvalid(rmux_tvalid_r), .i_tready(rmux_tready_r), .i_tdata({rmux_tlast_r, rmux_tdata_r}), 
      .o_aclk(bus_clk), .o_tvalid(resp_tvalid), .o_tready(resp_tready), .o_tdata({resp_tlast, resp_tdata}));
   
   axi_fifo_2clk #(.WIDTH(65), .SIZE(0)) rx_fifo
     (.reset(radio_rst),
      .i_aclk(radio_clk), .i_tvalid(rx_mux_tvalid_r), .i_tready(rx_mux_tready_r), .i_tdata({rx_mux_tlast_r, rx_mux_tdata_r}),
      .o_aclk(bus_clk), .o_tvalid(rx_tvalid_int), .o_tready(rx_tready_int), .o_tdata({rx_tlast_int, rx_tdata_int}));
   
   axi_packet_gate #(.WIDTH(64), .SIZE(RADIO_FIFO_SIZE)) buffer_whole_pkt
     (
      .clk(bus_clk), .reset(bus_rst), .clear(1'b0),
      .i_tdata(rx_tdata_int), .i_tlast(rx_tlast_int), .i_terror(1'b0), .i_tvalid(rx_tvalid_int), .i_tready(rx_tready_int),
      .o_tdata(rx_tdata), .o_tlast(rx_tlast), .o_tvalid(rx_tvalid), .o_tready(rx_tready)
      );
   
   // /////////////////////////////////////////////////////////////////////////////////////
   // Setting bus and controls
   
   localparam SR_SPI       = 8'd8;
   localparam SR_ATR       = 8'd12;
   localparam SR_TEST      = 8'd21;
   localparam SR_CODEC_IDLE = 8'd22;
   localparam SR_READBACK  = 8'd32;
   localparam SR_TX_CTRL   = 8'd64;
   localparam SR_RX_CTRL   = 8'd96;
   localparam SR_RX_DSP    = 8'd144;
   localparam SR_TX_DSP    = 8'd184;
   localparam SR_TIME      = 8'd128;
   localparam SR_RX_FMT    = 8'd136;
   localparam SR_TX_FMT    = 8'd138;


   wire 	set_stb;
   wire [7:0] 	set_addr;
   wire [31:0] 	set_data;
   wire [31:0] 	test_readback;
   wire 	run_rx, run_tx;

   reg [63:0] 	rb_data;
   wire [1:0] 	rb_addr;

   wire [63:0] vita_time, vita_time_lastpps;
   timekeeper #(.BASE(SR_TIME)) timekeeper
     (.clk(radio_clk), .reset(radio_rst), .pps(pps),
      .set_stb(set_stb), .set_addr(set_addr), .set_data(set_data),
      .vita_time(vita_time), .vita_time_lastpps(vita_time_lastpps));

   radio_ctrl_proc radio_ctrl_proc
     (.clk(radio_clk), .reset(radio_rst), .clear(1'b0),
      .ctrl_tdata(ctrl_tdata_r), .ctrl_tlast(ctrl_tlast_r), .ctrl_tvalid(ctrl_tvalid_r), .ctrl_tready(ctrl_tready_r),
      .resp_tdata(resp_tdata_r), .resp_tlast(resp_tlast_r), .resp_tvalid(resp_tvalid_r), .resp_tready(resp_tready_r),
      .vita_time(vita_time),
      .set_stb(set_stb), .set_addr(set_addr), .set_data(set_data),
      .ready(1'b1), .readback(rb_data),
      .debug());

   always @*
     case(rb_addr)
       2'd0 : rb_data <= { 32'b0, test_readback };
       2'd1 : rb_data <= vita_time;
       2'd2 : rb_data <= vita_time_lastpps;
       2'd3 : rb_data <= {tx, rx};
       default : rb_data <= 64'd0;
     endcase // case (rb_addr)

   setting_reg #(.my_addr(SR_TEST), .awidth(8), .width(32)) sr_test
     (.clk(radio_clk), .rst(radio_rst), .strobe(set_stb), .addr(set_addr), .in(set_data),
      .out(test_readback), .changed());

   wire [31:0] tx_idle;
   setting_reg #(.my_addr(SR_CODEC_IDLE), .awidth(8), .width(32)) sr_codec_idle
     (.clk(radio_clk), .rst(radio_rst), .strobe(set_stb), .addr(set_addr), .in(set_data),
      .out(tx_idle), .changed());

   setting_reg #(.my_addr(SR_READBACK), .awidth(8), .width(2)) sr_rdback
     (.clk(radio_clk), .rst(radio_rst), .strobe(set_stb), .addr(set_addr), .in(set_data),
      .out(rb_addr), .changed());

   gpio_atr #(.BASE(SR_ATR), .WIDTH(32)) gpio_atr
     (.clk(radio_clk),.reset(radio_rst),
      .set_stb(set_stb),.set_addr(set_addr),.set_data(set_data),
      .rx(run_rx), .tx(run_tx),
      .gpio(fe_atr), .gpio_readback() );

   // /////////////////////////////////////////////////////////////////////////////////
   //  TX Chain

   wire [175:0] txsample_tdata;
   wire 	txsample_tvalid, txsample_tready;
   wire [31:0] 	sample_tx;
   wire 	ack_or_error, packet_consumed;
   wire [11:0] 	seqnum;
   wire [63:0] 	error_code;
   wire [31:0] 	sid;
   wire [23:0] tx_fe_i, tx_fe_q;

   wire [31:0] debug_tx_control;
   
   assign tx[31:16] = (run_tx)? tx_fe_i[23:8] : tx_idle[31:16];
   assign tx[15:0]  = (run_tx)? tx_fe_q[23:8] : tx_idle[15:0];

   wire [63:0] tx_tdata_i; wire tx_tlast_i, tx_tvalid_i, tx_tready_i;

   new_tx_deframer tx_deframer
     (.clk(radio_clk), .reset(radio_rst), .clear(1'b0),
      .i_tdata(tx_tdata_i), .i_tlast(tx_tlast_i), .i_tvalid(tx_tvalid_i), .i_tready(tx_tready_i),
      .sample_tdata(txsample_tdata), .sample_tvalid(txsample_tvalid), .sample_tready(txsample_tready));

   new_tx_control #(.BASE(SR_TX_CTRL)) tx_control
     (.clk(radio_clk), .reset(radio_rst), .clear(1'b0),
      .set_stb(set_stb), .set_addr(set_addr), .set_data(set_data),
      .vita_time(vita_time),
      .ack_or_error(ack_or_error), .packet_consumed(packet_consumed),
      .seqnum(seqnum), .error_code(error_code), .sid(sid),
      .sample_tdata(txsample_tdata), .sample_tvalid(txsample_tvalid), .sample_tready(txsample_tready),
      .sample(sample_tx), .run(run_tx), .strobe(strobe_tx),
      .debug(debug_tx_control));

   tx_responder #(.BASE(SR_TX_CTRL+2)) tx_responder
     (.clk(radio_clk), .reset(radio_rst), .clear(1'b0),
      .set_stb(set_stb), .set_addr(set_addr), .set_data(set_data),
      .ack_or_error(ack_or_error), .packet_consumed(packet_consumed),
      .seqnum(seqnum), .error_code(error_code), .sid(sid),
      .vita_time(vita_time),
      .o_tdata(txresp_tdata_r), .o_tlast(txresp_tlast_r), .o_tvalid(txresp_tvalid_r), .o_tready(txresp_tready_r));

   duc_chain #(.BASE(SR_TX_DSP), .DSPNO(0), .WIDTH(24)) duc_chain
     (.clk(radio_clk), .rst(radio_rst), .clr(1'b0),
      .set_stb(set_stb),.set_addr(set_addr),.set_data(set_data),
      .tx_fe_i(tx_fe_i),.tx_fe_q(tx_fe_q),
      .sample(sample_tx), .run(run_tx), .strobe(strobe_tx),
      .debug() );

    chdr_xxxx_to_16sc_chain #(.BASE(SR_TX_FMT)) convert_xxxx_to_16sc
     (.clk(radio_clk), .reset(radio_rst),
      .set_stb(set_stb),.set_addr(set_addr),.set_data(set_data),
      .i_tdata(tx_tdata_r), .i_tlast(tx_tlast_r), .i_tvalid(tx_tvalid_r), .i_tready(tx_tready_r),
      .o_tdata(tx_tdata_i), .o_tlast(tx_tlast_i), .o_tvalid(tx_tvalid_i), .o_tready(tx_tready_i));

   // /////////////////////////////////////////////////////////////////////////////////
   //  RX Chain

   wire 	full, eob_rx;
   wire 	strobe_rx;
   wire [31:0] 	sample_rx;
   wire [31:0] 	  rx_sid;
   wire [11:0] 	  rx_seqnum;
   wire [63:0] rx_tdata_i; wire rx_tlast_i, rx_tvalid_i, rx_tready_i;
   
   new_rx_framer #(.BASE(SR_RX_CTRL+4),.SAMPLE_FIFO_SIZE(SAMPLE_FIFO_SIZE)) new_rx_framer
     (.clk(radio_clk), .reset(radio_rst), .clear(1'b0),
      .set_stb(set_stb), .set_addr(set_addr), .set_data(set_data),
      .vita_time(vita_time),
      .strobe(strobe_rx), .sample(sample_rx), .run(run_rx), .eob(eob_rx), .full(full),
      .sid(rx_sid), .seqnum(rx_seqnum),
      .o_tdata(rx_tdata_i), .o_tlast(rx_tlast_i), .o_tvalid(rx_tvalid_i), .o_tready(rx_tready_i),
      .debug());

   new_rx_control #(.BASE(SR_RX_CTRL)) new_rx_control
     (.clk(radio_clk), .reset(radio_rst), .clear(1'b0),
      .set_stb(set_stb), .set_addr(set_addr), .set_data(set_data),
      .vita_time(vita_time),
      .strobe(strobe_rx), .run(run_rx), .eob(eob_rx), .full(full),
      .sid(rx_sid), .seqnum(rx_seqnum),
      .err_tdata(rx_err_tdata_r), .err_tlast(rx_err_tlast_r), .err_tvalid(rx_err_tvalid_r), .err_tready(rx_err_tready_r),
      .debug());

   ddc_chain #(.BASE(SR_RX_DSP), .DSPNO(0), .WIDTH(24)) ddc_chain
     (.clk(radio_clk), .rst(radio_rst), .clr(1'b0),
      .set_stb(set_stb),.set_addr(set_addr),.set_data(set_data),
      .rx_fe_i({rx[31:16],8'd0}),.rx_fe_q({rx[15:0],8'd0}),
      .sample(sample_rx), .run(run_rx), .strobe(strobe_rx),
      .debug() );

   chdr_16sc_to_xxxx_chain #(.BASE(SR_RX_FMT)) convert_16sc_to_xxxx
     (.clk(radio_clk), .reset(radio_rst),
      .set_stb(set_stb),.set_addr(set_addr),.set_data(set_data),
      .i_tdata(rx_tdata_i), .i_tlast(rx_tlast_i), .i_tvalid(rx_tvalid_i), .i_tready(rx_tready_i),
      .o_tdata(rx_tdata_r), .o_tlast(rx_tlast_r), .o_tvalid(rx_tvalid_r), .o_tready(rx_tready_r));

   // /////////////////////////////////////////////////////////////////////////////////
   //  RX Channel Muxing

   axi_mux4 #(.PRIO(1), .WIDTH(64)) rx_mux
     (.clk(radio_clk), .reset(radio_rst), .clear(1'b0),
      .i0_tdata(rx_tdata_r), .i0_tlast(rx_tlast_r), .i0_tvalid(rx_tvalid_r), .i0_tready(rx_tready_r),
      .i1_tdata(rx_err_tdata_r), .i1_tlast(rx_err_tlast_r), .i1_tvalid(rx_err_tvalid_r), .i1_tready(rx_err_tready_r),
      .i2_tdata(), .i2_tlast(), .i2_tvalid(1'b0), .i2_tready(),
      .i3_tdata(), .i3_tlast(), .i3_tvalid(1'b0), .i3_tready(),
      .o_tdata(rx_mux_tdata_r), .o_tlast(rx_mux_tlast_r), .o_tvalid(rx_mux_tvalid_r), .o_tready(rx_mux_tready_r));

   // /////////////////////////////////////////////////////////////////////////////////
   //  Response Channel Muxing

   axi_mux4 #(.PRIO(0), .WIDTH(64)) response_mux
     (.clk(radio_clk), .reset(radio_rst), .clear(1'b0),
      .i0_tdata(txresp_tdata_r), .i0_tlast(txresp_tlast_r), .i0_tvalid(txresp_tvalid_r), .i0_tready(txresp_tready_r),
      .i1_tdata(resp_tdata_r), .i1_tlast(resp_tlast_r), .i1_tvalid(resp_tvalid_r), .i1_tready(resp_tready_r),
      .i2_tdata(), .i2_tlast(), .i2_tvalid(1'b0), .i2_tready(),
      .i3_tdata(), .i3_tlast(), .i3_tvalid(1'b0), .i3_tready(),
      .o_tdata(rmux_tdata_r), .o_tlast(rmux_tlast_r), .o_tvalid(rmux_tvalid_r), .o_tready(rmux_tready_r));

   // DEBUG LOGIC - NOT FOR PRODUCTION

   
   assign debug = 0;

   
   
endmodule // radio_b200
