//#include <linux/kernel.h>
//#include <linux/tty.h>
//#include <linux/module.h>
//#include <linux/slab.h>
//#include <linux/usb.h>
//#include <linux/usb/serial.h>
//#include <linux/serial.h>
//#include <asm/unaligned.h>

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

#define  EINVAL          22      /* Invalid argument */
#define BIT(nr)                 (1UL << (nr))


#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

typedef unsigned long uint32_t;
//typedef long int32_t;
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;
struct usb_device {int dummy;};
#define CH341_BAUDBASE_FACTOR 1532620800
#define CH341_BAUDBASE_DIVMAX 3

#define DEFAULT_BAUD_RATE 9600
#define DEFAULT_TIMEOUT   1000

/* flags for IO-Bits */
#define CH341_BIT_RTS (1 << 6)
#define CH341_BIT_DTR (1 << 5)

/******************************/
/* interrupt pipe definitions */
/******************************/
/* always 4 interrupt bytes */
/* first irq byte normally 0x08 */
/* second irq byte base 0x7d + below */
/* third irq byte base 0x94 + below */
/* fourth irq byte normally 0xee */

/* second interrupt byte */
#define CH341_MULT_STAT 0x04 /* multiple status since last interrupt event */

/* status returned in third interrupt answer byte, inverted in data
   from irq */
#define CH341_BIT_CTS 0x01
#define CH341_BIT_DSR 0x02
#define CH341_BIT_RI  0x04
#define CH341_BIT_DCD 0x08
#define CH341_BITS_MODEM_STAT 0x0f /* all bits */

/*******************************/
/* baudrate calculation factor */
/*******************************/
#define CH341_OSC_F    (12000000UL)

/* Break support - the information used to implement this was gleaned from
 * the Net/FreeBSD uchcom.c driver by Takanori Watanabe.  Domo arigato.
 */

#define CH341_REQ_READ_VERSION 0x5F
#define CH341_REQ_WRITE_REG    0x9A
#define CH341_REQ_READ_REG     0x95
#define CH341_REQ_SERIAL_INIT  0xA1
#define CH341_REQ_MODEM_CTRL   0xA4

#define CH341_REG_BREAK        0x05
#define CH341_REG_STAT1        0x06
#define CH341_REG_STAT2        0x07
#define CH341_REG_BPS_PRE      0x12
#define CH341_REG_BPS_DIV      0x13
#define CH341_REG_BPS_MOD      0x14
#define CH341_REG_BPS_PAD      0x0F
#define CH341_REG_LCR1         0x18
#define CH341_REG_LCR2         0x25

#define CH341_BPS_MOD_BASE     20000000
#define CH341_BPS_MOD_BASE_OFS 1100

#define CH341_NBREAK_BITS      0x01

#define CH341_LCR_ENABLE_RX    0x80
#define CH341_LCR_ENABLE_TX    0x40
#define CH341_LCR_MARK_SPACE   0x20
#define CH341_LCR_PAR_EVEN     0x10
#define CH341_LCR_ENABLE_PAR   0x08
#define CH341_LCR_STOP_BITS_2  0x04
#define CH341_LCR_CS8          0x03
#define CH341_LCR_CS7          0x02
#define CH341_LCR_CS6          0x01
#define CH341_LCR_CS5          0x00

struct ch341_private {
	//spinlock_t lock; /* access lock */
	unsigned baud_rate; /* set baud rate */
	//u8 mcr;
	//u8 msr;
	u8 lcr;
};

struct ch341_prescalers {
	u8 reg_value;
	u32 prescaler_div;
};
static const struct ch341_prescalers scaler_tab[] = {
	{ 7, 1 },
	{ 3, 2 },
	{ 6, 8 },
	{ 2, 16 },
	{ 5, 64 },
	{ 1, 128 },
	{ 4, 512 },
	{ 0, 1024 }
};


// mockup implementations
u8 g_regs[64];
void write_reg(u8 reg, u8 value) {
	if(reg < sizeof(g_regs)) {
		g_regs[reg] = value;
	}
}

static int ch341_control_out(struct usb_device *dev, u8 request,
			     u16 value, u16 index)
{
	write_reg((value >> 8) & 0xff, (index >> 8) & 0xff);
	write_reg(value & 0xff, index & 0xff);
	return 0;
}


static int ch341_set_baudrate_lcr_jon(struct usb_device *dev,
				  struct ch341_private *priv, u8 lcr)
{
	short a;
	int r;
	unsigned long factor;
	short divisor;

