# Contents
This directory contains [a Linux kernel driver patch](./Linux_4.14.114_ch341.patch) to improve the baud rate accuracy of the CH341 Linux driver.

There is also a [unit test](./check_baud_rates_unittest.c) which checks the baud rate error
and compares the result of the original code and the new algorithm.

The script [build.sh](./build.sh) is used by me to build and load the patched kernel modulu without building the whole kernel.

# Result of the unit test

## Testing common baud rates

For the following common baud rates my new function gives a better result than the original code.
Specifically the common baud rate 921600 has a high error and also the not so common baud 
rates 256000, 307200, 1090909 and 1333333 have a large error with the original code.

The columns have the following meaning:
 - `baus` means the requested baud rate
 - `errorOrig` is the relative error of the resulting baud rate using the original code
 - `errorNew` is the relative error of the resulting baud rate using my new code
 
```
baud=46         errorOrig=-0.10%        errorNew=-0.10%
baud=50         errorOrig=+0.16%        errorNew=+0.16%
baud=75         errorOrig=+0.16%        errorNew=+0.16%
baud=110        errorOrig=-0.44%        errorNew=+0.03%
baud=135        errorOrig=-0.22%        errorNew=-0.22%
baud=150        errorOrig=+0.16%        errorNew=+0.16%
baud=300        errorOrig=+0.16%        errorNew=+0.16%
baud=600        errorOrig=+0.16%        errorNew=+0.16%
baud=1200       errorOrig=+0.16%        errorNew=+0.16%
baud=1800       errorOrig=+0.16%        errorNew=+0.16%
baud=2400       errorOrig=+0.16%        errorNew=+0.16%
baud=4800       errorOrig=+0.16%        errorNew=+0.16%
baud=7200       errorOrig=+0.16%        errorNew=+0.16%
baud=9600       errorOrig=+0.16%        errorNew=+0.16%
baud=14400      errorOrig=+0.16%        errorNew=+0.16%
baud=19200      errorOrig=+0.16%        errorNew=+0.16%
baud=31250      errorOrig=+0.00%        errorNew=+0.00%
baud=38400      errorOrig=+0.16%        errorNew=+0.16%
baud=45450      errorOrig=+0.01%        errorNew=+0.01%
baud=56000      errorOrig=+0.13%        errorNew=+0.13%
baud=57600      errorOrig=+0.16%        errorNew=+0.16%
baud=76800      errorOrig=+0.16%        errorNew=+0.16%
baud=100000     errorOrig=+0.00%        errorNew=+0.00%
baud=115200     errorOrig=+0.16%        errorNew=+0.16%
baud=128000     errorOrig=-0.27%        errorNew=-0.27%
baud=153846     errorOrig=+0.00%        errorNew=+0.00%
baud=187500     errorOrig=+0.00%        errorNew=+0.00%
baud=230400     errorOrig=+0.16%        errorNew=+0.16%
baud=250000     errorOrig=+0.00%        errorNew=+0.00%
baud=256000     errorOrig=-2.34%        errorNew=-0.27%
baud=307200     errorOrig=-2.34%        errorNew=+0.16%
baud=460800     errorOrig=+0.16%        errorNew=+0.16%
baud=500000     errorOrig=+0.00%        errorNew=+0.00%
baud=750000     errorOrig=+0.00%        errorNew=+0.00%
baud=857143     errorOrig=-0.00%        errorNew=-0.00%
baud=921600     errorOrig=-6.99%        errorNew=+0.16%
baud=1000000    errorOrig=+0.00%        errorNew=+0.00%
baud=1090909    errorOrig=-8.33%        errorNew=+0.00%
baud=1200000    errorOrig=+0.00%        errorNew=+0.00%
baud=1333333    errorOrig=-10.00%       errorNew=+0.00%
baud=1500000    errorOrig=+0.00%        errorNew=+0.00%
baud=2000000    errorOrig=+0.00%        errorNew=+0.00%
baud=3000000    errorOrig=+0.00%        errorNew=+0.00%
```

        
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


