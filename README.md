# ch341-baudrate-calculation
How to calculate the baud rate of a CH341/CH340 usb serial adapter with a very small error rate

There is no publicly available document which explains the details about the registers and the
hardware of the CH341/CH340 usb serial adapter chips from WinChipHead (WCN).

This project is meant as a reference for drivers like the Linux and FreeBSD kernel or the Mac
OSX kernel.

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
 - 'divisor' is a number which can be chosen from 1 to 256

## How is the mapping between those variables and the registers of the CH341?

The ch341 has two registers which are related to the baud rate setting:
 - 0x12: Prescaler register 
 - 0x13: Divisor register

FreeBSD sets a value to register 0x14 (UCHCOM_REG_BPS_MOD) but it is unclear to me if this
has any effect on the baud rate. My speculation is that is has something to do with the
timing how long to wait for a character before sending the USB burst transfer.
UCHCOM_REG_BPS_MOD = (12000000 / 4 / baudrate + 1650 + 255) / 256

### Prescaler register 0x12 (Linux: CH341_REG_BPS_PRE, FreeBSD: UCHCOM_REG_BPS_PRE)

Each bit has a meaning on its own:
 - bit 0: =1 turns off the prescaler of factor 8
 - bit 1: =1 turns off the prescaler of factor 64
 - bit 2: =1 turns off the prescaler of factor 2
 - bit 7: =1 turns off waiting for buffer filled with 32 bytes before notifying the USB host.
   All drivers set this bit.

For example to get a prescaler of 16 use a value of %10000010 to turn off the x64 divider
and activate the x2 and x8 dividers.

The meaning of the bits 3 to 6 is unknown, all drivers set them to 0.
They might contain some more modes about when to notify the host about newly received values.

### Divisor register 0x13 (Linux: CH341_REG_BPS_DIV, UCHCOM_REG_BPS_DIV)

The divisor must be between 1 and 256. That means you have to choose a prescaler value so
that the divisor is within this range. The smaller the prescaler the larger and typical
better the divisor to get a small baud rate error.
The maximum supported baud rate is 2000000. 

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

## How to choose the prescaler value

You can use this code which iterates through all eight prescalar values in this order:
  1, 2, 8, 16, 64, 128, 512, 1024
When it finds a prescaler value which gives a divisor within the allowed range from 
1 to 256 it calculates prescaler_register_value and set foundDivisor=true.

*****TODO: the code needs to be tested*****

    unsigned long divisor;
    u8 divisor_register_value;
    unsigned long prescaler;
    short prescaler_register_value;
    bool foundDivisor = false;
    // start with the smallest possible prescaler value to get the best precision
    // at first match (largest mantissa value)
    for(short prescaler_index = 7; prescaler_index >= 0; --prescaler_index) {
      prescaler = ((prescaler_index & BIT(2)) ? 1 : 2)
        * ((prescaler_index & BIT(1)) ? 1 : 64)
        * ((prescaler_index & BIT(0)) ? 1 : 8);
      divisor = (2 * CH341_CRYSTAL_FREQ / (prescaler * priv->baud_rate) + 1) / 2;
      if (divisor >= 1 && divisor <= 256) {
        foundDivisor = true;
        prescaler_register_value = ((prescaler_index >> 1) & (BIT(0) | BIT(1)))
          | ((prescaler_index << 2) & BIT(2));
        break;
      }
    }

## How to set the registers?

Write to registers can only be performed two registers at a time. To write to a single register
either write two times the same value to the same register, or use a dummy register to write a
dummy value as second value.

 prescaler_register_value |= BIT(7); // don't wait until buffer contains 32 characters before sending
 divisor_register_value = 256 - divisor;
 ch341_control_out(dev, CH341_REQ_WRITE_REG,
  (CH341_REG_BPS_DIV      << 8) | CH341_REG_BPS_PRE,
  (divisor_register_value << 8) | prescaler_register_value);

## Thanks to
 - Jonathan Olds for his efforts of analyzing and measuring the baud rate errors
   and providing a patch to improve the baud rate calculation
 - the authors of the FreeBSD ch341 driver for giving some more insights. If you read this,
   please tell me the meaning of UCHCOM_REG_BPS_MOD
 - the authors of the Linux ch341 driver for providing the driver I need

## Links
- FreeBSD ch341 driver: https://github.com/freebsd/freebsd/blob/master/sys/dev/usb/serial/uchcom.c
- Linux ch341 driver: https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/commit/drivers/usb/serial/ch341.c
- Linux kernel patch to improve accuracy from Jonathan Olds: https://patchwork.kernel.org/patch/10983017/
- Linux kernel patch which modified the baud rate calculation (no longer set register 0x2c): https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/commit/drivers/usb/serial/ch341.c?id=4e46c410e050bcac36deadbd8e20449d078204e8