	if (!priv->baud_rate)
		return -EINVAL;
	factor = (CH341_BAUDBASE_FACTOR / priv->baud_rate);
	divisor = CH341_BAUDBASE_DIVMAX;

	while ((factor > 0xfff0) && divisor) {
		factor >>= 3;
		divisor--;
	}

	if (factor > 0xfff0)
		return -EINVAL;

	factor = 0x10000 - factor;
	a = (factor & 0xff00) | divisor;

	/*
	 * Calculate baud error using the 0,1,2,3 LSB and
	 * also the error without the divisor (LSB==7).
	 * Decide whether the divisor should be used.
	 */
	uint32_t msB = (a>>8) & 0xFF;
	uint32_t lsB = a & 0xFF;
	int32_t baud_wanted = priv->baud_rate;
	uint32_t denom = ((1<<(10-3*lsB))*(256-msB));
	/*
	 * baud_wanted==(CH341_OSC_F/256) implies MSB==0 for no divisor
	 * the 100 is for rounding.
	 */
	if (denom && ((baud_wanted+100) >= (((uint32_t)CH341_OSC_F)>>8))) {

		/* Calculate error for divisor */
		int32_t baud_expected = ((uint32_t)CH341_OSC_F) / denom;
		uint32_t baud_error_difference = abs(baud_expected-baud_wanted);

		/* Calculate a for no divisor */
		uint32_t a_no_divisor = ((0x10000-(((uint32_t)CH341_OSC_F)<<8) /
			baud_wanted+128) & 0xFF00) | 0x07;

		/* a_no_divisor is only valid for MSB<248 */
		if ((a_no_divisor>>8) < 248) {

			/* Calculate error for no divisor */
			int32_t baud_expected_no_divisor = ((uint32_t)CH341_OSC_F) /
				(256-(a_no_divisor>>8));
			uint32_t baud_error_difference_no_divisor =
				abs(baud_expected_no_divisor-baud_wanted);

			/*
			 * If error using no divisor is less than using
			 * a divisor then use it instead for the "a" word.
			 */
			if (baud_error_difference_no_divisor < baud_error_difference)
				a = a_no_divisor;
		}

	}

	/*
	 * CH341A buffers data until a full endpoint-size packet (32 bytes)
	 * has been received unless bit 7 is set.
	 */
	a |= BIT(7);

	r = ch341_control_out(dev, CH341_REQ_WRITE_REG, 0x1312, a);
	if (r)
		return r;

	r = ch341_control_out(dev, CH341_REQ_WRITE_REG, 0x2518, lcr);
	if (r)
		return r;

	return r;
}

static int ch341_set_baudrate_lcr(struct usb_device *dev,
				  struct ch341_private *priv, u8 lcr)
{
	short a;
	int r;
	unsigned long factor;
	short divisor;

	if (!priv->baud_rate)
		return -EINVAL;
	factor = (CH341_BAUDBASE_FACTOR / priv->baud_rate);
	divisor = CH341_BAUDBASE_DIVMAX;

	while ((factor > 0xfff0) && divisor) {
		factor >>= 3;
		divisor--;
	}

	if (factor > 0xfff0)
		return -EINVAL;

	factor = 0x10000 - factor;
	a = (factor & 0xff00) | divisor;

	/*
	 * CH341A buffers data until a full endpoint-size packet (32 bytes)
	 * has been received unless bit 7 is set.
	 */
	a |= BIT(7);

	r = ch341_control_out(dev, CH341_REQ_WRITE_REG, 0x1312, a);
	if (r)
		return r;

	r = ch341_control_out(dev, CH341_REQ_WRITE_REG, 0x2518, lcr);
	if (r)
		return r;

	return r;
}


static int ch341_set_baudrate_lcr_new(struct usb_device *dev,
				  struct ch341_private *priv, u8 lcr)
{
	int found_div;
	u8 div_regvalue;
	u8 prescaler_regvalue;
	short prescaler_index;
	int r;

	if (priv->baud_rate < 46 || priv->baud_rate > 3030000)
		return -EINVAL;

	found_div = 0;
	// start with the smallest possible prescaler value to get the
	// best precision at first match (largest mantissa value)
	for (prescaler_index = 0; prescaler_index < ARRAY_SIZE(scaler_tab);
			++prescaler_index) {
		unsigned long prescaler;
		unsigned long div;

		prescaler = scaler_tab[prescaler_index].prescaler_div;
		div = ((2UL * CH341_OSC_F)
			/ (prescaler * priv->baud_rate) + 1UL) / 2UL;
		// when prescaler==1 the divisors from 8 to 2 are
		// actually 16 to 4; skip them, use next prescaler
		if (prescaler == 1 && div <= 8) {
			continue;
		} else if (div <= 256 && div >= 2) {
			found_div = 1;
			prescaler_regvalue =
				scaler_tab[prescaler_index].reg_value | BIT(7);
			div_regvalue = 256 - div;
			break;
		}
	}

