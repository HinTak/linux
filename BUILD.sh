#!/bin/bash

##############################################################################
# set envs
ROOT_DIR=$PWD

chmod 755 include/linux/vdlp_version*.h

##############################################################################
# make signature
function make_signature()
{
	cookie=~/cookies.txt

	# delete cookie file if it is expired (24hr)
	find $cookie -type f -mtime +0 -delete
	if [ ! -f $cookie ];then
		echo "no cookies"
		./CheckCookieISMS.sh
	fi

	echo "input = $1, target = $2, output = $3"

	if [ "$3" == ""  ]
	then
		echo "[Signature] not enough parameters"
		return
	fi

	hash=$(openssl sha1 $1 | cut -d " " -f 2)
	echo "hash: [$hash]"
	sign_hash=$(curl -b $cookie -F "file=@$1" -F "hash=$hash" -F "target=$2" -F "mode=sign" -o \
		$ROOT_DIR/uImage http://10.40.68.68/isms/open/masterkey/secosbin.do --noproxy 10.40.68.68 -D - \
		| grep hash: | sed 's/hash://' | tr -d " \r")
	sign_hashgenerate=$(openssl sha1 $ROOT_DIR/uImage | cut -d " " -f 2)
	echo "Sign_hashgen: [$sign_hashgenerate]"
	echo "Sign_hash : [$sign_hash]"

	if [ -s $3  ];then
		echo Input file exists.
		if [ "$sign_hashgenerate" = "$sign_hash" ]; then
			echo "Hash is same"
			cp $ROOT_DIR/uImage $3
		elif [ "$sign_hash" = "" ]; then
			echo "Hash is NULL. Skipped."
		else
			echo "Hash is different."
			exit
		fi
	else
		echo "Input file does not exist."
		echo "Signing is skipped"
	fi
}

##############################################################################

echo ""
echo " BSP Release Process"
echo "================================================================"
echo " Select target for Release"
echo "================================================================"
echo "  1. Hawk.A-TIZEN"
echo "  2. Kant.M2-TIZEN"
echo "  3. Kant.SU-TIZEN"
echo "  4. Muse.M-TIZEN"
echo "================================================================"
echo " 11. Kant.M-TIZEN"
echo " 14. Kant.M-TIZEN-EP"
echo " 15. MT8516-TIZEN"
echo " 16. MT8516-TIZEN-SAT"
echo "================================================================"
echo " 21. Kant.M2-TIZEN-KASAN"
echo " 22. Kant.M2-TIZEN-KUBSAN"
echo "================================================================"
echo "================================================================"
echo " 23. Muse.M-TIZEN-KASAN"
echo " 24. Muse.M-TIZEN-KUBSAN"
echo "================================================================"
read target

###################################################

if [ "$target" == "1" ]; then
	PRODUCT_TARGET=AV
	MODEL_CHIP=HawkA
	MODEL_TARGET=Hawk.A-Tizen
	TOOLCHAIN_PREFIX=armv7l-tizen-linux-gnueabi-
	export ARCH=arm
	$TOOLCHAIN_PREFIX"gcc" --version | grep "Cortex-A15.Cortex-A7/Standalone-20170830"
	if [ $? -ne 0 ]; then
		echo "####################################################"
		echo "# Your local Toolchain should be used for HawkA !!!#"
		echo "####################################################"
		exit 1
	fi
elif [ "$target" == "2" ]; then
	PRODUCT_TARGET=DTV
	MODEL_CHIP=KantM
	MODEL_TARGET=Kant.M2-Tizen
	TOOLCHAIN_PREFIX=armv7l-tizen-linux-gnueabi-
	export ARCH=arm
elif [ "$target" == "3" ]; then
	PRODUCT_TARGET=DTV
	MODEL_CHIP=KantSU
	MODEL_TARGET=Kant.SU-Tizen
	TOOLCHAIN_PREFIX=armv7l-tizen-linux-gnueabi-
	export ARCH=arm
