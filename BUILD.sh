#!/bin/bash

##############################################################################
# set envs
ROOT_DIR=$PWD

chmod 755 include/linux/vdlp_version*.h

##############################################################################
# Find the XXX_debug_defconfig into config folder and show the list
# The list is shown automatically. and If select the number, Build is started.
# CAUTION: Remove the files which are unnecessary anymore under arch/arm/configs.
##############################################################################
configdir=$ROOT_DIR/arch/arm/configs
echo ""
echo "================================================================"
echo " Select target for Build"
echo "================================================================"
if [ -e "$configdir" ]; then
	for entry in `ls -1 $configdir`
	do
		# find the only xxx_debug_defconfig
		if [[ "$entry" =~ "debug_defconfig" ]]; then
			cnt=$((cnt+1))
			prj=${entry%_debug_defconfig}
			echo "$cnt. $prj"
		fi
	done
fi
echo "================================================================"
read target

PRODUCT_TARGET=LOCAL
MODEL_TARGET=NONE
TOOLCHAIN_PREFIX=armv7l-tizen-linux-gnueabi-
export ARCH=arm

if [ "$target" -ne "0" ]; then
	cnt=0
	for entry in `ls -1 $configdir`
	do
		# find the only xxx_debug_defconfig
		if [[ "$entry" =~ "debug_defconfig" ]]; then
			cnt=$((cnt+1))
			if [ $cnt = $target ]; then
				MODEL_TARGET=${entry%_debug_defconfig}
				echo "================================================================"
				echo "Build $MODEL_TARGET."
				echo "================================================================"
				break
			fi
		fi
	done
fi

if [ "$MODEL_TARGET" = "NONE" ]; then
	echo "There are no target what you input $target"
	exit ;
fi

#############################################################
# MODEL_CHIP for sign
if [[ "$MODEL_TARGET" =~ "Kant.M2-Tizen" ]]; then
	MODEL_CHIP=KantM
elif [[ "$MODEL_TARGET" =~ "Kant.SU-Tizen" ]]; then
	MODEL_CHIP=KantSU
elif [[ "$MODEL_TARGET" =~ "Kant.SU2-Tizen" ]]; then
	MODEL_CHIP=KantSU2
elif [[ "$MODEL_TARGET" =~ "Kant.S-Tizen" ]]; then
	MODEL_CHIP=KantS
elif [[ "$MODEL_TARGET" =~ "Muse.M-Tizen" ]]; then
	MODEL_CHIP=MuseM
elif [[ "$MODEL_TARGET" =~ "Nike.M-Tizen" ]]; then
	MODEL_CHIP=NikeM
elif [[ "$MODEL_TARGET" =~ "iMX8M-Tizen" ]]; then
	MODEL_CHIP=iMX8MM
	DTB_BUILD=yes
	DTB_NAME=fsl-imx8mm-sbp-tizen
	export LOADADDR=0x40008000
	DTB_LOADADDR=0x43000000
else
	MODEL_CHIP=SKIP_SIGN
fi

if [[ "$MODEL_TARGET" =~ "LICENSING" ]]; then
	MODEL_CHIP=SKIP_SIGN
fi

echo "Sign Chip Type : $MODEL_CHIP"

#############################################################
# Toolchain
$TOOLCHAIN_PREFIX"gcc" --version | grep "20161213"
if [ $? -ne 0 ]; then
	echo "#########################################################"
	echo "# Your local toolchain should be updated for Tizen4.0!! #"
	echo "#########################################################"
	exit 1
fi

#############################################################
# Build Type
if [[ "$MODEL_TARGET" =~ "KASAN" ]] || [[ "$MODEL_TARGET" =~ "KUBSAN" ]]; then
	mode="3"
else
	echo "========================================================================"
	echo " Tizen Select Release Mode..."
	echo "========================================================================"
	echo " 1. release mode"
	echo " 2. perf mode"
	echo " 3. debug mode"
	echo "========================================================================"
	read mode
fi

cd $ROOT_DIR

mkdir -p $ROOT_DIR/../modules-release/modules
make distclean

if [ "$mode" == "1" ]; then
	RELEASE_MODE=release
	RELEASE_MODE_STRING=release
elif [ "$mode" == "2" ]; then
	RELEASE_MODE=perf
	RELEASE_MODE_STRING=perf
elif [ "$mode" == "3" ]; then
	RELEASE_MODE=debug
	RELEASE_MODE_STRING=debug
else
	echo "Wrong input"
	exit 1
fi

export RELEASE_MODE

