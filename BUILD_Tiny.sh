#!/bin/bash

##############################################################################
# set envs
ROOT_DIR=$PWD
CONFIG_DIR=arch/arm/configs

chmod 755 include/linux/vdlp_version_*.h

##############################################################################
# select target

echo ""
echo " BSP Tiny Kernel Build Process"
echo ""
echo "================================================================"
echo " Select target"
echo "================================================================"
echo " 2. NT16M-TIZEN"
echo "================================================================"

while true
do
	echo -n " Enter target number : "
	read target
	if [ "$target" == "2" ]
	then
		PRODUCT_TARGET=DTV
		MODEL_TARGET=NT16M-Tizen
		CORE_TYPE=ARM
		TOOLCHAIN_PREFIX=armv7l-tizen-linux-gnueabi-
		break
	else
		echo " Wrong input"
	fi
done

##############################################################################
# select build mode

echo ""
echo "========================================================================"
echo " Select Build Mode..."
echo "========================================================================"
echo " 1. debug mode"
echo " 2. perf mode"
echo " 3. release mode"
echo "========================================================================"

while true
do
	echo -n " Enter build mode number : "
	read mode
	if [ "$mode" == "1" ]
	then
		RELEASE_DEBUG_CHECK=debug
		RELEASE_MODE=debug
		RELEASE_MODE_STRING=debug
		break
	elif [ "$mode" == "2" ]
	then
		RELEASE_DEBUG_CHECK=perf
		RELEASE_MODE=perf
		RELEASE_MODE_STRING=perf
		break
	elif [ "$mode" == "3" ]
	then
		RELEASE_DEBUG_CHECK=release
		RELEASE_MODE=release
		RELEASE_MODE_STRING=release
		break
	else
		echo " Wrong input"
	fi
done

echo ""

##############################################################################
# clean to prepare to build

echo "Running Clean.sh"
./Clean.sh

##############################################################################
# setup kernel build environment

echo "Creating Symbolic Link [vdlp_version.h -> vdlp_version_"$MODEL_TARGET".h]"
test -f include/linux/vdlp_version_"$MODEL_TARGET".h
if [ $? == 0 ]
then
	cd ./include/linux
	rm -Rf vdlp_version.h
	ln -s vdlp_version_"$MODEL_TARGET".h vdlp_version.h
else
	echo "vdlp_version_"$MODEL_TARGET".h not exist!"
	exit 1
fi

cd $ROOT_DIR

find ./include/linux/ -name "vdlp_version.h" | xargs perl -pi -e 's/product/'$RELEASE_MODE'/g'
find ./include/linux/ -name "vdlp_version.h" | xargs perl -pi -e 's/debug/'$RELEASE_MODE'/g'
find ./include/linux/ -name "vdlp_version.h" | xargs perl -pi -e 's/perf/'$RELEASE_MODE'/g'

echo "========================================================================"
cat ./include/linux/vdlp_version.h
echo "========================================================================"

##############################################################################
# config file check

CONFIG_FILE="$CONFIG_DIR"/"$MODEL_TARGET"_tinykernel_"$RELEASE_MODE"_defconfig

if [ ! -f "$CONFIG_FILE" ]
then
	echo "Error : "$CONFIG_FILE" not exist...."
	exit 1
fi

echo ""
echo "Copying "$CONFIG_FILE" to .config"
cp -f "$CONFIG_FILE" .config | exit 1

echo ""
echo -n "Do you want to change kernel configs? [y/n] : "
read answer
echo ""
if [ "$answer" == "y" ]
then
	make menuconfig
else
	make oldconfig
fi

##############################################################################
# config file check

echo ""
echo "Running \"diff "$CONFIG_FILE" .config\""

DIFF_RESULT=$(diff "$CONFIG_FILE" .config)
if [ "$DIFF_RESULT" == "" ]
then
	DIFF_RESULT="  None : Two files are identical."
fi

echo ""
echo "#################################################################################"
echo "# Kernel Config Diff Result"
echo "================================================================================="
echo "$DIFF_RESULT"
echo "================================================================================="

echo -n "Make sure kernel configs that you want? [y/n] : "
read answer
if [ "$answer" != "y" ]
then
	exit 1
fi

##############################################################################
# build kernel

echo ""
echo -n "Do you want to build kernel? [y/n] : "
read answer
if [ "$answer" != "y" ]
then
	exit 1
fi

echo ""
echo "Start to build kernel"

export ARCH=arm
export CROSS_COMPILE=$TOOLCHAIN_PREFIX
export SERET_DIR=../../seret4_UL/SERET-UL/

if [ "$MODEL_TARGET" == "NT16M-Tizen" ]
then
	rm -rf ../seret_root
	mkdir ../seret_root | exit 1
	grep -v "PREFIX" $SERET_DIR/kernel/initramfs.in > ../seret_root/initramfs.in
	make -j8 $*
	if [ $? -ne 0 ]; then
		echo "build uImage failed. Stopped..."
		exit 1
	fi
else
	echo "Error : "$MODEL_TARGET"'s kernel build not defined"
	exit 1
fi

# make modules -j8 $*
# if [ $? -ne 0 ]; then
# 	echo "build modules failed. Stopped..."
# 	exit 1
# fi

echo ""
echo "Kernel Build OK..."
echo ""

echo "##################################################################"
echo "Do you want to save config? [y/n]"
echo "##################################################################"
read answer
if [ "$answer" == "y" ]
then
	cp -f .config "$CONFIG_FILE" | exit 1
fi

./Clean.sh
rm -rf ../seret_root

echo ""
echo "TINY KERNEL BUILD DONE : "$MODEL_TARGET", "$RELEASE_MODE""
