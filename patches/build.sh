#!/bin/sh
(cd ../../.. ; make -C . M=drivers/usb/serial)
sudo cp ch341.ko /lib/modules/4.14.114-v7+/kernel/drivers/usb/serial/ch341.ko
sudo rmmod ch341
sudo modprobe ch341
git diff ch341.c >Linux_4.14.114_ch341.patch
../../../scripts/checkpatch.pl Linux_4.14.114_ch341.patch
