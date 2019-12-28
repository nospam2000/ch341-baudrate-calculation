# ch341-baudrate-calculation HOWTO
How to calculate the baud rate of a CH341/CH340 usb serial adapter with a very small error rate

There is no publicly available document which explains the details about the registers and the
hardware of the CH341/CH340 usb serial adapter chips from WinChipHead (WCN).

This project is meant as a reference for drivers like the Linux and FreeBSD kernel or the Mac
OSX kernel because I really would like to use ESP8266 and ESP32 boards with a baud rate of 921600.

The contents of this project:
 - this README.md which describes how to calculate the baud rate and the limits.
 - [docs](./docs/) : a spreadsheet with calculations and measurements to prove that the used
   formulas are correct. It also contains the error calculation. Baud rates not contained can
   easily be added to see their error value.
 - [patches](./patches/) : a Linux kernel patch to use the new formula to calculate the baudrate
   and a unit test to compare the result to the original implementation and the implementation
   from Jonathan Olds' patch.
 - [measurements](./measurements/) : scope measurements of transmissions with some baud rates.
   The hex constants at the end of the name (e.g. 0x55_0x55) are the data which was transmitted.
   Includes pictures and csv raw data.

## How is the baud rate calculated?

It took me a while to figure it out, because most drivers are using magic constants like
1532620800 which are not clear.

The NetBSD driver was the best reference I could find (see section links below), I
just found it after I created this whole project. You should really consider to use
their work.

The hardware has a great flexibility and can do most baud rates with a error smaller than 0.2%.
Most drivers give an acceptable baud rate for the medium and common baud rates like 38400,
but many fail at higher baud rates like 921600 and at unusual baud rates
like 256000.

The base formular is very simple: ***baud rate = 12000000 / prescaler / divisor***

 - '12000000' is the 12 MHz oszillator frequency which is also needed for the USB bus clock
 - 'prescaler' scales down the 12 MHz clock by a fixed dividing factor between 1 and 1024 which
   can be choosen from the following 8 values: 1, 2, 8, 16, 64, 128, 512, 1024.

   Most drivers only use the prescaler factors 2, 16, 128 and 1024 (register values 0 to 3). 
   Internally the prescaler works by providing three clock dividers which are cascaded
   and can be bypassed separately.
   
   The three prescaler divisor base factors are 2, 8 and 64.
   By multiplying these factors in all possible combinations you get the 8 prescaler factors
   mentioned above.
 - 'divisor' is a number which can be chosen from 2 to 256 (with some limitations when `prescaler=1`, see below)

## How is the mapping between those variables and the registers of the CH341?

The ch341 has two registers which are related to the baud rate setting:
 - 0x12: Prescaler register 
 - 0x13: Divisor register

### Prescaler register 0x12 (Linux: CH341_REG_BPS_PRE, FreeBSD: UCHCOM_REG_BPS_PRE)

Each bit of this register has a meaning on its own:
 - bit 0: =1 turns off the prescaler of factor 8
 - bit 1: =1 turns off the prescaler of factor 64
 - bit 2: =1 turns off the prescaler of factor 2
 - bit 7: =1 turns off waiting for buffer filled with 32 bytes before notifying the USB host.
   All drivers set this bit.

For example to get a prescaler of 16 use a value of `%10000010 == 0x82` to turn off the x64 divider
and activate the x2 and x8 dividers.

The meaning of the bits 3 to 6 is unknown, all known drivers set them to 0.

### Divisor register 0x13 (Linux: CH341_REG_BPS_DIV, FreeBSD: UCHCOM_REG_BPS_DIV)

The divisor must be between 2 and 256. That means you have to choose a prescaler value so
that the divisor is within this range. The smaller the prescaler the larger and typical
better the divisor to get a small baud rate error.

The maximum officially supported baud rate is 2000000. A baud rate of 3000000 can also be
set and at least sending data at this rate is possible, but the stop bit length is too long
for baud rates > 500000, details see chapter below. The smallest possible baud rate is 46.

