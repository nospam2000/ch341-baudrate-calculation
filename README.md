# ch341-baudrate-calculation
How to calculate the baud rate of a CH341/CH340 usb serial adapter with a very small error rate

There is no publicly available document which explains the details about the registers and the
hardware of the CH341/CH340 usb serial adapter chips from WinChipHead (WCN).

This project is meant as a reference for drivers like the Linux and FreeBSD kernel or the Mac
OSX kernel because I really would like to use ESP8266 and ESP32 boards with a baud rate of 921600.

## How is the baud rate calculated?

It took me a while to figure it out, because all drivers are using magic constants like
1532620800 which are not clear and none of the source code I have seen does it completely right.
The hardware has a great flexibility and can do most baud rates with a error smaller than 0.2%.
Most drivers give an acceptable baud rate for the medium baud rates like 38400, but almost all of
them fail at higher baud rates like 921600 or 2000000 and at unusual baud rates like 256000.

The base formular is very simply:

***baud rate = 12000000 / prescaler / divisor***

 - '12000000' is the 12 MHz oszillator frequency which is also needed for the USB bus clock
 - 'prescaler' scales down the 12 MHz clock by a fixed dividing factor between 1 and 1024 which
   can be choosen from the following 8 values: 1, 2, 8, 16, 64, 128, 512, 1024.
   Most drivers only use the following factors : 2, 16, 128 and 1024
   Internally this works by providing three clock dividers which are cascaded and can be
   separetely bypassed.
   The divider factors are: 2, 8 and 64
   By multiplying these factors in all possible combinations you get the 8 dividing factors
   mentioned above.
 - 'divisor' is a number which can be chosen from 4 to 256

## How is the mapping between those variables and the registers of the CH341?

The ch341 has two registers which are related to the baud rate setting:
 - 0x12: Prescaler register 
 - 0x13: Divisor register

FreeBSD additionally sets a value to register 0x14 (UCHCOM_REG_BPS_MOD) but it is unclear
to me if this has any effect on the baud rate. My speculation is that this might have
something to do with the timing how long to wait for a character before sending the USB
burst transfer. They calculate the following value:
  
  UCHCOM_REG_BPS_MOD = (12000000 / 4 / baudrate + 1650 + 255) / 256

### Prescaler register 0x12 (Linux: CH341_REG_BPS_PRE, FreeBSD: UCHCOM_REG_BPS_PRE)

Each bit has a meaning on its own:
 - bit 0: =1 turns off the prescaler of factor 8
 - bit 1: =1 turns off the prescaler of factor 64
 - bit 2: =1 turns off the prescaler of factor 2
 - bit 7: =1 turns off waiting for buffer filled with 32 bytes before notifying the USB host.
   All drivers set this bit.

For example to get a prescaler of 16 use a value of `%10000010 == 0x82` to turn off the x64 divider
and activate the x2 and x8 dividers.

The meaning of the bits 3 to 6 is unknown, all drivers set them to 0.
They might contain some more modes about when to notify the host about newly received values.

### Divisor register 0x13 (Linux: CH341_REG_BPS_DIV, FreeBSD: UCHCOM_REG_BPS_DIV)

The divisor must be between 1 and 256. That means you have to choose a prescaler value so
that the divisor is within this range. The smaller the prescaler the larger and typical
better the divisor to get a small baud rate error.
The maximum officially supported baud rate is 2000000, but 3000000 also works. 

    #define CH341_CRYSTAL_FREQ (12000000UL)
    divisor = (2 * CH341_CRYSTAL_FREQ / (prescaler * baud_rate) + 1) / 2
    CH341_REG_BPS_DIV = 256 - divisor

Why not just (CH341_CRYSTAL_FREQ / (prescaler * baud_rate))? Because we are using integer
arithmetic and truncating values after the division leads to an error which only goes into
the positive direction because the fractional part of divisor is lost.

With floating point arithmetic you would do:

    divisor = TRUNC(CH341_CRYSTAL_FREQ / (prescaler * baud_rate) + 0.5)

which is equal to

    divisor = TRUNC((10 * CH341_CRYSTAL_FREQ / (prescaler * baud_rate) + 5) / 10)

The formula above does the same but using dual system integer arithmetic.

## How to choose the prescaler value and write the calculation code

You can use this code which iterates through all eight prescalar values in this order:
  1, 2, 8, 16, 64, 128, 512, 1024