	if (!found_div)
		return -EINVAL;

	/*
	 * CH341A buffers data until a full endpoint-size packet (32 bytes)
	 * has been received unless bit 7 is set.
	 */
	r = ch341_control_out(dev, CH341_REQ_WRITE_REG,
		(CH341_REG_BPS_DIV << 8) | CH341_REG_BPS_PRE,
		(div_regvalue      << 8) | prescaler_regvalue);
	if (r)
		return r;

	r = ch341_control_out(dev, CH341_REQ_WRITE_REG,
		(CH341_REG_LCR2 << 8) | CH341_REG_LCR1, lcr);
	if (r)
		return r;

	return r;
}


double calcRealBaud(u8 prescaler_reg, u8 div_reg, unsigned long *p_pre, unsigned long *p_div)
{
	double baud;
	int i;

	*p_pre = 0;
	for(i = 0; i < ARRAY_SIZE(scaler_tab); i++) {
		if((scaler_tab[i].reg_value & 0x07) == (prescaler_reg & 0x07)) {
			*p_pre = scaler_tab[i].prescaler_div;
			break;
		}
	}

	if(!*p_pre)
		return 0.0;

	*p_div = 256 - div_reg;
	if (*p_div == 1)
		*p_div = 78;
	else if (*p_pre == 1 && *p_div <= 8 && *p_div >= 2)
		*p_div *= 2;

	baud = (double)CH341_OSC_F / (*p_pre * *p_div);

	return baud;
}

double calc_baud_error(unsigned long baud, double real_baud)
{
	return ((real_baud / (double)baud) - 1.0) * 100.0;
}


struct baud_compare {
	int rc;
	unsigned long baud;
	double real_baud;
	double baud_error;
	unsigned long pre;
	unsigned long div;
	u8 pre_reg;
	u8 div_reg;
};

typedef int (pfct_ch341_set_baudrate_lcr)(struct usb_device *dev,
				  struct ch341_private *priv, u8 lcr);

void test_baud_rate(struct baud_compare *p_bc, unsigned long baud, pfct_ch341_set_baudrate_lcr pfct)
{
	struct usb_device dummy_dev;
	struct ch341_private priv;
	u8 lcr = 0;
	memset(&dummy_dev, 0, sizeof(dummy_dev));
	memset(&priv, 0, sizeof(priv));
	memset(&g_regs, 0, sizeof(g_regs));

	p_bc->baud = baud;
	priv.baud_rate = p_bc->baud;
	p_bc->rc = pfct(&dummy_dev, &priv, lcr);
	p_bc->pre_reg = g_regs[CH341_REG_BPS_PRE];
	p_bc->div_reg = g_regs[CH341_REG_BPS_DIV];
	p_bc->real_baud = calcRealBaud(p_bc->pre_reg, p_bc->div_reg, &p_bc->pre, &p_bc->div);
	p_bc->baud_error = calc_baud_error(p_bc->baud, p_bc->real_baud);
}