Here the formula for the register values based on the `prescaler` and `divisor` values:

    #define CH341_CRYSTAL_FREQ (12000000UL)
    divisor = (2UL * CH341_CRYSTAL_FREQ / (prescaler * baud_rate) + 1UL) / 2UL
    CH341_REG_BPS_DIV = 256 - divisor

Using this formula together with choosing the right prescaler will give you an relative baud rate error <= 0.16% for the common baud rates. For all other baud rates between 46 to 100000 (the uncommon ones) the error is < 0.8% . Above 100000 baud you should only use selected baud rates.

#### Divisor values <= 8 when prescaler=1 ####

The divisor register doesn't treat all values equally. The `divisor` values from 9 to 256 are
just mapped 1:1, but when `prescaler=1` the values between 8 and 2 give a divisor which
is double of the specified value (16 to 4), for example using `divisor=8` divides
actually by 16. That means the values <8 should not be used when `prescaler=1` because
then you can also just use `prescaler=2`. This is important for the baud rates >=1500000.
                
`divisor = 1` results in a actual divisor of 78 when `prescaler=1` and is therefore also not used.

#### Rounding issues ####

Why not just using the simple formula `(CH341_CRYSTAL_FREQ / (prescaler * baud_rate))`?
Because we are using integer arithmetic and truncating values after the division leads to
an error which only goes into the positive direction because the fractional part of
divisor is lost.

With floating point arithmetic you would do 5/10 bankers rounding to spread the error equally
between positive and negative range:

    divisor = TRUNC(CH341_CRYSTAL_FREQ / (prescaler * baud_rate) + 0.5)

which is equal to

    divisor = TRUNC((10 * CH341_CRYSTAL_FREQ / (prescaler * baud_rate) + 5) / 10)

The formula to calculate the baud rate above does the same rounding but using dual
system integer arithmetic with `(2 * ... + 1) / 2`.


### MOD register 0x14 (Linux: CH341_REG_BPS_MOD, FreeBSD: UCHCOM_REG_BPS_MOD)

FreeBSD additionally sets a value to register 0x14 (UCHCOM_REG_BPS_MOD) but the reason
is unclear to me. It doesn't have a direct influence on the baud rate.

My speculation is that this has something to do with other timing based setting,
e.g. the Oversampling Constant (OVS), how long to wait for a character
before sending the USB transfer or the length of the stop bits above 500000 baud?

They calculate the value using the following (simplified) formula:
  
  UCHCOM_REG_BPS_MOD = (12000000 / 4 / baudrate + 1650 + 255) / 256


## How to choose the prescaler value and write the calculation code

You can use this code which iterates through all eight prescalar values in this order:
  1, 2, 8, 16, 64, 128, 512, 1024

When it finds a prescaler value which gives a divisor within the allowed range from 
2 to 256 it calculates `prescaler_register_value` and sets `foundDivisor=true`.
In [./patches/Linux_4.14.114_ch341.patch](./patches/Linux_4.14.114_ch341.patch) you
can find a patch which can directly applied to a Linux kernel and is updated more
often than the code here.

    #define CH341_OSC_FREQ    (12000000UL)
    #define CH341_REG_BPS_PRE      0x12
    #define CH341_REG_BPS_DIV      0x13
    #define CH341_REG_LCR1         0x18
    #define CH341_REG_LCR2         0x25

    struct ch341_prescalers {
            u8 reg_value;
            u32 prescaler_divisor;
    };

    /*
     * CH341A has 3 chained prescalers
     * bit 0: disable prescaler factor *8
     * bit 1: disable prescaler factor *64
     * bit 2: disable prescaler factor *2
     */
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
            int found_div;
            u8 div_regvalue;
            u8 prescaler_regvalue;
            u8 mod_regval;
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
    
