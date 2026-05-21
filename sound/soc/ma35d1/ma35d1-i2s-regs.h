/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2026 Emac Inc.
 */

#ifndef _MA35D1_I2S_REGS_H
#define _MA35D1_I2S_REGS_H

#include <linux/bits.h>

/*
 * MA35D1 I2S register offsets
 */
#define MA35D1_I2S_CTL0			0x00
#define MA35D1_I2S_CLKDIV		0x04
#define MA35D1_I2S_IEN			0x08
#define MA35D1_I2S_STATUS0		0x0c
#define MA35D1_I2S_TXFIFO		0x10
#define MA35D1_I2S_RXFIFO		0x14
#define MA35D1_I2S_CTL1			0x20
#define MA35D1_I2S_STATUS1		0x24

/*
 * MA35D1_I2S_CTL0 - Control register 0
 *
 * This register controls the main I2S/TDM format, data width, channel
 * width, master/slave mode, FIFO clear bits, DMA request enables, MCLK
 * output enable, and stream enable bits.
 */
#define MA35D1_I2S_CTL0_I2S_EN		BIT(0)
#define MA35D1_I2S_CTL0_TX_EN		BIT(1)
#define MA35D1_I2S_CTL0_RX_EN		BIT(2)
#define MA35D1_I2S_CTL0_MUTE		BIT(3)

#define MA35D1_I2S_CTL0_DATWIDTH	GENMASK(5, 4)
#define MA35D1_I2S_CTL0_DATWIDTH_8	0x0
#define MA35D1_I2S_CTL0_DATWIDTH_16	0x1
#define MA35D1_I2S_CTL0_DATWIDTH_24	0x2
#define MA35D1_I2S_CTL0_DATWIDTH_32	0x3

#define MA35D1_I2S_CTL0_MONO		BIT(6)
#define MA35D1_I2S_CTL0_ORDER		BIT(7)
#define MA35D1_I2S_CTL0_SLAVE		BIT(8)
#define MA35D1_I2S_CTL0_MCLKEN		BIT(15)
#define MA35D1_I2S_CTL0_TXFBCLR		BIT(18)
#define MA35D1_I2S_CTL0_RXFBCLR		BIT(19)
#define MA35D1_I2S_CTL0_TXPDMAEN	BIT(20)
#define MA35D1_I2S_CTL0_RXPDMAEN	BIT(21)
#define MA35D1_I2S_CTL0_RXLCH		BIT(23)

#define MA35D1_I2S_CTL0_FORMAT		GENMASK(26, 24)
#define MA35D1_I2S_CTL0_PCMSYNC		BIT(27)

#define MA35D1_I2S_CTL0_CHWIDTH		GENMASK(29, 28)
#define MA35D1_I2S_CTL0_CHWIDTH_8	0x0
#define MA35D1_I2S_CTL0_CHWIDTH_16	0x1
#define MA35D1_I2S_CTL0_CHWIDTH_24	0x2
#define MA35D1_I2S_CTL0_CHWIDTH_32	0x3

#define MA35D1_I2S_CTL0_TDMCHNUM	GENMASK(31, 30)

/*
 * MA35D1_I2S_CLKDIV - Clock divider register
 *
 * BCLKDIV controls the bit clock divider.
 * MCLKDIV controls the master clock divider.
 *
 * The old vendor driver appeared to write these fields reversed:
 *
 *     (bclkdiv << 8) | mclkdiv
 *
 * Based on the masks below, BCLKDIV is bits [6:0] and MCLKDIV is bits [17:8].
 * The implementation should use FIELD_PREP() with these masks instead of
 * open-coded shifts.
 */
#define MA35D1_I2S_CLKDIV_BCLKDIV	GENMASK(6, 0)
#define MA35D1_I2S_CLKDIV_MCLKDIV	GENMASK(17, 8)

/*
 * MA35D1_I2S_IEN - Interrupt enable register
 *
 * Enables FIFO threshold, overflow, underflow, and channel zero-crossing
 * interrupts.
 */
#define MA35D1_I2S_IEN_RXUDFIEN		BIT(0)
#define MA35D1_I2S_IEN_RXOVFIEN		BIT(1)
#define MA35D1_I2S_IEN_RXTHIEN		BIT(2)

#define MA35D1_I2S_IEN_TXUDFIEN		BIT(8)
#define MA35D1_I2S_IEN_TXOVFIEN		BIT(9)
#define MA35D1_I2S_IEN_TXTHIEN		BIT(10)