void test_range(unsigned long start, unsigned long end)
{
	unsigned long newBetter = 0;
	unsigned long origBetter = 0;
	unsigned long jonBetter = 0;
	unsigned long badCounter = 0;

	for(unsigned long baud = start; baud < end; baud++) {
		struct baud_compare bc1;
		struct baud_compare bc2;
		struct baud_compare bc3;
		test_baud_rate(&bc1, baud, ch341_set_baudrate_lcr);
		test_baud_rate(&bc2, baud, ch341_set_baudrate_lcr_new);
		test_baud_rate(&bc3, baud, ch341_set_baudrate_lcr_jon);

		// for the range 46 to 100000: newBetter:44653, origBetter:0, badCounter:0
		// for the range 46 to 3000000
		if(fabs(fabs(bc1.baud_error) - fabs(bc2.baud_error)) > 0.01) // ignore floating point errors
		{
			if(fabs(bc1.baud_error) < fabs(bc2.baud_error))
			{
				origBetter++;
#if 0
				printf("O: baud=%ld\treal_baud=%.3lf\terror=%+.2lf\%\tpre_reg=0x%02x\tdiv_reg=0x%02x\tpre=%lu\tdiv=%lu\n",
				      bc1.baud, bc1.real_baud, bc1.baud_error, bc1.pre_reg, bc1.div_reg, bc1.pre, bc1.div);
				printf("N: baud=%ld\treal_baud=%.3lf\terror=%+.2lf\%\tpre_reg=0x%02x\tdiv_reg=0x%02x\tpre=%lu\tdiv=%lu\n",
				      bc2.baud, bc2.real_baud, bc2.baud_error, bc2.pre_reg, bc2.div_reg, bc2.pre, bc2.div);
#endif
			}
			else if(fabs(bc3.baud_error) < fabs(bc2.baud_error))
			{
				jonBetter++;
#if 0
				printf("O: baud=%ld\treal_baud=%.3lf\terror=%+.2lf\%\tpre_reg=0x%02x\tdiv_reg=0x%02x\tpre=%lu\tdiv=%lu\n",
				      bc1.baud, bc1.real_baud, bc1.baud_error, bc1.pre_reg, bc1.div_reg, bc1.pre, bc1.div);
				printf("N: baud=%ld\treal_baud=%.3lf\terror=%+.2lf\%\tpre_reg=0x%02x\tdiv_reg=0x%02x\tpre=%lu\tdiv=%lu\n",
				      bc2.baud, bc2.real_baud, bc2.baud_error, bc2.pre_reg, bc2.div_reg, bc2.pre, bc2.div);
#endif
			}
			else if(fabs(bc1.baud_error) > fabs(bc2.baud_error) && fabs(bc3.baud_error) > fabs(bc2.baud_error))
			{
				newBetter++;
			}
		}

		if(fabs(bc2.baud_error) > 0.8)
		{
			badCounter++;
#if 0
			printf("O: baud=%ld\treal_baud=%.3lf\terror=%+.2lf\%\tpre_reg=0x%02x\tdiv_reg=0x%02x\tpre=%lu\tdiv=%lu\n",
			      bc1.baud, bc1.real_baud, bc1.baud_error, bc1.pre_reg, bc1.div_reg, bc1.pre, bc1.div);
			printf("N: baud=%ld\treal_baud=%.3lf\terror=%+.2lf\%\tpre_reg=0x%02x\tdiv_reg=0x%02x\tpre=%lu\tdiv=%lu\n",
			      bc2.baud, bc2.real_baud, bc2.baud_error, bc2.pre_reg, bc2.div_reg, bc2.pre, bc2.div);
#endif
		}
	}

	printf("newBetter:%ld, origBetter:%ld, jonBetter:%ld, badCounter:%ld\n", newBetter, origBetter, jonBetter, badCounter);
}

void test_list()
{
	unsigned long newBetter = 0;
	unsigned long origBetter = 0;
	unsigned long badCounter = 0;

	unsigned long baud_rates[] = {
		46, 50, 75, 110, 135, 150, 300, 600, 1200, 1800, 2400, 4800, 7200, 9600, 14400, 19200,
		31250, 38400, 45450, 56000, 57600, 76800, 100000, 115200, 128000, 153846, 187500,
		230400, 250000, 256000, 307200, 460800, 500000, 750000, 857143, 921600, 1000000,
		1090909, 1200000, 1333333, 1500000, 2000000, 3000000,
	};

	printf("baud\terrOrig\terrMike\terrJon\tpre*divOrig\tpre*divMike\tpre*divJon\n");
	for(unsigned long i = 0; i < ARRAY_SIZE(baud_rates); i++) {
		unsigned long baud = baud_rates[i];
		struct baud_compare bc1;
		struct baud_compare bc2;
		struct baud_compare bc3;
		test_baud_rate(&bc1, baud, ch341_set_baudrate_lcr);
		test_baud_rate(&bc2, baud, ch341_set_baudrate_lcr_new);
		test_baud_rate(&bc3, baud, ch341_set_baudrate_lcr_jon);

		//printf("baud=%ld \terrOrig=%+.2lf\%  \terrMike=%+.2lf\%  \terrJon=%+.2lf\%  \tpre*divOrig=%ld*%ld  \tpre*divMike=%ld*%ld  \tpre*divJon=%ld*%ld\n",
		printf("%ld\t%+.2lf\%\t%+.2lf\%\t%+.2lf\%\t%ld*%ld      \t%ld*%ld      \t%ld*%ld\n",
			bc1.baud, bc1.baud_error, bc2.baud_error, bc3.baud_error, bc1.pre, bc1.div, bc2.pre, bc2.div, bc3.pre, bc3.div);
	}
	printf("\n");
}

int main(int argc, char**argv)
{
	test_list();
	test_range(46, 100000);
}