If you don't like the table driven approach to get the prescaler value from the index 0..7 you
could also calculate the prescaler value but it is harder to understand:

    prescaler =
          ((prescaler_index & BIT(2)) ? 1 : 2)    // prescaler 2
        * ((prescaler_index & BIT(1)) ? 1 : 64)   // prescaler 64
        * ((prescaler_index & BIT(0)) ? 1 : 8);   // prescaler 8
        
    prescaler_register_value = (
              ((prescaler_index >> 1) & (BIT(0) | BIT(1)))
            | ((prescaler_index << 2) & BIT(2))
        ) ^ (BIT(0) | BIT(1) | BIT(2)); // all 3 bits inverted
        
    prescaler_register_value |= BIT(7); // don't buffer the data until buffer full
        

## How to set the registers?

Write to registers can only be performed two registers at a time. To write to a single register
either write two times the same value to the same register, or use a dummy register to write a
dummy value as second value. Fortunately we need to write exactly two registers so we need one
call and one USB request.

    prescaler_register_value |= BIT(7); // don't wait until buffer contains 32 characters before sending
    divisor_register_value = 256 - divisor;
    ch341_control_out(dev, CH341_REQ_WRITE_REG,
      (CH341_REG_BPS_DIV      << 8) | CH341_REG_BPS_PRE,         // the two register addresses
      (divisor_register_value << 8) | prescaler_register_value); // the two register values

## How to calculate the error
Because of rounding you always have an error when `12000000 / wanted baud rate` is not a natural number.
To calculate the real baud rate and the error, you have to use the calculated register values and do
the calculation with the formula ***resulting baud rate = 12000000 / prescaler / divisor*** 

The error formula: `error (in percent) = ((resulting baud rate / wanted baud rate) - 1) * 100`

For example the source from above gives the value `prescaler=1` and `divider=13` for the requested 
baud rate of 921600 (complete divisor=13):

    resulting baud rate = 12000000 / 1 / 13 = 923076.92 baud
    baud rate error = (923076.92 / 921600 - 1) * 100% = 0.16%

With `prescaler=2` and `divider=7` for 921600 baud (complete divisor=14 instead of 13):

    resulting baud rate = 12000000 / 2 / 7 = 857142.86 baud
    baud rate error = (857142.86 / 921600 - 1) * 100% = -6.99%

With `prescaler=2` and `divider=6` for 921600 baud (complete divisor=12 instead of 13):

    resulting baud rate = 12000000 / 2 / 6 = 1000000.00 baud
    baud rate error = (1000000.00 / 921600 - 1) * 100% = 8.51%

So you can see that choosing the correct prescaler value and using correct rounding is essential to get a smaller error.

## Stop bit length too long for baud rates > 500000 ##
The minimum stop bit time for transmitting is 2.00 µs (see [scope picture](./measurements/3000000_baud_zoom_stopbit/F0007TEK.BMP?raw=true)) which is correct for 500000.
This means any baud rate above that has too long stop bits. For sending, this just
reduces the throughput (bytes/s) but I haven't tested if data is lost when a sender
with a correct stop bit time sends with full speed. 

### Here the measured number of stop bits for sending data (the transmitted values are 0x55) ###
#### <= 500000 baud: 1 stop bits ####
![scope picture](./measurements/500000_baud_0x55_0x55_0x55_0x55/F0010TEK.BMP)

#### 1000000 baud: 2 stop bits ####
![scope picture](./measurements/1000000_baud_0x55_0x55/F0009TEK.BMP)

#### 2000000 baud: 4 stop bits ####
![scope picture](./measurements/2000000_baud_0x55_0x55_0x55_0x55/F0008TEK.BMP)

#### 3000000 baud: 6 stop bits ####
![scope picture](./measurements/3000000_baud_0x55_0x55/F0006TEK.BMP)

## With the new driver my application no longer works
With the old driver you were able to use the baud rate 115200 with your hardware but with the new formula in the driver it does not longer work. What happened?
The formula of the old driver might result in a baud rate of the CH341 which is closer to the real baud rate of your microcontroller although it is closer to the nomimal baud rate.

Most baud rates cannot be exactly resembled by hardware. This holds true for the CH341 as well as for the UART of your microcontroller which is connected to the CH341. As long as the _real baudrates_ of both sides are within a small enough tolerance window it works.