rm -rf $ROOT_DIR/$MODEL_TARGET/$RELEASE_MODE_STRING
mkdir -p $ROOT_DIR/$MODEL_TARGET/$RELEASE_MODE_STRING

##############################################################################
# VERSION
cd $ROOT_DIR

echo "Input KERNEL Base CL NUMBER for Version"
read answer
VERSION=$answer

KERNEL_VERSION_MSG="$PRODUCT_TARGET, $MODEL_TARGET, $RELEASE_MODE_STRING, ONEMAIN"
echo "#define DTV_KERNEL_VERSION "\"$VERSION, $RELEASE_MODE_STRING\" > include/linux/vdlp_version.h
echo "#define DTV_LAST_PATCH "\"$KERNEL_VERSION_MSG\" >> include/linux/vdlp_version.h

echo "========================================================================"
cat ./include/linux/vdlp_version.h
echo "========================================================================"

##############################################################################
# build kernel
echo $MODEL_TARGET\_$RELEASE_MODE_STRING\_defconfig
make $MODEL_TARGET\_$RELEASE_MODE_STRING\_defconfig

echo "========================================================================"
echo "Do you want to change kernel config (y/n)?"
echo "========================================================================"

read answer

if [ $answer == "y" ]; then
	make menuconfig
elif [ $answer == "n" ]; then
	echo "Keep going"
fi

diff arch/$ARCH/configs/"$MODEL_TARGET"_"$RELEASE_MODE"_defconfig .config

echo "========================================================================"
echo " Make sure configs that you want y/n?"
echo "========================================================================"

read answer

if [ $answer == "y" ]; then
	echo " Keep going"
elif [ $answer == "n" ]; then
	./Clean.sh
	exit 1
fi

export CROSS_COMPILE=$TOOLCHAIN_PREFIX

if [ "$ARCH" == "arm" ]; then
	echo "make uImage"
	make uImage -j8 $*
	# Check uImage build
	if [ $? -ne 0 ]; then
		echo "build uImage failed. Stopped..."
		exit 1
	fi
	cp -f arch/$ARCH/boot/uImage $ROOT_DIR/$MODEL_TARGET/$RELEASE_MODE_STRING/uImage

	# dtb build
	if [ "$DTB_BUILD" == "yes" ]; then
		echo "make dtbs"
		make dtbs -j8
		if [ $? -ne 0 ]; then
			echo "build dtb failed. Stopped..."
			exit 1
		fi
		cp -f arch/$ARCH/boot/dts/$DTB_NAME.dtb $ROOT_DIR/$MODEL_TARGET/$RELEASE_MODE_STRING/dtb.bin
	fi

	# sign image
	if [[ "$MODEL_TARGET" =~ "iMX8M-Tizen" ]]; then
		cd $ROOT_DIR/scripts
		$ROOT_DIR/scripts/secos_auth.sh
		$ROOT_DIR/scripts/sign_raw_NXP.sh $ROOT_DIR/$MODEL_TARGET/$RELEASE_MODE_STRING uImage 0x60000000 0 $MODEL_CHIP 
		$ROOT_DIR/scripts/sign_raw_NXP.sh $ROOT_DIR/$MODEL_TARGET/$RELEASE_MODE_STRING dtb.bin $DTB_LOADADDR 0 $MODEL_CHIP 
		cd -
	else
		$ROOT_DIR/BUILD_sign.sh $ROOT_DIR/$MODEL_TARGET/$RELEASE_MODE_STRING/uImage $MODEL_CHIP $ROOT_DIR/$MODEL_TARGET/$RELEASE_MODE_STRING/uImage
	fi

	if [ $? -ne 0 ]; then
		echo "sign failed. Stopped..."
		exit 1
	fi

	mv vmlinux $ROOT_DIR/$MODEL_TARGET/$RELEASE_MODE_STRING/vmlinux
elif [ "$ARCH" == "arm64" ]; then
	if [ "$MODEL_TARGET" == "Qemu.64-Tizen" ]; then
		echo "make Image.gz"
		make Image.gz -j8 $*
		# Check Image.gz build
		if [ $? -ne 0 ]; then
			echo "build Image.gz failed. Stopped..."
			exit 1
		fi

		echo "make rtsm_ve-aemv8a.dtb"
		make arm/rtsm_ve-aemv8a.dtb
		if [ $? -ne 0 ]; then
			echo "make rtsm_ve-aemv8a.dtb failed. Stopped..."
			exit 1
		fi
		cp -f arch/$ARCH/boot/Image.gz $ROOT_DIR/$MODEL_TARGET/$RELEASE_MODE_STRING/Image.gz
		cp -f arch/$ARCH/boot/dts/arm/rtsm_ve-aemv8a.dtb $ROOT_DIR/$MODEL_TARGET/$RELEASE_MODE_STRING/rtsm_ve-aemv8a.dtb
	else
		make Image.gz -j8 $*
		if [ $? -ne 0 ]; then
			echo "build uImage failed. Stopped..."
			exit 1
		fi

		echo "make uImage64"
	fi
	mv vmlinux $ROOT_DIR/$MODEL_TARGET/$RELEASE_MODE_STRING/vmlinux