When it finds a prescaler value which gives a divisor within the allowed range from 
4 to 256 it calculates `prescaler_register_value` and sets `foundDivisor=true`.

    #define CH341_OSC_FREQ    (12000000UL)
    #define CH341_REG_BPS_PRE      0x12
    #define CH341_REG_BPS_DIV      0x13
    #define CH341_REG_LCR          0x18
    #define CH341_REG_LCR2         0x25
        struct ch341_prescalers {
            u8 reg_value;
            u32 prescaler_divisor;
    };
    static const struct ch341_prescalers prescaler_table[] = {
            { 7, 1 },
            { 3, 2 },
            { 6, 8 },
            { 2, 16 },
            { 5, 64 },
            { 1, 128 },
            { 4, 512 },
            { 0, 1024 }
    };
    #define PRESCALER_TABLE_SIZE (sizeof(prescaler_table) / sizeof(prescaler_table[0]))
    
    static int ch341_set_baudrate_lcr(struct usb_device *dev,
                                      struct ch341_private *priv, u8 lcr)
    {
            unsigned long divisor;
            short prescaler_index;
            u8 divisor_regvalue;
            unsigned long prescaler;
            short prescaler_regvalue; 
            bool foundDivisor;
            int r;
    
            if (priv->baud_rate < 46 || priv->baud_rate > 3030000)
                    return -EINVAL;
    
            /*
             * CH341A has 3 chained prescalers
             * bit 0: disable prescaler factor *8
             * bit 1: disable prescaler factor *64
             * bit 2: disable prescaler factor *2
             */
            foundDivisor = false;
            prescaler_index = 8; // illegal value, just to suppress compiler warning
            // start with the smallest possible prescaler value to get the
            // best precision at first match (largest mantissa value)
            for (prescaler_index = 0; prescaler_index <= PRESCALER_TABLE_SIZE;
                            ++prescaler_index) {
                    prescaler = prescaler_table[prescaler_index].prescaler_divisor;
                    divisor = (2 * CH341_OSC_FREQ / (prescaler * priv->baud_rate) + 1) / 2;
                    if (divisor <= 256 && divisor >= 9) {
                            foundDivisor = true;
                            break;
                    }
                    // the divisors from 8 to 2 are actually 16 to 4
                    // this is needed for baud rates >=1500000
                    else if (divisor <= 8 && divisor >= 4) {
                            divisor /= 2;
                            foundDivisor = true;
                            break;
                    }
            }
    
            if (!foundDivisor)
                    return -EINVAL;
    
            /*
             * CH341A buffers data until a full endpoint-size packet (32 bytes)
             * has been received unless bit 7 is set.
             */
            prescaler_regvalue = prescaler_table[prescaler_index].reg_value | BIT(7);
            divisor_regvalue = 256 - divisor;
            printk("ch341.c: baud_rate %u, prescaler %lu, prescaler_regvalue 0x%x,"
                    " divisor %lu, divisor_regvalue 0x%x, foundDivisor %d\n",
                    priv->baud_rate, prescaler, prescaler_regvalue, divisor,
                    divisor_regvalue, foundDivisor);
            r = ch341_control_out(dev, CH341_REQ_WRITE_REG,
                    (CH341_REG_BPS_DIV      << 8) | CH341_REG_BPS_PRE,
                    (divisor_regvalue << 8) | prescaler_regvalue);
            if (r)
                    return r;
    
            r = ch341_control_out(dev, CH341_REQ_WRITE_REG,
                    (CH341_REG_LCR2 << 8) | CH341_REG_LCR, lcr);
            if (r)
                    return r;
    
            return r;
    }

## How to set the registers?

Write to registers can only be performed two registers at a time. To write to a single register
either write two times the same value to the same register, or use a dummy register to write a
dummy value as second value. Fortunately we need to write exactly two register so we need one
call and one USB request.

    prescaler_register_value |= BIT(7); // don't wait until buffer contains 32 characters before sending
    divisor_register_value = 256 - divisor;
    ch341_control_out(dev, CH341_REQ_WRITE_REG,
      (CH341_REG_BPS_DIV      << 8) | CH341_REG_BPS_PRE,
      (divisor_register_value << 8) | prescaler_register_value);

## How to calculate the error
To calculate the real baud rate and the error, you have to use the calculated register values and do
the calculation with the formula ***baud rate = 12000000 / prescaler / divisor***
Because of rounding you always have an error.

For example the source from above gives the value `prescaler=1` and `divider=13` for the requested 
baud rate of 921600 (complete divisor=13):

    real baud rate = 12000000 / 1 / 13 = 923076.92 baud
    baud rate error = (923076.92 / 921600 - 1) * 100% = 0.16%

With `prescaler=2` and `divider=7` for 921600 baud (complete divisor=14 instead of 13):

    real baud rate = 12000000 / 2 / 7 = 857142.86 baud
    baud rate error = (857142.86 / 921600 - 1) * 100% = -6.99%

With `prescaler=2` and `divider=6` for 921600 baud (complete divisor=12 instead of 13):

    real baud rate = 12000000 / 2 / 6 = 1000000.00 baud
    baud rate error = (1000000.00 / 921600 - 1) * 100% = 8.51%

So you can see that choosing the correct prescaler value and using correct rounding is essential to get a small error.

## Thanks to
 - Jonathan Olds for his efforts of analyzing and measuring the baud rate errors
   and providing a patch to improve the baud rate calculation
 - the authors of the FreeBSD ch341 driver for giving some more insights. If you read this,
   please tell me the meaning of UCHCOM_REG_BPS_MOD
 - the authors of the Linux ch341 driver for providing the driver I need
 - Apple for providing a ch341 driver (although sometimes it hangs and killing the process
   which uses the port or just pulling the USB cable causes the computer to crash)

## Links
- FreeBSD ch341 driver: https://github.com/freebsd/freebsd/blob/master/sys/dev/usb/serial/uchcom.c
- Linux ch341 driver: https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/commit/drivers/usb/serial/ch341.c
- Linux kernel patch to improve accuracy from Jonathan Olds: https://patchwork.kernel.org/patch/10983017/
- Linux kernel patch which modified the baud rate calculation (no longer set register 0x2c): https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/commit/drivers/usb/serial/ch341.c?id=4e46c410e050bcac36deadbd8e20449d078204e8


