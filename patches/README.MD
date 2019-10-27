# Contents
This directory contains [a Linux kernel driver patch](./Linux_4.14.114_ch341.patch) to improve the baud rate accuracy of the CH341 Linux driver.

There is also a [unit test](./check_baud_rates_unittest.c) which checks the baud rate error
and compares the result of the original code and the new algorithm.

The script [build.sh](./build.sh) is used by me to build and load the patched kernel modulu without building the whole kernel.

# Result of the unit test

## Testing common baud rates

For the following common baud rates my new function gives a better result than the original code. Please look the `error` column.

The lines have the following meaning:
 - 'O' means 'original code'
 - 'N' means 'new code from me':
 
```
O: baud=110     real_baud=109.521       error=-0.44%    pre_reg=0x80    div_reg=0x95    pre=1024 div=107
N: baud=110     real_baud=110.035       error=+0.03%    pre_reg=0x84    div_reg=0x2b    pre=512 div=213
O: baud=256000  real_baud=250000.000    error=-2.34%    pre_reg=0x83    div_reg=0xe8    pre=2   div=24
N: baud=256000  real_baud=255319.149    error=-0.27%    pre_reg=0x87    div_reg=0xd1    pre=1   div=47
O: baud=307200  real_baud=300000.000    error=-2.34%    pre_reg=0x83    div_reg=0xec    pre=2   div=20
N: baud=307200  real_baud=307692.308    error=+0.16%    pre_reg=0x87    div_reg=0xd9    pre=1   div=39
O: baud=921600  real_baud=857142.857    error=-6.99%    pre_reg=0x83    div_reg=0xf9    pre=2   div=7
N: baud=921600  real_baud=923076.923    error=+0.16%    pre_reg=0x87    div_reg=0xf3    pre=1   div=13
O: baud=1090909 real_baud=1000000.000   error=-8.33%    pre_reg=0x83    div_reg=0xfa    pre=2   div=6
N: baud=1090909 real_baud=1090909.091   error=+0.00%    pre_reg=0x87    div_reg=0xf5    pre=1   div=11
O: baud=1333333 real_baud=1200000.000   error=-10.00%   pre_reg=0x83    div_reg=0xfb    pre=2   div=5
N: baud=1333333 real_baud=1333333.333   error=+0.00%    pre_reg=0x87    div_reg=0xf7    pre=1   div=9
```


Specifically the baud rates 921600 and 307200 have a large error with the original code.

Here the complete list of baud rates tested. Only the ones which give different results between the old
and new functions are printed above.

    unsigned long baud_rates[] = {
        46, 50, 75, 110, 135, 150, 300, 600, 1200, 1800, 2400, 4800, 7200, 9600, 14400, 19200, 31250, 38400,
        45450, 56000, 57600, 76800, 100000, 115200, 128000, 153846, 187500, 230400, 250000, 256000, 307200,
        460800, 500000, 750000, 857143, 921600, 1000000, 1090909, 1200000, 1333333, 1500000, 2000000, 3000000
    };
        
## Testing all uncommon baud rates

The second test scans all single baud rates between 46 and 100000 and compares which algorithm gives a
better result, here the result: `newBetter:44653, origBetter:0, badCounter:0`

This means the original algorithm was never better and the new algorithm was better 44653 times.
`badCounter:0` means the new algorithm didn't have any baud rate error than 0.8%.

Above 100000 this test makes no sense because only few numbers give little baud rate errors, see above
for the tested common baud rates.


## How does the unit test work?

It emulates a test bed, for the original functions, that means the original functions can be copied
without modifying them.


