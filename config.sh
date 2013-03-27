#!/bin/bash

# make -k distclean 
# /bin/sh ./config.status --recheck
./configure  -C --prefix=/home/michael/Masterarbeit/Quagga/install/ \
--localstatedir=/home/michael/Masterarbeit/Quagga/install/var \
--enable-user=michael --enable-group=users

