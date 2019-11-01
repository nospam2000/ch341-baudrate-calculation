# Contents
This directory contains [a Linux kernel driver patch](./Linux_4.14.114_ch341.patch) to improve the baud rate accuracy of the CH341 Linux driver.

There is also a [unit test](./check_baud_rates_unittest.c) which checks the baud rate error
and compares the result of the original code and the new algorithm.

The script [build.sh](./build.sh) is used by me to build and load the patched kernel module without building the whole kernel.

# Result of the unit test

## Testing common baud rates

Here a comparison between the relative error in % between my algorithm and the original algorithm.
Specifically the common baud rate 921600 which is often used on ESP8266 and ESP32 boards to upload
new code has a high error. The not so common baud rates 110, 256000, 307200, 1090909 and 1333333
have a large error with the original code.

The columns have the following meaning:
 - `baud` means the requested baud rate
 - `errOrig` is the relative error of the resulting baud rate using the original code
 - `errMike` is the relative error of the resulting baud rate using my new code
 - `errJon` is the relative error of the resulting baud rate using Jonathan Olds' code
 - `pre/divOrig` the values of prescaler and divisor using the original code
 - `pre/divMike` the values of prescaler and divisor using my new code
 - `pre/divJon` the values of prescaler and divisor using Jonathan Olds' code
```
    baud    errOrig errMike errJon  pre*divOrig     pre*divMike     pre*divJon
    46      -0.10%  -0.10%  -0.10%  1024*255        1024*255        1024*255
    50      +0.16%  +0.16%  +0.16%  1024*234        1024*234        1024*234
    75      +0.16%  +0.16%  +0.16%  1024*156        1024*156        1024*156
    110     -0.44%  +0.03%  -0.44%  1024*107        512*213         1024*107
    135     -0.22%  -0.22%  -0.22%  1024*87         512*174         1024*87
    150     +0.16%  +0.16%  +0.16%  1024*78         512*156         1024*78
    300     +0.16%  +0.16%  +0.16%  1024*39         512*78          1024*39
    600     +0.16%  +0.16%  +0.16%  128*156         128*156         128*156
    1200    +0.16%  +0.16%  +0.16%  128*78          64*156          128*78
    1800    +0.16%  +0.16%  +0.16%  128*52          64*104          128*52
    2400    +0.16%  +0.16%  +0.16%  128*39          64*78           128*39
    4800    +0.16%  +0.16%  +0.16%  16*156          16*156          16*156
    7200    +0.16%  +0.16%  +0.16%  16*104          8*208           16*104
    9600    +0.16%  +0.16%  +0.16%  16*78           8*156           16*78
    14400   +0.16%  +0.16%  +0.16%  16*52           8*104           16*52
    19200   +0.16%  +0.16%  +0.16%  16*39           8*78            16*39
    31250   +0.00%  +0.00%  +0.00%  2*192           2*192           2*192
    38400   +0.16%  +0.16%  +0.16%  2*156           2*156           2*156
    45450   +0.01%  +0.01%  +0.01%  2*132           2*132           2*132
    56000   +0.13%  +0.13%  +0.13%  2*107           1*214           2*107
    57600   +0.16%  +0.16%  +0.16%  2*104           1*208           2*104
    76800   +0.16%  +0.16%  +0.16%  2*78            1*156           2*78
    100000  +0.00%  +0.00%  +0.00%  2*60            1*120           2*60
    115200  +0.16%  +0.16%  +0.16%  2*52            1*104           2*52
    128000  -0.27%  -0.27%  -0.27%  2*47            1*94            2*47
    153846  +0.00%  +0.00%  +0.00%  2*39            1*78            2*39
    187500  +0.00%  +0.00%  +0.00%  2*32            1*64            2*32
    230400  +0.16%  +0.16%  +0.16%  2*26            1*52            2*26
    250000  +0.00%  +0.00%  +0.00%  2*24            1*48            2*24
    256000  -2.34%  -0.27%  -0.27%  2*24            1*47            1*47
    307200  -2.34%  +0.16%  +0.16%  2*20            1*39            1*39
    460800  +0.16%  +0.16%  +0.16%  2*13            1*26            2*13
    500000  +0.00%  +0.00%  +0.00%  2*12            1*24            2*12
    750000  +0.00%  +0.00%  +0.00%  2*8             1*16            2*8
    857143  -0.00%  -0.00%  -0.00%  2*7             1*14            2*7
    921600  -6.99%  +0.16%  +0.16%  2*7             1*13            1*13
    1000000 +0.00%  +0.00%  +0.00%  2*6             1*12            2*6
    1090909 -8.33%  +0.00%  +0.00%  2*6             1*11            1*11
    1200000 +0.00%  +0.00%  +0.00%  2*5             1*10            2*5
    1333333 -10.00% +0.00%  +0.00%  2*5             1*9             1*9
    1500000 +0.00%  +0.00%  +0.00%  2*4             2*4             2*4
    2000000 +0.00%  +0.00%  +0.00%  2*3             2*3             2*3
    3000000 +0.00%  +0.00%  +0.00%  2*2             2*2             2*2
```

        
## Testing all uncommon baud rates

The second test scans all single baud rates between 46 and 100000 and compares which algorithm gives a
better result, here the result: `newBetter:44653, origBetter:0, badCounter:0`

This means the original algorithm was never better and the new algorithm was better 44653 times.
`badCounter:0` means the new algorithm didn't have any baud rate error >0.8%.

Above 100000 this test makes no sense because only few numbers give little baud rate errors when the
divisor gets smaller than 100. See above for the tested common baud rates.

## How does the unit test work?

It emulates a test bed, for the original functions, that means the original functions can be used
without modifying them.
