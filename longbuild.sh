#!/bin/bash

# Iterate over all possible combinations of a particular set of Quagga build
# flags, checking that the source tree successfully builds for each combination.

# print a 4-bit integer as binary
function dectobin_4bits()
{
	case $1 in
	0)  echo -n 0000 ;;
	1)  echo -n 0001 ;;
	2)  echo -n 0010 ;;
	3)  echo -n 0011 ;;
	4)  echo -n 0100 ;;
	5)  echo -n 0101 ;;
	6)  echo -n 0110 ;;
	7)  echo -n 0111 ;;
	8)  echo -n 1000 ;;
	9)  echo -n 1001 ;;
	10) echo -n 1010 ;;
	11) echo -n 1011 ;;
	12) echo -n 1100 ;;
	13) echo -n 1101 ;;
	14) echo -n 1110 ;;
	15) echo -n 1111 ;;
	*) echo "ERROR in argument ($1)" ;;
	esac
}

# print an 8-bit integer as binary
function dectobin_8bits()
{
	dectobin_4bits $(($1 / 16)) # high 4 bits
	dectobin_4bits $(($1 % 16)) # low 4 bits
}

if [ `uname -s` = FreeBSD ]; then
	MAKE=gmake
else
	MAKE=make
fi

# iterate over decimals, decoding each into a combination of build flags
for ((i = 0; i < 2**7; i++)); do
	binstr=`dectobin_8bits $i`
	confargs=''
	if [ ${binstr:7:1} = 1 ]; then
		confargs="$confargs --enable-ipv6"
	else
		confargs="$confargs --disable-ipv6"
	fi
	if [ ${binstr:6:1} = 1 ]; then
		confargs="$confargs --enable-rtadv"
	else
		confargs="$confargs --disable-rtadv"
	fi
	if [ ${binstr:5:1} = 1 ]; then
		confargs="$confargs --enable-opaque-lsa"
	else
		confargs="$confargs --disable-opaque-lsa"
	fi
	if [ ${binstr:4:1} = 1 ]; then
		confargs="$confargs --enable-irdp"
	else
		confargs="$confargs --disable-irdp"
	fi
	if [ ${binstr:3:1} = 1 ]; then
		confargs="$confargs --enable-snmp"
	else
		confargs="$confargs --disable-snmp"
	fi
	if [ ${binstr:2:1} = 1 ]; then
		confargs="$confargs --with-libpam"
	else
		confargs="$confargs --without-libpam"
	fi
	if [ ${binstr:1:1} = 1 ]; then
		confargs="$confargs --with-libgcrypt"
	else
		confargs="$confargs --without-libgcrypt"
	fi
#	if [ ${binstr:0:1} = 1 ]; then
#		arg_ipv6='--enable-something'
#	else
#		arg_ipv6='--disable-something'
#	fi
	echo -n "Running build $((i + 1)) of $((2**7)): "
	echo -n 'configure... '
	if ! ./configure $confargs >configure-output.txt 2>&1; then
		echo 'ERROR: configure script failed'
		echo "Failed command was: ./configure $confargs >configure-output.txt 2>&1"
		break
	fi
	rm -f configure-output.txt
	echo -n 'make... '
	if ! $MAKE clean all >make-output.txt 2>&1; then
		echo 'ERROR: make failed'
		echo "Previous command was: ./configure $confargs >configure-output.txt 2>&1"
		echo "Failed command was: $MAKE clean all >make-output.txt 2>&1"
		break
	fi
	rm -f make-output.txt
	echo OK
done

