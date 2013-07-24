/*
 * Copyright 2007,2009 Free Software Foundation, Inc.
 * Copyright 2009 Ettus Research LLC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "../b250/b250_defs.h"
#include "ethernet.h"
#include "mdelay.h"
#include "printf.h"
#include "wb_i2c.h"
#include "wb_utils.h"
//#include "memory_map.h"
//#include "eth_phy.h"
//#include "pic.h"
//#include "hal_io.h"
//#include "nonstdio.h"
#include <stdint.h>
#include <stdbool.h>
#include "xge_phy.h"
#include "xge_mac.h"




#define VERBOSE 0

#define	NETHS	2	// # of ethernet interfaces


////////////////////////////////////////////////////////////////////////
//
// 10 Gig Ethernet MAC.
//
typedef struct {
  volatile uint32_t config;             // WO
  volatile uint32_t int_pending;        // Clear-on-read
  volatile uint32_t int_status;         // RO
  volatile uint32_t int_mask;           // RW
  volatile uint32_t mdio_data;
  volatile uint32_t mdio_addr;
  volatile uint32_t mdio_op;
  volatile uint32_t mdio_control;
  volatile uint32_t gpio;
} xge_regs_t;

#define xge_regs ((xge_regs_t *) base)

#define SFPP_STATUS_MODABS_CHG     (1 << 5)    // Has MODABS changed since last read?
#define SFPP_STATUS_TXFAULT_CHG    (1 << 4)    // Has TXFAULT changed sonce last read?
#define SFPP_STATUS_RXLOS_CHG      (1 << 3)    // Has RXLOS changed since last read?
#define SFPP_STATUS_MODABS         (1 << 2)    // MODABS state
#define SFPP_STATUS_TXFAULT        (1 << 1)    // TXFAULT state
#define SFPP_STATUS_RXLOS          (1 << 0)    // RXLOS state


int
ethernet_ninterfaces(void)
{
  return NETHS;
}
  
//
// Clause 45 MDIO used for 10Gig Ethernet has two bus transactions to complete a transfer.
// An initial transaction sets up the address, and a subsequent one transfers the read or write data.
//
static uint32_t 
xge_read_mdio(const uint32_t base, const uint32_t address, const uint32_t device, const uint32_t port)
{
  // Set register address each iteration
  xge_regs->mdio_addr = address;
  // Its a clause 45 device. We want to ADDRESS
  xge_regs->mdio_op = XGE_MDIO_CLAUSE(CLAUSE45) | XGE_MDIO_OP(MDIO_ADDRESS) | XGE_MDIO_ADDR(port) | XGE_MDIO_MMD(device);
  // Start MDIO bus transaction
  xge_regs->mdio_control = 1;
  // Wait until bus transaction complete
  while (xge_regs->mdio_control == 1);
  // Its a clause 45 device. We want to READ
  xge_regs->mdio_op = XGE_MDIO_CLAUSE(CLAUSE45) | XGE_MDIO_OP(MDIO_READ) | XGE_MDIO_ADDR(port) | XGE_MDIO_MMD(device);
  // Start MDIO bus transaction
  xge_regs->mdio_control = 1;
  // Wait until bus transaction complete
  while (xge_regs->mdio_control == 1);
  // Read MDIO data
  return(xge_regs->mdio_data);
}

static void 
xge_write_mdio(const uint32_t base, const uint32_t address, const uint32_t device, const uint32_t port, const uint32_t data)
{  
  // Set register address each iteration
  xge_regs->mdio_addr = address;
  // Its a clause 45 device. We want to ADDRESS
  xge_regs->mdio_op = XGE_MDIO_CLAUSE(CLAUSE45) | XGE_MDIO_OP(MDIO_ADDRESS) | XGE_MDIO_ADDR(port) | XGE_MDIO_MMD(device);
  // Start MDIO bus transaction
  xge_regs->mdio_control = 1;
  // Wait until bus transaction complete
  while (xge_regs->mdio_control == 1);
  // Write new value to mdio_write_data reg. 
  xge_regs->mdio_data = data; 
  // Its a clause 45 device. We want to WRITE 
  xge_regs->mdio_op = XGE_MDIO_CLAUSE(CLAUSE45) | XGE_MDIO_OP(MDIO_WRITE) | XGE_MDIO_ADDR(port) | XGE_MDIO_MMD(device);
  // Start MDIO bus transaction 
  xge_regs->mdio_control = 1; 
  // Wait until bus transaction complete  
  while (xge_regs->mdio_control == 1);
}

//
// Read an 8-bit word from a device attached to the PHY's i2c bus.
//
static int
xge_i2c_rd(const uint32_t base, const uint8_t i2c_dev_addr, const uint8_t i2c_word_addr)
{
  uint8_t buf;
  // IJB. CHECK HERE FOR MODET. Bail immediately if no module

  // SFF-8472 defines a hardcoded bus address of 0xA0, an 8bit internal address and a register map.
  // Write the random access address to the SPF module
  if (wb_i2c_write(base, i2c_dev_addr, &i2c_word_addr, 1) == false)
    return(-1);

  // Now read back a byte of data
  if (wb_i2c_read(base, i2c_dev_addr, &buf, 1) == false)
    return(-1);
  
  return((int) buf);
}

//
// Read identity of SFP+ module for XGE PHY
//
// (base is i2c controller)
static int
xge_read_sfpp_type(const uint32_t base, const uint32_t delay_ms)
{
  int x;
 // Delay read of SFPP
  if (delay_ms)
    mdelay(delay_ms);
  // Read ID code from SFP
  x = xge_i2c_rd(base, MODULE_DEV_ADDR, 3);
  // I2C Error?
  if (x < 0) {
    printf("DEBUG: I2C error in SFPP_TYPE.\n");
    return x;
    }
  // Decode module type. These registers and values are defined in SFF-8472
  if (x & 0x01) // Active 1X Infinband Copper
    {
      goto twinax;
    }
  if (x & 0x10)
    {
      printf("DEBUG: SFFP_TYPE_SR.\n");
      return SFFP_TYPE_SR;
    }
  if (x & 0x20)
    {
      printf("DEBUG: SFFP_TYPE_LR.\n");
      return SFFP_TYPE_LR;
    }
  if (x & 0x40)
    {
      printf("DEBUG: SFFP_TYPE_LRM.\n");
      return SFFP_TYPE_LRM;
    }
  // Search for legacy 1000-Base SFP types
  x = xge_i2c_rd(base, MODULE_DEV_ADDR, 0x6);
  if (x < 0) {
    printf("DEBUG: I2C error in SFPP_TYPE.\n");
    return x;
  }
  if (x & 0x01) {
    printf("DEBUG: SFFP_TYPE_1000BASE_SX.\n");
    return SFFP_TYPE_1000BASE_SX;
  }
  if (x & 0x02) {
    printf("DEBUG: SFFP_TYPE_1000BASE_LX.\n");
    return SFFP_TYPE_1000BASE_LX;
  }
  if (x & 0x08) {
    printf("DEBUG: SFFP_TYPE_1000BASE_T.\n");
    return SFFP_TYPE_1000BASE_T;
  }
  // Not one of the standard optical types..now try to deduce if it's twinax aka 10GSFP+CU
  // which is not covered explicitly in SFF-8472
  x = xge_i2c_rd(base, MODULE_DEV_ADDR, 8);
  if (x < 0) {
    printf("DEBUG: I2C error in SFPP_TYPE.\n");
    return x;
  }
  if ((x & 4) == 0) // Passive SFP+ cable type
    goto unknown;
//  x = xge_i2c_rd(MODULE_DEV_ADDR, 6);
//   printf("SFP+ reg6 read as %x\n",x);
//  if (x < 0)
//    return x;
//  if (x != 0x04)  // Returns  1000Base-CX as Compliance code
//    goto unknown;
  x = xge_i2c_rd(base, MODULE_DEV_ADDR, 0xA);
  if (x < 0) {
    printf("DEBUG: I2C error in SFPP_TYPE.\n");
    return x;
  }
  if (x & 0x80) {
  twinax:
    // Reports 1200 MBytes/sec fibre channel speed..close enough to 10G ethernet!
    x = xge_i2c_rd(base, MODULE_DEV_ADDR, 0x12);

    if (x < 0) {
      printf("DEBUG: I2C error in SFPP_TYPE.\n");
      return x;
    }
    printf("DEBUG: TwinAx.\n");
    // If cable length support is greater than 10M then pick correct type
    return x > 10 ? SFFP_TYPE_TWINAX_LONG : SFFP_TYPE_TWINAX;
  }
unknown:
  printf("DEBUG: Unknown SFP+ type.\n");
  // Not a supported Module type
  return SFFP_TYPE_UNKNOWN;
}


// Pull  reset line low for 100ms then release and wait 100ms
static void
xge_hard_phy_reset(const uint32_t base)
{
  wb_poke32(base, 1);
  mdelay(100);
  wb_poke32(base, 0);
  mdelay(100);

}

static void
xge_mac_init(const uint32_t base)
{
  printf("INFO: Begining XGE MAC init sequence.\n");
  xge_regs->config =  XGE_TX_ENABLE;
}

// base is pointer to XGE MAC on Wishbone.
static void
xge_phy_init(const uint32_t base, const uint32_t mdio_port)
{
  int x;
  // Read LASI Ctrl register to capture state.
  //y = xge_read_mdio(0x9002,XGE_MDIO_DEVICE_PMA,XGE_MDIO_ADDR_PHY_A);
  printf("INFO: Begining XGE PHY init sequence.\n");
  // Software reset
  x = xge_read_mdio(base, 0x0, XGE_MDIO_DEVICE_PMA,mdio_port);
  x = x | (1 << 15); 
  xge_write_mdio(base, 0x0,XGE_MDIO_DEVICE_PMA,mdio_port,x);  
  while(x&(1<<15)) 
    x = xge_read_mdio(base, 0x0,XGE_MDIO_DEVICE_PMA,mdio_port);
}

void
xge_poll_sfpp_status(const uint32_t eth)
{   
  uint32_t x;
  // Has MODDET/MODAbS changed since we last looked?
  x = wb_peek32(SR_ADDR(RB0_BASE, (eth==0) ? RB_SFPP_STATUS0 : RB_SFPP_STATUS1 ));

  if (x & SFPP_STATUS_RXLOS_CHG)
    printf("DEBUG: eth%1d RXLOS changed state: %d\n", eth, x & SFPP_STATUS_RXLOS);
  if (x & SFPP_STATUS_TXFAULT_CHG)
    printf("DEBUG: eth%1d TXFAULT changed state: %d\n", eth,(x & SFPP_STATUS_TXFAULT) >> 1 );
  if (x & SFPP_STATUS_MODABS_CHG)
    printf("DEBUG: eth%1d MODABS changed state: %d\n", eth, (x & SFPP_STATUS_MODABS) >> 2);

  if (x & (SFPP_STATUS_RXLOS_CHG|SFPP_STATUS_TXFAULT_CHG|SFPP_STATUS_MODABS_CHG))
    if (( x & (SFPP_STATUS_RXLOS|SFPP_STATUS_TXFAULT|SFPP_STATUS_MODABS)) == 0) {
      // Only re-init if it's 10GE
      if (wb_peek32(SR_ADDR(RB0_BASE, (eth==0) ? RB_ETH_TYPE0 : RB_ETH_TYPE1)) != 0) {
	xge_ethernet_init(eth);
	dump_mdio_regs((eth==0) ? XGE0_BASE : XGE1_BASE,MDIO_PORT);
	mdelay(100);
	dump_mdio_regs((eth==0) ? XGE0_BASE : XGE1_BASE,MDIO_PORT);
	mdelay(100);
	dump_mdio_regs((eth==0) ? XGE0_BASE : XGE1_BASE,MDIO_PORT);
      }
    }

  if (x & SFPP_STATUS_MODABS_CHG) {
    // MODDET has changed state since last checked
    if (x & SFPP_STATUS_MODABS) {
      // MODDET is high, module currently removed.
      printf("INFO: An SFP+ module has been removed from eth port %d.\n", eth);
    } else {
      // MODDET is low, module currently inserted.
      // Return status.
      printf("INFO: A new SFP+ module has been inserted into eth port %d.\n", eth);
      xge_read_sfpp_type((eth==0) ? I2C0_BASE : I2C2_BASE,1);
    }
  }
}
  

void
xge_ethernet_init(const uint32_t eth)
{ 
  xge_mac_init((eth==0) ? XGE0_BASE : XGE1_BASE); 
 //xge_hard_phy_reset();  
  xge_phy_init((eth==0) ? XGE0_BASE : XGE1_BASE ,MDIO_PORT);    
  uint32_t x = wb_peek32(SR_ADDR(RB0_BASE, (eth==0) ? RB_SFPP_STATUS0 : RB_SFPP_STATUS1 ));
  printf(" eth%1d SFP initial state: RXLOS: %d  TXFAULT: %d  MODABS: %d\n",
	 eth,
	 x & SFPP_STATUS_RXLOS,
	 (x & SFPP_STATUS_TXFAULT) >> 1,
	 (x & SFPP_STATUS_MODABS) >> 2);
}

//
// Debug code to verbosely read XGE MDIO registers below here.
//


void decode_reg(uint32_t address, uint32_t device, uint32_t data)
{
  int x;
  printf("Device: ");
  printf("%x",device);
  printf("  ");
  switch(address) {
  case XGE_MDIO_CONTROL1: 
    printf("CONTROL1: ");
    printf("%x",data); printf("  ");
    for (x=15; x >= 0 ; x--)
      if ((data & (1 << x)) != 0)
	// Bits set.
	switch(x) {
	case 15: printf("Reset,"); break;
	case 14: printf("Loopback,"); break;
	case 11: printf("Low Power Mode,"); break;
	case 5:case 4:case 3:case 2: printf("RESERVED speed value,"); break;
	case 0: printf("PMA loopback,"); break;
	} //else 
	// Bits clear.
	//switch (x) {
	//case 13: case 6: printf(" None 10Gb/s speed set!"); break;
	//}
    printf(" \n");
    break;
  case XGE_MDIO_STATUS1:
    printf("STATUS1: ");
    printf("%x",data); printf("  ");
    for (x=15; x >= 0 ; x--)
      if ((data & (1 << x)) != 0)
	// Bits set.
	switch(x) {
	case 7: printf("Fault Detected,"); break;
	case 2: printf("Link is Up,"); break;
	case 1: printf("Supports Low Power,"); break;
	} else 
	// Bits Clear
	switch(x) {
	case 2: printf("Link is Down,"); break;
	}
    printf(" \n");
    break;
  case XGE_MDIO_SPEED:
    printf("SPEED ABILITY: ");
    printf("%x",data); printf("  ");
    for (x=15; x >= 0 ; x--)
      if ((data & (1 << x)) != 0)
	// Bits set.
	switch(x) {
	case 15:case 14:case 13:case 12:case 11:case 10:case 9:
	case 8:case 7:case 6:case 5:case 4:case 3:case 2:case 1: printf("RESERVED bits set!,"); break;
	case 0: printf("Capable of 10Gb/s,");
	} else 
	// Bits clear.
	switch(x) {
	case 0: printf("Incapable of 10Gb/s,"); break;
	}
    printf(" \n");
    break;
  case XGE_MDIO_DEVICES1:
    printf("DEVICES IN PACKAGE: ");
    printf("%x",data); printf("  ");
    for (x=15; x >= 0 ; x--)
      if ((data & (1 << x)) != 0)
	// Bits set.
	switch(x) {
	case 7: printf("Auto-Negotiation,"); break;
	case 6: printf("TC,"); break;
 	case 5: printf("DTE XS,"); break;
	case 4: printf("PHY XS,"); break;
	case 3: printf("PCS,"); break;
	case 2: printf("WIS,"); break;
	case 1: printf("PMD/PMA,"); break;
	case 0: printf("Clause 22 registers,"); break;
	}
    printf(" \n");
    break;
  case XGE_MDIO_DEVICES2:
    printf("DEVICES IN PACKAGE (cont): ");
    printf("%x",data); printf("  ");
    for (x=15; x >= 0 ; x--)
      if ((data & (1 << x)) != 0)
	// Bits set.
	switch(x) {
	case 15: printf("Vendor device 2,"); break;
	case 14: printf("Vendor device 1,"); break;
	case 13: printf("Clause 22 extension,"); break;
	}
    printf(" \n");
    break;
  case XGE_MDIO_CONTROL2:
    printf("CONTROL2: ");
    printf("%x",data); printf("  ");
    // PMA/PMD
    if (device == XGE_MDIO_DEVICE_PMA)
      switch((data & 0xf)) {
      case 0xF: printf("10BASE-T,"); break;
      case 0xE: printf("100BASE-TX,"); break;
      case 0xD: printf("1000BASE-KX,"); break;
      case 0xC: printf("1000BASE-T,"); break;
      case 0xB: printf("10GBASE-KR,"); break;
      case 0xA: printf("10GBASE-KX4,"); break;
      case 0x9: printf("10GBASE-T,"); break;
      case 0x8: printf("10GBASE-LRM,"); break;
      case 0x7: printf("10GBASE-SR,"); break;
      case 0x6: printf("10GBASE-LR,"); break;
      case 0x5: printf("10GBASE-ER,"); break;
      case 0x4: printf("10GBASE-LX4,"); break;
	//     case 0x3: printf("10GBASE-SW,"); break;
	//      case 0x2: printf("10GBASE-LW,"); break;
	//     case 0x1: printf("10GBASE-EW,"); break;
      case 0x0: printf("10GBASE-CX4,"); break;
      } else if (device == XGE_MDIO_DEVICE_PCS)
      // PCS
      switch((data & 0x3)) {
      case 0x3: printf("10GBASE-T PCS,"); break;
      case 0x2: printf("10GBASE-W PCS,"); break;
      case 0x1: printf("10GBASE-X PCS,"); break;
      case 0x0: printf("10GBASE-R PCS,"); break;
      }
    printf(" \n");
    break;
  case XGE_MDIO_STATUS2:
    printf("STATUS2: ");
    printf("%x",data); printf("  ");
    for (x=15; x >= 0 ; x--)
      if ((data & (1 << x)) != 0)
	// Bits set.
	switch(x) {
	case 15: if ((data & (1 << 14)) == 0) printf("Device responding,"); break;
	case 13: if (device == XGE_MDIO_DEVICE_PMA) printf("Able detect a Tx fault,"); break;
	case 12: if (device == XGE_MDIO_DEVICE_PMA) printf("Able detect an Rx fault,"); break;
	case 11: printf("Fault on Tx path,"); break;
	case 10: printf("Fault on Rx path,"); break;
	case 9:  if (device == XGE_MDIO_DEVICE_PMA) printf("Extended abilities in Reg1.11,"); break;
	case 8:  if (device == XGE_MDIO_DEVICE_PMA) printf("Able to disable TX,"); break;
	case 7:  if (device == XGE_MDIO_DEVICE_PMA) printf("10GBASE-SR,"); break;
	case 6:  if (device == XGE_MDIO_DEVICE_PMA) printf("10GBASE-LR,"); break;
	case 5:  if (device == XGE_MDIO_DEVICE_PMA) printf("10GBASE-ER,"); break;
	case 4:  if (device == XGE_MDIO_DEVICE_PMA) printf("10GBASE-LX4,"); break;
	case 3:  if (device == XGE_MDIO_DEVICE_PMA) printf("10GBASE-SW,"); break;
	case 2:  if (device == XGE_MDIO_DEVICE_PMA) printf("10GBASE-LW,"); break;
	case 1:  if (device == XGE_MDIO_DEVICE_PMA) printf("10GBASE-EW,"); break;
	case 0:  if (device == XGE_MDIO_DEVICE_PMA) printf("loopback,"); break;
	}
    printf(" \n");
    break;
  case XGE_MDIO_LANESTATUS:
    printf("LANE STATUS: ");
    printf("%x",data); printf("  ");
    for (x=15; x >= 0 ; x--)
      if ((data & (1 << x)) != 0)
	// Bits set.
	switch(x) {
	case 12: printf("Lanes aligned,"); break;
	case 11: printf("Able to generate test patterns,"); break;
	case 3:  printf("Lane 3 synced,"); break;
	case 2:  printf("Lane 2 synced,"); break;
	case 1:  printf("Lane 1 synced,"); break;
	case 0:  printf("Lane 0 synced,"); break;
	} else
	// Bits clear
	switch(x) {
 	case 3:  printf("Lane 3 not synced,"); break;
	case 2:  printf("Lane 2 not synced,"); break;
	case 1:  printf("Lane 1 not synced,"); break;
	case 0:  printf("Lane 0 not synced,"); break;
	} 
    printf(" \n");
    break;
  case XILINX_CORE_VERSION:
    printf("XILINX CORE VERSION: %x  ",data);
    printf("Version: %d.%d ",(data&0xf000)>>12,(data&0xf00)>>8);
    printf("Patch: %d ",(data&0xE)>>1);
    if (data&0x1) printf("Evaluation Version of core");
    printf("\n");
    break;
  default:
    printf("Register @ address: ");
    printf("%x",address);
    printf(" has value: ");
    printf("%x\n",data);
    break;
  }
}

void 
dump_mdio_regs(const uint32_t base, uint32_t mdio_port)
{
    volatile unsigned int x;
    int y;
    unsigned int regs_a[9] = {0,1,4,5,6,7,8,32,33};
    unsigned int regs_b[10] = {0,1,4,5,6,7,8,10,11,65535};
   
    printf("\n");

    for (y = 0; y < 10; y++)       
	{
	  // Read MDIO data
	  x = xge_read_mdio(base,regs_b[y],XGE_MDIO_DEVICE_PMA,mdio_port);
	  decode_reg(regs_b[y],XGE_MDIO_DEVICE_PMA,x);
	}
    
      for (y = 0; y < 9; y++)
	{
	  // Read MDIO data
	  x = xge_read_mdio(base,regs_a[y],XGE_MDIO_DEVICE_PCS,mdio_port);
	  decode_reg(regs_a[y],XGE_MDIO_DEVICE_PCS,x);
	}

     printf("\n");

      /* for (y = 0; y < 8; y++) */
      /* 	{ */
      /* 	  // Read MDIO data */
      /* 	  x = xge_read_mdio(base,regs_a[y],XGE_MDIO_DEVICE_PHY_XS,mdio_port); */
      /* 	  decode_reg(regs_a[y],XGE_MDIO_DEVICE_PHY_XS,x); */
      /* 	}  */

      /* for (y = 0; y < 8; y++) */
      /* 	{ */
      /* 	  // Read MDIO data */
      /* 	  x = xge_read_mdio(base,regs_a[y],XGE_MDIO_DEVICE_DTE_XS,mdio_port); */
      /* 	  decode_reg(regs_a[y],XGE_MDIO_DEVICE_DTE_XS,x); */
      /* 	} */
}
