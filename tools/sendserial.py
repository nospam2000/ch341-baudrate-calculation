#!/usr/bin/env python

# commandline parameter:
# 1. baudrate
# 2. data as hex string, e.g. "00 55 AA"

# default values
baudrate = 921600
data="00" # to measure the baud rate (will show start bit + 8 data bits as low pulse, baud=9/pulse width)
#data='55 55 55 55 ' # see every single bit
 
import serial
import time
import sys
 
# main()
# https://pyserial.readthedocs.org/en/latest/pyserial_api.html
# https://pyserial.readthedocs.org/en/latest/appendix.html#how-to
# http://www.unixwiz.net/techtips/termios-vmin-vtime.html

if len(sys.argv) >= 2:
    baudrate=sys.argv[1]

if len(sys.argv) >= 3:
    data=sys.argv[2]

print "baudrate=", baudrate, " data=", data

#ser = serial.Serial(port='/dev/ttyAMA0', baudrate=38400, timeout=2.0, rtscts=False, write_timeout=2.0, dsrdtr=False, inter_byte_timeout=0.2) # according to documentation
ser = serial.Serial(port='/dev/ttyUSB0', baudrate=baudrate, timeout=2.0, rtscts=False, writeTimeout=2.0, dsrdtr=False, interCharTimeout=0.2)
#print(ser.get_settings())
time.sleep(1)

ser.write(bytearray.fromhex(data))