fi

##############################################################################
# make modules
export LIN_MAIN_VER=$(cat include/generated/autoconf.h | sed -n '/Linux\/arm/p' | awk '{ print $3 }')
echo $ROOT_DIR/../modules-release/modules/$LIN_MAIN_VER/

make modules -j8 $*
if [ $? -ne 0 ]; then
	echo "build modules failed. Stopped..."
	exit 1
fi

make modules_install INSTALL_MOD_STRIP=1 INSTALL_MOD_PATH=$ROOT_DIR/../modules-release/modules $*
rm -f $ROOT_DIR/../modules-release/modules/lib/modules/$LIN_MAIN_VER/build
rm -f $ROOT_DIR/../modules-release/modules/lib/modules/$LIN_MAIN_VER/source
if [ $? -ne 0 ]; then
	echo "modules_install failed. Stopped..."
	exit 1
fi

#############################################################################
# add module config files
rm -f $ROOT_DIR/../modules-release/modules/lib/modules/$LIN_MAIN_VER/modules.*
cp -f $ROOT_DIR/../modules-release/modules.* $ROOT_DIR/../modules-release/modules/lib/modules/$LIN_MAIN_VER/

#############################################################################
# add link file to exact modules 
cd $ROOT_DIR/../modules-release/modules/lib/modules/
ln -s ./$LIN_MAIN_VER linux
if [ $? -ne 0 ]; then
	echo "create symbolic link failed. Stopped..."
	exit 1
fi

# KUEP
#find . -name "*.ko" -exec curl -w %{http_code}  -f -F "file=@{}" -F "year=2017" -o {}  http://10.40.68.68/isms/open/masterkey/signKUEP.do \;
#

##############################################################################
# package modules
/bin/tar zcvfh $ROOT_DIR/../modules-release/kmod.tgz $LIN_MAIN_VER linux
rm -f linux
cp -f $ROOT_DIR/../modules-release/kmod.tgz $ROOT_DIR/$MODEL_TARGET/$RELEASE_MODE_STRING/
cd $ROOT_DIR

##############################################################################
# rootfs
#cp  $ROOT_DIR/../$MODEL_TARGET/$RELEASE_MODE_STRING/rootfs.img $ROOT_DIR/$MODEL_TARGET/$RELEASE_MODE_STRING/

##############################################################################
# Image
echo "##################################################################"
echo " $MODEL_TARGET  $RELEASE_MODE_STRING"
echo "##################################################################"
ls -alh $ROOT_DIR/$MODEL_TARGET/$RELEASE_MODE_STRING/
echo "##################################################################"


echo "##################################################################"
echo "Do you want to copy build result y/n "
echo "##################################################################"

read answer
if [ $answer == "y" ]; then
	if [ ! -d ../$MODEL_TARGET/image/$RELEASE_MODE ]; then
		mkdir -p ../$MODEL_TARGET/image/$RELEASE_MODE
	fi
	cp -f $MODEL_TARGET/$RELEASE_MODE/* ../$MODEL_TARGET/image/$RELEASE_MODE/
else
	echo "do nothing"
fi


echo "##################################################################"
echo "Do you want to clean result y/n "
echo "##################################################################"

read answer
if [ $answer == "y" ]; then
	rm -rf $MODEL_TARGET
	rm -rf ../modules-release/kmod.tgz
	rm -rf ../modules-release/modules
else
	echo "leave all"
fi

echo "##################################################################"
echo "Do you want to save config to default config y/n"
echo "##################################################################"
read answer
if [ $answer == "y" ]; then
	cp -f .config  arch/$ARCH/configs/"$MODEL_TARGET"_"$RELEASE_MODE"_defconfig
else
	echo "do nothing"
fi

echo "##################################################################"
echo "Do you want make user header files y/n ?"
echo "##################################################################"

read answer
if [ $answer == "y" ]; then
	./BUILD_header.sh
else
	echo "keep going"
fi

echo "##################################################################"
echo "Do you want to Do Clean.sh y/n"
echo "##################################################################"
read answer
if [ $answer == "y" ]; then
	./Clean.sh
else
	echo "do nothing"
fi

echo "BUILD DONE"

