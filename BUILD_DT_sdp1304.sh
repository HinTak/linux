#!/bin/bash

ROOT_DIR=$PWD

cd $ROOT_DIR
export ARCH=arm
export CROSS_COMPILE=arm-v7a15v4r3-linux-gnueabi-

if [ -e .config ]
then
	make oldconfig
else
	cp arch/arm/configs/sdp_defconfig .config
fi
make uImage -j16
make sdp1304-eval-uhd.dtb
make sdp1304-eval-fhd.dtb
make sdp1304-eval-us-1mp.dtb
make sdp1304-dtv-uhd.dtb
make sdp1304-dtv-fhd.dtb
make sdp1304-dtv-us-1mp.dtb

VER=`strings arch/arm/boot/Image | grep "Linux version" | cut -d ' ' -f3`

cp arch/arm/boot/uImage .
cp arch/arm/boot/dts/sdp1304-eval-uhd.dtb ./sdp.dtb
cp arch/arm/boot/dts/sdp1304-eval-fhd.dtb ./sdp-fhd.dtb
cp arch/arm/boot/dts/sdp1304-eval-us-1mp.dtb ./sdp-us-1mp.dtb
cp arch/arm/boot/dts/sdp1304-dtv-fhd.dtb ./sdp-dtv-fhd.dtb
cp arch/arm/boot/dts/sdp1304-dtv-us-1mp.dtb ./sdp-dtv-us-1mp.dtb
