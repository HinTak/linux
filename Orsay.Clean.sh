#!/bin/bash 

ROOT_DIR=$PWD

test -f .config
if [ $? == 0 ]
then
	echo "Distclean......"
	make distclean
fi

cd $ROOT_DIR

rm -f .product
rm -f .perf
rm -f .debug
rm -f .kasan
rm -f .PreHawk
rm -f .PreHawk_AV
rm -f .HawkP
rm -f .HawkP_TV
rm -f .HawkM
rm -f .HAWKA

rm -f ./include/linux/vdlp_version.h
rm -f Image
rm -f uImage uImage_US_1MP
rm -f rootfs.img
rm -f dtb.bin
rm -f dtb.uhd.bin
rm -f dtb.fhd.bin

rm -rf ../KO

rm -f scripts/rsa_tool/rsa_tool

cd $ROOT_DIR


