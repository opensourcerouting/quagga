#!/bin/bash

make -k distclean 
/bin/sh ./config.status --recheck
./configure  --prefix=/home/mic/Masterarbeit/Quagga-install/ \
--localstatedir=/home/mic/Masterarbeit/Quagga-install/var \
--enable-user=mic --enable-group=users --enable-rpki
