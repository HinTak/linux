#!/bin/bash

TARGET=$1

ConfigProc()
{
	DEFCONFIG="$1"_"$2"_defconfig

	echo ""
	echo "==========================================================="
	echo "$1 $2"
	echo "==========================================================="
	echo ""
	echo "cp -af arch/arm/configs/$DEFCONFIG .config"
	cp -f arch/arm/configs/$DEFCONFIG .config
	if [ $? != 0 ]; then
		echo "[Error] The file is not existed: arch/arm/configs/$DEFCONFIG"
		exit 1
	fi

	echo ""
	echo -n "Do you want to change kernel configs? [y/n] : "
	read answer
	echo ""
	if [ "$answer" = "y" ]; then
		make menuconfig
	else
		make oldconfig
	fi

	echo ""
	echo "diff .config arch/arm/configs/$DEFCONFIG"
	echo "==========================================================="
	diff .config arch/arm/configs/$DEFCONFIG
	echo "==========================================================="

	echo ""
	echo -n "Do you want to update $DEFCONFIG? [y/n] : "
	read answer
	echo ""
	if [ "$answer" = "y" ]; then
		echo "cp -f .config arch/arm/configs/$DEFCONFIG"
		cp -f .config arch/arm/configs/$DEFCONFIG
	fi

	echo ""
	make distclean
}

if [ -z "$1" ]; then
	echo "./BUILD_Config.sh TargetName"
	exit 1
fi

echo "============================================"
echo "Configuring $TARGET  debug kernel config    "
echo "============================================"
ConfigProc $TARGET debug

echo "============================================"
echo "Configuring $TARGET  perf kernel config     "
echo "============================================"
ConfigProc $TARGET perf

echo "============================================"
echo "Configuring $TARGET  release kernel config  "
echo "============================================"
ConfigProc $TARGET release



echo "==========================================="
echo " End of kernel configuration               "
echo "==========================================="