#define MA35D1_I2S_IEN_CH0ZCIEN		BIT(16)
#define MA35D1_I2S_IEN_CH1ZCIEN		BIT(17)
#define MA35D1_I2S_IEN_CH2ZCIEN		BIT(18)
#define MA35D1_I2S_IEN_CH3ZCIEN		BIT(19)
#define MA35D1_I2S_IEN_CH4ZCIEN		BIT(20)
#define MA35D1_I2S_IEN_CH5ZCIEN		BIT(21)
#define MA35D1_I2S_IEN_CH6ZCIEN		BIT(22)
#define MA35D1_I2S_IEN_CH7ZCIEN		BIT(23)

/*
 * MA35D1_I2S_STATUS0 - Status register 0
 *
 * Reports FIFO status, interrupt status, and current data channel.
 */
#define MA35D1_I2S_STATUS0_I2SINT	BIT(0)
#define MA35D1_I2S_STATUS0_I2SRXINT	BIT(1)
#define MA35D1_I2S_STATUS0_I2STXINT	BIT(2)

#define MA35D1_I2S_STATUS0_DATACH	GENMASK(5, 3)

#define MA35D1_I2S_STATUS0_RXUDIF	BIT(8)
#define MA35D1_I2S_STATUS0_RXOVIF	BIT(9)
#define MA35D1_I2S_STATUS0_RXTHIF	BIT(10)
#define MA35D1_I2S_STATUS0_RXFULL	BIT(11)
#define MA35D1_I2S_STATUS0_RXEMPTY	BIT(12)

#define MA35D1_I2S_STATUS0_TXUDIF	BIT(16)
#define MA35D1_I2S_STATUS0_TXOVIF	BIT(17)
#define MA35D1_I2S_STATUS0_TXTHIF	BIT(18)
#define MA35D1_I2S_STATUS0_TXFULL	BIT(19)
#define MA35D1_I2S_STATUS0_TXEMPTY	BIT(20)
#define MA35D1_I2S_STATUS0_TXBUSY	BIT(21)

/*
 * MA35D1_I2S_CTL1 - Control register 1
 *
 * Controls peripheral bus width, FIFO thresholds, and zero-crossing
 * detection enables.
 */
#define MA35D1_I2S_CTL1_CH0ZCEN		BIT(0)
#define MA35D1_I2S_CTL1_CH1ZCEN		BIT(1)
#define MA35D1_I2S_CTL1_CH2ZCEN		BIT(2)
#define MA35D1_I2S_CTL1_CH3ZCEN		BIT(3)
#define MA35D1_I2S_CTL1_CH4ZCEN		BIT(4)
#define MA35D1_I2S_CTL1_CH5ZCEN		BIT(5)
#define MA35D1_I2S_CTL1_CH6ZCEN		BIT(6)
#define MA35D1_I2S_CTL1_CH7ZCEN		BIT(7)

#define MA35D1_I2S_CTL1_TXTH		GENMASK(10, 8)
#define MA35D1_I2S_CTL1_RXTH		GENMASK(18, 16)

#define MA35D1_I2S_CTL1_PBWIDTH_32	0x0
#define MA35D1_I2S_CTL1_PBWIDTH_16	BIT(24)
#define MA35D1_I2S_CTL1_PB16ORD		BIT(25)

/*
 * MA35D1_I2S_STATUS1 - Status register 1
 *
 * Reports TX/RX FIFO counts and channel zero-crossing interrupt flags.
 */
#define MA35D1_I2S_STATUS1_CH0ZCIF	BIT(0)
#define MA35D1_I2S_STATUS1_CH1ZCIF	BIT(1)
#define MA35D1_I2S_STATUS1_CH2ZCIF	BIT(2)
#define MA35D1_I2S_STATUS1_CH3ZCIF	BIT(3)
#define MA35D1_I2S_STATUS1_CH4ZCIF	BIT(4)
#define MA35D1_I2S_STATUS1_CH5ZCIF	BIT(5)
#define MA35D1_I2S_STATUS1_CH6ZCIF	BIT(6)
#define MA35D1_I2S_STATUS1_CH7ZCIF	BIT(7)

#define MA35D1_I2S_STATUS1_TXCNT	GENMASK(12, 8)
#define MA35D1_I2S_STATUS1_RXCNT	GENMASK(20, 16)

#endif /* _MA35D1_I2S_REGS_H */