I use the ATmega328P with 15200 baud in this example, but the principle is the same for other microcontrollers and baud rates.
The ATmega328P is often used with a oscillator frequency of 16 MHz. The baud rate is derived from that clock with this formula (using double speed mode with U2Xn=1): `baud = fosc / 8*(UBRR + 1)`

The best fit for 115200 baud is with `UBRR=16` which gives a real baud rate of 117647 (error=+2.1%). This doesn't match the 115385 baud of the CH341 for the nominal baud rate 115200 baud very well. Depending on you calculation you might also be using `UBRR=17` which results in a real baud rate of 111111 (error=-3.6%).

What can you do to solve the problem?
 1. use baud rates like 125000 or 250000 which can be exactly derived from the oscillator frequency 16 MHz and have an error of 0% or use lower standard baud rates like 38400 which also have smaller errors
 1. calculate and use the _real baud rate_ of your microcontroller (instead of the _nominal baud rate_) for your application on the PC, e.g. for the ATmega328P example you would have to use 117647 baud instead of the nominal value of 115200 baud
 1. use a 'baud rate oscillator' for your ATmega328P with a frequency of e.g. 18.4320MHz or 14.7456MHz which can handle the standard baud rates with a small error
 
See also the link to the ATmega datasheet in the [Links] section.

## Thanks to
 - Jonathan Olds for his efforts of analyzing and measuring the baud rate errors
   and providing a patch to improve the baud rate calculation
 - Johan Hovold for maintaining the Linux driver
 - the authors of the Linux ch341 driver for providing the driver I need
 - the authors of the FreeBSD ch341 driver for giving some more insights. If you read this,
   please tell me the meaning of UCHCOM_REG_BPS_MOD
 - Apple for providing a ch341 driver. I would be happy if you could use my work to fix
   the baud rate 921600.

## Links
- [NetBSD ch341 driver](http://cvsweb.netbsd.org/bsdweb.cgi/src/sys/dev/usb/uchcom.c?only_with_tag=MAIN)  This is really the best implementation I could find so far.
- [NetBSD ch341 driver changlelog](http://cvsweb.netbsd.org/bsdweb.cgi/src/sys/dev/usb/uchcom.c?only_with_tag=MAIN) 
- [FreeBSD ch341 driver](https://github.com/freebsd/freebsd/blob/master/sys/dev/usb/serial/uchcom.c)
- [Linux ch341 driver](https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/commit/drivers/usb/serial/ch341.c)
- [Linux ch341 kernel patch to improve baud rate mail thread](https://lore.kernel.org/linux-usb/001001d590e9$262004f0$72600ed0$@co.nz/T/#t)
- [Linux ch341 kernel patch to improve baud rate accuracy from Jonathan Olds](https://patchwork.kernel.org/patch/10983017/)
- [Linux ch341 kernel patch which modified the baud rate calculation (no longer set register 0x2c)](https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/commit/drivers/usb/serial/ch341.c?id=4e46c410e050bcac36deadbd8e20449d078204e8)
- [Linux kernel patch which improved the baud rate calculation and adds register names](https://lore.kernel.org/patchwork/patch/139700/)
- [Official WCH CH341 download page](http://www.wch.cn/downloads/CH341SER_LINUX_ZIP.html)
- [Archives of old Mac OSX CH340/CH341 drivers for Mac OSX version Mojave or 10.14 or earlier](https://github.com/adrianmihalko/ch340g-ch34g-ch34x-mac-os-x-driver). These are not needed for newer OSX versions which already have a driver from Apple (which also suffers from bad baudrate calculation).
- [Data sheet for ATmega 48, 88, 168 and 328](http://ww1.microchip.com/downloads/en/DeviceDoc/ATmega48A-PA-88A-PA-168A-PA-328-P-DS-DS40002061A.pdf), please refer specifically to chapters "20.3.1 Internal Clock Generation – The Baud Rate Generator" and "20.10 Examples of Baud Rate Setting"