elif [ "$target" == "4" ]; then
	PRODUCT_TARGET=DTV
	MODEL_CHIP=MuseM
	MODEL_TARGET=Muse.M-Tizen
	TOOLCHAIN_PREFIX=armv7l-tizen-linux-gnueabi-
	export ARCH=arm
elif [ "$target" == "11" ]; then
	PRODUCT_TARGET=DTV
	MODEL_CHIP=KantM
	MODEL_TARGET=Kant.M-Tizen
	TOOLCHAIN_PREFIX=armv7l-tizen-linux-gnueabi-
	export ARCH=arm
elif [ "$target" == "14" ]; then
	PRODUCT_TARGET=EP
	MODEL_CHIP=KantM
	MODEL_TARGET=Kant.M-Tizen-EP
	TOOLCHAIN_PREFIX=armv7l-tizen-linux-gnueabi-
	export ARCH=arm
elif [ "$target" == "15" ]; then
	PRODUCT_TARGET=AV
	MODEL_CHIP=MT8516
	MODEL_TARGET=MT8516-Tizen
	TOOLCHAIN_PREFIX=armv7l-tizen-linux-gnueabi-
	export ARCH=arm
elif [ "$target" == "16" ]; then
	PRODUCT_TARGET=AV
	MODEL_CHIP=MT8516
	MODEL_TARGET=MT8516-Tizen-SAT
	TOOLCHAIN_PREFIX=armv7l-tizen-linux-gnueabi-
	export ARCH=arm
elif [ "$target" == "21" ]; then
	PRODUCT_TARGET=DTV
	MODEL_CHIP=KantM
	MODEL_TARGET=Kant.M2-Tizen-KASAN
	TOOLCHAIN_PREFIX=armv7l-tizen-linux-gnueabi-
	export ARCH=arm
elif [ "$target" == "22" ]; then
	PRODUCT_TARGET=DTV
	MODEL_CHIP=KantM
	MODEL_TARGET=Kant.M2-Tizen-KUBSAN
	TOOLCHAIN_PREFIX=armv7l-tizen-linux-gnueabi-
	export ARCH=arm
elif [ "$target" == "23" ]; then
	PRODUCT_TARGET=DTV
	MODEL_CHIP=MuseM
	MODEL_TARGET=Muse.M-Tizen-KASAN
	TOOLCHAIN_PREFIX=armv7l-tizen-linux-gnueabi-
	export ARCH=arm
elif [ "$target" == "24" ]; then
	PRODUCT_TARGET=DTV
	MODEL_CHIP=MuseM
	MODEL_TARGET=Muse.M-Tizen-KUBSAN
	TOOLCHAIN_PREFIX=armv7l-tizen-linux-gnueabi-
	export ARCH=arm
else
	echo "Wrong input"
	TARGET_INPUT_ERR=$TRUE
	exit 1
fi

$TOOLCHAIN_PREFIX"gcc" --version | grep "20161213"
if [ $? -ne 0 ]; then
	echo "#########################################################"
	echo "# Your local toolchain should be updated for Tizen4.0!! #"
	echo "#########################################################"
	exit 1
fi


if [ "$target" == "21" ] || [ "$target" == "22" ]; then
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
	RELEASE_DEBUG_CHECK=release
	RELEASE_MODE=release
	RELEASE_MODE_STRING=release
elif [ "$mode" == "2" ]; then
	RELEASE_DEBUG_CHECK=perf
	RELEASE_MODE=perf
	RELEASE_MODE_STRING=perf
elif [ "$mode" == "3" ]; then
	RELEASE_DEBUG_CHECK=debug
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

KERNEL_VERSION_MSG="$PRODUCT_TARGET, $MODEL_TARGET, $RELEASE_MODE_STRING, MAIN2019"
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
	make_signature $ROOT_DIR/$MODEL_TARGET/$RELEASE_MODE_STRING/uImage $MODEL_CHIP $ROOT_DIR/$MODEL_TARGET/$RELEASE_MODE_STRING/uImage
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

