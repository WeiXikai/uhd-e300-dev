//
// Copyright 2013-2014 Ettus Research
//

module e300
#(parameter STREAMS_WIDTH = 3,
  parameter CMDFIFO_DEPTH = 10,
  parameter CONFIG_BASE = 32'h4000_0000,
  parameter PAGE_WIDTH = 10)
(
  // ARM Connections
  inout [53:0]  MIO,
  input         PS_SRSTB,
  input         PS_CLK,
  input         PS_PORB,
  inout         DDR_Clk,
  inout         DDR_Clk_n,
  inout         DDR_CKE,
  inout         DDR_CS_n,
  inout         DDR_RAS_n,
  inout         DDR_CAS_n,
  output        DDR_WEB_pin,
  inout [2:0]   DDR_BankAddr,
  inout [14:0]  DDR_Addr,
  inout         DDR_ODT,
  inout         DDR_DRSTB,
  inout [31:0]  DDR_DQ,
  inout [3:0]   DDR_DM,
  inout [3:0]   DDR_DQS,
  inout [3:0]   DDR_DQS_n,
  inout         DDR_VRP,
  inout         DDR_VRN,

  // Control connections for FPGA
  //input wire SYSCLK_P;
  //input wire SYSCLK_N;
  //input wire PS_SRST_B;

  //AVR SPI IO
  output        AVR_CS_R,
  input         AVR_IRQ,
  input         AVR_MISO_R,
  output        AVR_MOSI_R,
  output        AVR_SCK_R,

  input         ONSWITCH_DB,

  // RF Board connections
  // Change to inout/output as
  // they are implemented/tested
  input [34:0]  DB_EXP_1_8V,

  //band selects
  output [2:0]  TX_BANDSEL,
  output [2:0]  RX1_BANDSEL,
  output [2:0]  RX2_BANDSEL,
  output [1:0]  RX2C_BANDSEL,
  output [1:0]  RX1B_BANDSEL,
  output [1:0]  RX1C_BANDSEL,
  output [1:0]  RX2B_BANDSEL,

  //enables
  output        TX_ENABLE1A,
  output        TX_ENABLE2A,
  output        TX_ENABLE1B,
  output        TX_ENABLE2B,

  //antenna selects
  output        VCTXRX1_V1,
  output        VCTXRX1_V2,
  output        VCTXRX2_V1,
  output        VCTXRX2_V2,
  output        VCRX1_V1,
  output        VCRX1_V2,
  output        VCRX2_V1,
  output        VCRX2_V2,

  // leds
  output        LED_TXRX1_TX,
  output        LED_TXRX1_RX,
  output        LED_RX1_RX,
  output        LED_TXRX2_TX,
  output        LED_TXRX2_RX,
  output        LED_RX2_RX,

  // ad9361 connections
  input [7:0]   CAT_CTRL_OUT,
  output [3:0]  CAT_CTRL_IN,
  output        CAT_RESET,  // Really CAT_RESET_B, active low
  output        CAT_CS,
  output        CAT_SCLK,
  output        CAT_MOSI,
  input         CAT_MISO,
  input         CAT_BBCLK_OUT, //unused
  output        CAT_SYNC,
  output        CAT_TXNRX,
  output        CAT_ENABLE,
  output        CAT_ENAGC,
  input         CAT_RX_FRAME,
  input         CAT_DATA_CLK,
  output        CAT_TX_FRAME,
  output        CAT_FB_CLK,
  input [11:0]  CAT_P0_D,
  output [11:0] CAT_P1_D,

  // pps connections
  input         GPS_PPS,
  input         PPS_EXT_IN,

  // gpios, change to inout somehow
  inout [5:0]   PL_GPIO
);

  // Internal connections to PS
  //   GP0 -- General Purpose port 0, FPGA is the slave
  wire [31:0] GP0_M_AXI_AWADDR_pin;
  wire        GP0_M_AXI_AWVALID_pin;
  wire        GP0_M_AXI_AWREADY_pin;
  wire [31:0] GP0_M_AXI_WDATA_pin;
  wire [3:0]  GP0_M_AXI_WSTRB_pin;
  wire        GP0_M_AXI_WVALID_pin;
  wire        GP0_M_AXI_WREADY_pin;
  wire [1:0]  GP0_M_AXI_BRESP_pin;
  wire        GP0_M_AXI_BVALID_pin;
  wire        GP0_M_AXI_BREADY_pin;
  wire [31:0] GP0_M_AXI_ARADDR_pin;
  wire        GP0_M_AXI_ARVALID_pin;
  wire        GP0_M_AXI_ARREADY_pin;
  wire [31:0] GP0_M_AXI_RDATA_pin;
  wire [1:0]  GP0_M_AXI_RRESP_pin;
  wire        GP0_M_AXI_RVALID_pin;
  wire        GP0_M_AXI_RREADY_pin;
  //   HP0 -- High Performance port 0, FPGA is the master
  wire [31:0] HP0_S_AXI_AWADDR_pin;
  wire [2:0]  HP0_S_AXI_AWPROT_pin;
  wire        HP0_S_AXI_AWVALID_pin;
  wire        HP0_S_AXI_AWREADY_pin;
  wire [63:0] HP0_S_AXI_WDATA_pin;
  wire [7:0]  HP0_S_AXI_WSTRB_pin;
  wire        HP0_S_AXI_WVALID_pin;
  wire        HP0_S_AXI_WREADY_pin;
  wire [1:0]  HP0_S_AXI_BRESP_pin;
  wire        HP0_S_AXI_BVALID_pin;
  wire        HP0_S_AXI_BREADY_pin;
  wire [31:0] HP0_S_AXI_ARADDR_pin;
  wire [2:0]  HP0_S_AXI_ARPROT_pin;
  wire        HP0_S_AXI_ARVALID_pin;
  wire        HP0_S_AXI_ARREADY_pin;
  wire [63:0] HP0_S_AXI_RDATA_pin;
  wire [1:0]  HP0_S_AXI_RRESP_pin;
  wire        HP0_S_AXI_RVALID_pin;
  wire        HP0_S_AXI_RREADY_pin;
  wire [3:0]  HP0_S_AXI_ARCACHE_pin;
  wire [7:0]  HP0_S_AXI_AWLEN_pin;
  wire [2:0]  HP0_S_AXI_AWSIZE_pin;
  wire [1:0]  HP0_S_AXI_AWBURST_pin;
  wire [3:0]  HP0_S_AXI_AWCACHE_pin;
  wire        HP0_S_AXI_WLAST_pin;
  wire [7:0]  HP0_S_AXI_ARLEN_pin;
  wire [1:0]  HP0_S_AXI_ARBURST_pin;
  wire [2:0]  HP0_S_AXI_ARSIZE_pin;

  wire        bus_clk_pin;

  wire        bus_clk, radio_clk;
  wire        bus_rst, radio_rst;
  wire        sys_arst, sys_arst_n;

  wire [31:0] ps_gpio_out;
  wire [31:0] ps_gpio_in;

  wire        stream_irq;

  wire [63:0] h2s_tdata;
  wire        h2s_tvalid;
  wire        h2s_tready;
  wire        h2s_tlast;

  wire [63:0] s2h_tdata;
  wire        s2h_tvalid;
  wire        s2h_tready;
  wire        s2h_tlast;

   // First, make all connections to the PS (ARM+buses)
  e300_ps e300_ps_instance
  (  // Outward connections to the pins
    .processing_system7_0_MIO(MIO),
    .processing_system7_0_PS_SRSTB_pin(PS_SRSTB),
    .processing_system7_0_PS_CLK_pin(PS_CLK),
    .processing_system7_0_PS_PORB_pin(PS_PORB),
    .processing_system7_0_DDR_Clk(DDR_Clk),
    .processing_system7_0_DDR_Clk_n(DDR_Clk_n),
    .processing_system7_0_DDR_CKE(DDR_CKE),
    .processing_system7_0_DDR_CS_n(DDR_CS_n),
    .processing_system7_0_DDR_RAS_n(DDR_RAS_n),
    .processing_system7_0_DDR_CAS_n(DDR_CAS_n),
    .processing_system7_0_DDR_WEB_pin(DDR_WEB_pin),
    .processing_system7_0_DDR_BankAddr(DDR_BankAddr),
    .processing_system7_0_DDR_Addr(DDR_Addr),
    .processing_system7_0_DDR_ODT(DDR_ODT),
    .processing_system7_0_DDR_DRSTB(DDR_DRSTB),
    .processing_system7_0_DDR_DQ(DDR_DQ),
    .processing_system7_0_DDR_DM(DDR_DM),
    .processing_system7_0_DDR_DQS(DDR_DQS),
    .processing_system7_0_DDR_DQS_n(DDR_DQS_n),
    .processing_system7_0_DDR_VRN(DDR_VRN),
    .processing_system7_0_DDR_VRP(DDR_VRP),

    // Inward connections to our logic
    //    GP0  --  General Purpose Slave 0
    .axi_ext_slave_conn_0_M_AXI_AWADDR_pin(GP0_M_AXI_AWADDR_pin),
    .axi_ext_slave_conn_0_M_AXI_AWVALID_pin(GP0_M_AXI_AWVALID_pin),
    .axi_ext_slave_conn_0_M_AXI_AWREADY_pin(GP0_M_AXI_AWREADY_pin),
    .axi_ext_slave_conn_0_M_AXI_WDATA_pin(GP0_M_AXI_WDATA_pin),
    .axi_ext_slave_conn_0_M_AXI_WSTRB_pin(GP0_M_AXI_WSTRB_pin),
    .axi_ext_slave_conn_0_M_AXI_WVALID_pin(GP0_M_AXI_WVALID_pin),
    .axi_ext_slave_conn_0_M_AXI_WREADY_pin(GP0_M_AXI_WREADY_pin),
    .axi_ext_slave_conn_0_M_AXI_BRESP_pin(GP0_M_AXI_BRESP_pin),
    .axi_ext_slave_conn_0_M_AXI_BVALID_pin(GP0_M_AXI_BVALID_pin),
    .axi_ext_slave_conn_0_M_AXI_BREADY_pin(GP0_M_AXI_BREADY_pin),
    .axi_ext_slave_conn_0_M_AXI_ARADDR_pin(GP0_M_AXI_ARADDR_pin),
    .axi_ext_slave_conn_0_M_AXI_ARVALID_pin(GP0_M_AXI_ARVALID_pin),
    .axi_ext_slave_conn_0_M_AXI_ARREADY_pin(GP0_M_AXI_ARREADY_pin),
    .axi_ext_slave_conn_0_M_AXI_RDATA_pin(GP0_M_AXI_RDATA_pin),
    .axi_ext_slave_conn_0_M_AXI_RRESP_pin(GP0_M_AXI_RRESP_pin),
    .axi_ext_slave_conn_0_M_AXI_RVALID_pin(GP0_M_AXI_RVALID_pin),
    .axi_ext_slave_conn_0_M_AXI_RREADY_pin(GP0_M_AXI_RREADY_pin),

    //    Misc interrupts, GPIO, clk
    .processing_system7_0_IRQ_F2P_pin({15'h0, stream_irq}),
    .processing_system7_0_GPIO_I_pin(ps_gpio_in),
    .processing_system7_0_GPIO_O_pin(ps_gpio_out),
    .processing_system7_0_FCLK_CLK0_pin(bus_clk_pin),
    .processing_system7_0_FCLK_RESET0_N_pin(sys_arst_n),

    //    HP0  --  High Performance Master 0
    .axi_ext_master_conn_0_S_AXI_AWADDR_pin(HP0_S_AXI_AWADDR_pin),
    .axi_ext_master_conn_0_S_AXI_AWPROT_pin(HP0_S_AXI_AWPROT_pin),
    .axi_ext_master_conn_0_S_AXI_AWVALID_pin(HP0_S_AXI_AWVALID_pin),
    .axi_ext_master_conn_0_S_AXI_AWREADY_pin(HP0_S_AXI_AWREADY_pin),
    .axi_ext_master_conn_0_S_AXI_WDATA_pin(HP0_S_AXI_WDATA_pin),
    .axi_ext_master_conn_0_S_AXI_WSTRB_pin(HP0_S_AXI_WSTRB_pin),
    .axi_ext_master_conn_0_S_AXI_WVALID_pin(HP0_S_AXI_WVALID_pin),
    .axi_ext_master_conn_0_S_AXI_WREADY_pin(HP0_S_AXI_WREADY_pin),
    .axi_ext_master_conn_0_S_AXI_BRESP_pin(HP0_S_AXI_BRESP_pin),
    .axi_ext_master_conn_0_S_AXI_BVALID_pin(HP0_S_AXI_BVALID_pin),
    .axi_ext_master_conn_0_S_AXI_BREADY_pin(HP0_S_AXI_BREADY_pin),
    .axi_ext_master_conn_0_S_AXI_ARADDR_pin(HP0_S_AXI_ARADDR_pin),
    .axi_ext_master_conn_0_S_AXI_ARPROT_pin(HP0_S_AXI_ARPROT_pin),
    .axi_ext_master_conn_0_S_AXI_ARVALID_pin(HP0_S_AXI_ARVALID_pin),
    .axi_ext_master_conn_0_S_AXI_ARREADY_pin(HP0_S_AXI_ARREADY_pin),
    .axi_ext_master_conn_0_S_AXI_RDATA_pin(HP0_S_AXI_RDATA_pin),
    .axi_ext_master_conn_0_S_AXI_RRESP_pin(HP0_S_AXI_RRESP_pin),
    .axi_ext_master_conn_0_S_AXI_RVALID_pin(HP0_S_AXI_RVALID_pin),
    .axi_ext_master_conn_0_S_AXI_RREADY_pin(HP0_S_AXI_RREADY_pin),
    .axi_ext_master_conn_0_S_AXI_AWLEN_pin(HP0_S_AXI_AWLEN_pin),
    .axi_ext_master_conn_0_S_AXI_RLAST_pin(HP0_S_AXI_RLAST_pin),
    .axi_ext_master_conn_0_S_AXI_ARCACHE_pin(HP0_S_AXI_ARCACHE_pin),
    .axi_ext_master_conn_0_S_AXI_AWSIZE_pin(HP0_S_AXI_AWSIZE_pin),
    .axi_ext_master_conn_0_S_AXI_AWBURST_pin(HP0_S_AXI_AWBURST_pin),
    .axi_ext_master_conn_0_S_AXI_AWCACHE_pin(HP0_S_AXI_AWCACHE_pin),
    .axi_ext_master_conn_0_S_AXI_WLAST_pin(HP0_S_AXI_WLAST_pin),
    .axi_ext_master_conn_0_S_AXI_ARLEN_pin(HP0_S_AXI_ARLEN_pin),
    .axi_ext_master_conn_0_S_AXI_ARBURST_pin(HP0_S_AXI_ARBURST_pin),
    .axi_ext_master_conn_0_S_AXI_ARSIZE_pin(HP0_S_AXI_ARSIZE_pin),

    //    SPI Core 0 - To AD9361
    .processing_system7_0_SPI0_SS_O_pin(),
    .processing_system7_0_SPI0_SS1_O_pin(CAT_CS),
    .processing_system7_0_SPI0_SS2_O_pin(),
    .processing_system7_0_SPI0_SCLK_O_pin(CAT_SCLK),
    .processing_system7_0_SPI0_MOSI_O_pin(CAT_MOSI),
    .processing_system7_0_SPI0_MISO_I_pin(CAT_MISO),

    //    SPI Core 1 - To AVR
    .processing_system7_0_SPI1_SS_O_pin(),
    .processing_system7_0_SPI1_SS1_O_pin(AVR_CS_R),
    .processing_system7_0_SPI1_SS2_O_pin(),
    .processing_system7_0_SPI1_SCLK_O_pin(AVR_SCK_R),
    .processing_system7_0_SPI1_MOSI_O_pin(AVR_MOSI_R),
    .processing_system7_0_SPI1_MISO_I_pin(AVR_MISO_R)
  );

  //------------------------------------------------------------------
  //-- generate clock and reset signals
  //------------------------------------------------------------------

  assign sys_arst = ~sys_arst_n;

  reset_sync radio_rst_sync
  (
    .clk(radio_clk),
    .reset_in(sys_arst),
    .reset_out(radio_rst)
  );

  BUFG core_clk_gen
  (
    .I(bus_clk_pin),
    .O(bus_clk)
  );

  reset_sync core_rst_gen
  (
    .clk(bus_clk),
    .reset_in(sys_arst),
    .reset_out(bus_rst)
  );

  //------------------------------------------------------------------
  // CODEC capture/gen
  //------------------------------------------------------------------
  wire        rx_clk, rx_strobe, tx_clk, tx_strobe;
  wire [11:0] rx_i0, rx_q0, rx_i1, rx_q1;
  wire [11:0] tx_i0, tx_q0, tx_i1, tx_q1;
  wire        mimo_rx, mimo_tx;
  wire [7:0]  sen;
  assign radio_clk = rx_clk;

  wire        codec_data_clk;
  BUFG codec_data_clk_bufg
  (
    .I(CAT_DATA_CLK),
    .O(codec_data_clk)
  );

  // CMOS Data interface to catalina, ignore _n pins
  catcap_ddr_cmos catcap
  (
    .data_clk(codec_data_clk),
    .reset(radio_rst),
    .mimo(mimo_rx),
    .rx_frame(CAT_RX_FRAME),
    .rx_d(CAT_P0_D),
    .rx_clk(rx_clk),
    .rx_strobe(rx_strobe),
    .i0(rx_i0), .q0(rx_q0),
    .i1(rx_i1), .q1(rx_q1)
  );

  assign tx_clk = rx_clk;

  catgen_ddr_cmos catgen
  (
    .data_clk(CAT_FB_CLK),
    .reset(radio_rst),
    .mimo(mimo_tx),
    .tx_frame(CAT_TX_FRAME),
    .tx_d(CAT_P1_D),
    .tx_clk(tx_clk),
    .tx_strobe(tx_strobe),
    .i0(tx_i0), .q0(tx_q0),
    .i1(tx_i1), .q1(tx_q1)
  );

  assign CAT_CTRL_IN = 4'b1;
  assign CAT_ENAGC = 1'b1;
  assign CAT_TXNRX = 1'b1;
  assign CAT_ENABLE = 1'b1;

  assign CAT_RESET = ~(sys_arst || (CAT_CS & CAT_MOSI));   // Operates active-low, really CAT_RESET_B
  assign CAT_SYNC = 1'b0;

  //------------------------------------------------------------------
  //-- connect misc stuff to user GPIO
  //------------------------------------------------------------------
  assign ps_gpio_in[5] = AVR_IRQ;       //59
  assign ps_gpio_in[7] = ONSWITCH_DB;   //61

  //------------------------------------------------------------------
  //-- radio core from x300 for super fast bring up
  //------------------------------------------------------------------
  wire [31:0] 	rx_data0, rx_data1;
  wire 	rx_enb0, rx_enb1;
  assign rx_data0 = {rx_i0, 4'b0, rx_q0, 4'b0};
  assign rx_data1 = {rx_i1, 4'b0, rx_q1, 4'b0};

  wire [31:0] 	tx_data0, tx_data1;
  wire 	tx_enb0, tx_enb1;
  assign {tx_i0, tx_q0} = {tx_data0[31:20], tx_data0[15:4]};
  assign {tx_i1, tx_q1} = {tx_data1[31:20], tx_data1[15:4]};
  //assign DATA[255:224] = tx_data0;

  assign mimo_rx = rx_enb0 && rx_enb1;
  assign mimo_tx = tx_enb0 && tx_enb1;

  wire [2:0] TX1_BANDSEL;
  wire [2:0] TX2_BANDSEL; //unused now
  assign TX_BANDSEL = TX1_BANDSEL;  // FIXME -- should this be TX1_BANDSEL | TX2_BANDSEL ?

  wire [31:0] 	gpio0, gpio1;

  assign { rx_enb0, tx_enb0, //2
           LED_TXRX1_TX, LED_TXRX1_RX, LED_RX1_RX, //3
           VCRX1_V2, VCRX1_V1, VCTXRX1_V2, VCTXRX1_V1, //4
           TX_ENABLE1B, TX_ENABLE1A, //2
           RX1C_BANDSEL, RX1B_BANDSEL, RX1_BANDSEL, TX1_BANDSEL //10
         } = gpio0[20:0];

  assign { rx_enb1, tx_enb1, //2
           LED_TXRX2_TX, LED_TXRX2_RX, LED_RX2_RX, //3
           VCRX2_V2, VCRX2_V1, VCTXRX2_V2, VCTXRX2_V1, //4
           TX_ENABLE2B, TX_ENABLE2A, //2
           RX2C_BANDSEL, RX2B_BANDSEL, RX2_BANDSEL, TX2_BANDSEL //10
         } = gpio1[20:0];

  //------------------------------------------------------------------
  //-- Zynq system interface, DMA, control channels, etc.
  //------------------------------------------------------------------

  wire [31:0] core_set_data, core_rb_data;
  wire [31:0] core_set_addr;
  wire        core_stb;

  zynq_fifo_top
  #(
    .CONFIG_BASE(CONFIG_BASE),
    .PAGE_WIDTH(PAGE_WIDTH),
    .H2S_STREAMS_WIDTH(STREAMS_WIDTH),
    .H2S_CMDFIFO_DEPTH(CMDFIFO_DEPTH),
    .S2H_STREAMS_WIDTH(STREAMS_WIDTH),
    .S2H_CMDFIFO_DEPTH(CMDFIFO_DEPTH)
  )
  zynq_fifo_top0
  (
    .clk(bus_clk), .rst(bus_rst),
    .CTL_AXI_AWADDR(GP0_M_AXI_AWADDR_pin),
    .CTL_AXI_AWVALID(GP0_M_AXI_AWVALID_pin),
    .CTL_AXI_AWREADY(GP0_M_AXI_AWREADY_pin),
    .CTL_AXI_WDATA(GP0_M_AXI_WDATA_pin),
    .CTL_AXI_WSTRB(GP0_M_AXI_WSTRB_pin),
    .CTL_AXI_WVALID(GP0_M_AXI_WVALID_pin),
    .CTL_AXI_WREADY(GP0_M_AXI_WREADY_pin),
    .CTL_AXI_BRESP(GP0_M_AXI_BRESP_pin),
    .CTL_AXI_BVALID(GP0_M_AXI_BVALID_pin),
    .CTL_AXI_BREADY(GP0_M_AXI_BREADY_pin),
    .CTL_AXI_ARADDR(GP0_M_AXI_ARADDR_pin),
    .CTL_AXI_ARVALID(GP0_M_AXI_ARVALID_pin),
    .CTL_AXI_ARREADY(GP0_M_AXI_ARREADY_pin),
    .CTL_AXI_RDATA(GP0_M_AXI_RDATA_pin),
    .CTL_AXI_RRESP(GP0_M_AXI_RRESP_pin),
    .CTL_AXI_RVALID(GP0_M_AXI_RVALID_pin),
    .CTL_AXI_RREADY(GP0_M_AXI_RREADY_pin),

    .DDR_AXI_AWADDR(HP0_S_AXI_AWADDR_pin),
    .DDR_AXI_AWPROT(HP0_S_AXI_AWPROT_pin),
    .DDR_AXI_AWVALID(HP0_S_AXI_AWVALID_pin),
    .DDR_AXI_AWREADY(HP0_S_AXI_AWREADY_pin),
    .DDR_AXI_WDATA(HP0_S_AXI_WDATA_pin),
    .DDR_AXI_WSTRB(HP0_S_AXI_WSTRB_pin),
    .DDR_AXI_WVALID(HP0_S_AXI_WVALID_pin),
    .DDR_AXI_WREADY(HP0_S_AXI_WREADY_pin),
    .DDR_AXI_BRESP(HP0_S_AXI_BRESP_pin),
    .DDR_AXI_BVALID(HP0_S_AXI_BVALID_pin),
    .DDR_AXI_BREADY(HP0_S_AXI_BREADY_pin),
    .DDR_AXI_ARADDR(HP0_S_AXI_ARADDR_pin),
    .DDR_AXI_ARPROT(HP0_S_AXI_ARPROT_pin),
    .DDR_AXI_ARVALID(HP0_S_AXI_ARVALID_pin),
    .DDR_AXI_ARREADY(HP0_S_AXI_ARREADY_pin),
    .DDR_AXI_RDATA(HP0_S_AXI_RDATA_pin),
    .DDR_AXI_RRESP(HP0_S_AXI_RRESP_pin),
    .DDR_AXI_RVALID(HP0_S_AXI_RVALID_pin),
    .DDR_AXI_RREADY(HP0_S_AXI_RREADY_pin),
    .DDR_AXI_AWLEN(HP0_S_AXI_AWLEN_pin),
    .DDR_AXI_RLAST(HP0_S_AXI_RLAST_pin),
    .DDR_AXI_ARCACHE(HP0_S_AXI_ARCACHE_pin),
    .DDR_AXI_AWSIZE(HP0_S_AXI_AWSIZE_pin),
    .DDR_AXI_AWBURST(HP0_S_AXI_AWBURST_pin),
    .DDR_AXI_AWCACHE(HP0_S_AXI_AWCACHE_pin),
    .DDR_AXI_WLAST(HP0_S_AXI_WLAST_pin),
    .DDR_AXI_ARLEN(HP0_S_AXI_ARLEN_pin),
    .DDR_AXI_ARBURST(HP0_S_AXI_ARBURST_pin),
    .DDR_AXI_ARSIZE(HP0_S_AXI_ARSIZE_pin),

    .h2s_tdata(h2s_tdata),
    .h2s_tlast(h2s_tlast),
    .h2s_tvalid(h2s_tvalid),
    .h2s_tready(h2s_tready),

    .s2h_tdata(s2h_tdata),
    .s2h_tlast(s2h_tlast),
    .s2h_tvalid(s2h_tvalid),
    .s2h_tready(s2h_tready),

    .core_set_data(core_set_data),
    .core_set_addr(core_set_addr),
    .core_set_stb(core_set_stb),
    .core_rb_data(core_rb_data),

    .event_irq(stream_irq)
  );

  // Generate an internal PPS signal with a 25% duty cycle
  // Note: This is based on a 50Mhz clk assumption
  reg [31:0] pps_count;
  wire int_pps = (pps_count < 32'd12500000);
  always @(posedge bus_clk)
    if (pps_count >= 32'd49999999)
      pps_count <= 32'b0;
    else
      pps_count <= pps_count + 1'b1;

  // pps mux - selects internal, external, or gpsdo PPS
  reg pps;
  wire [1:0] pps_select;
  always @(*) begin
    case(pps_select)
      2'b00  :   pps = GPS_PPS;
      2'b01  :   pps = 1'b0;
      2'b10  :   pps = int_pps;
      2'b11  :   pps = PPS_EXT_IN;
      default:   pps = 1'b0;
    endcase
  end

  // E300 Core logic

  e300_core e300_core0
  (
    .bus_clk(bus_clk),
    .bus_rst(bus_rst),

    .h2s_tdata(h2s_tdata),
    .h2s_tlast(h2s_tlast),
    .h2s_tvalid(h2s_tvalid),
    .h2s_tready(h2s_tready),

    .s2h_tdata(s2h_tdata),
    .s2h_tlast(s2h_tlast),
    .s2h_tvalid(s2h_tvalid),
    .s2h_tready(s2h_tready),

    .radio_clk(radio_clk),
    .radio_rst(radio_rst),
    .rx_data0(rx_data0),
    .tx_data0(tx_data0),
    .rx_data1(rx_data1),
    .tx_data1(tx_data1),

    .ctrl_out0(gpio0),
    .ctrl_out1(gpio1),

    .fp_gpio(PL_GPIO),

    .set_data(core_set_data),
    .set_addr(core_set_addr),
    .set_stb(core_set_stb),
    .rb_data(core_rb_data),

    .pps_select(pps_select),
    .pps(pps),

    .debug()
  );

  //------------------------------------------------------------------
  //-- chipscope debugs
  //------------------------------------------------------------------
  /*
  wire [35:0] CONTROL;
  wire [255:0] DATA;
  wire [7:0] TRIG;

  chipscope_icon chipscope_icon(.CONTROL0(CONTROL));
  chipscope_ila chipscope_ila
  (
      .CONTROL(CONTROL), .CLK(bus_clk),
      .DATA(DATA), .TRIG0(TRIG)
  );

  assign DATA[63:0] = h2s_tdata;
  assign TRIG = {
      h2s_tlast, h2s_tvalid, h2s_tready, 1'b0,
      4'b0
  };
  assign DATA[64] = h2s_tlast;
  assign DATA[65] = h2s_tvalid;
  assign DATA[66] = h2s_tready;
  */

endmodule // e300
