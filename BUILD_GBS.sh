#!/bin/bash

##############################################################################
# set envs
ROOT_DIR=$PWD

##############################################################################
# VERSION

##############################################################################
# Default environment

CORE_TYPE=ARM
TOOLCHAIN_PREFIX=""
##############################################################################

if [ "$1" == "2" ]; then
	PRODUCT_TARGET=DTV
	MODEL_TARGET=Hawk.P-Tizen
elif [ "$1" == "3" ]; then
	PRODUCT_TARGET=DTV
	MODEL_TARGET=Golf.P-Tizen
elif [ "$1" == "4" ]; then
	PRODUCT_TARGET=DTV
	MODEL_TARGET=Hawk.M-Tizen
elif [ "$1" == "5" ]; then
	PRODUCT_TARGET=DTV
	MODEL_TARGET=Echo.P-Tizen
elif [ "$1" == "6" ]; then
	PRODUCT_TARGET=DTV
	MODEL_TARGET=Fox.P-Tizen
elif [ "$1" == "7" ]; then
	PRODUCT_TARGET=BD
	MODEL_TARGET=Hawk.P-Tizen_BD
elif [ "$1" == "9" ]; then
	PRODUCT_TARGET=SBB
	MODEL_TARGET=Hawk.P-Tizen_SBB
elif [ "$1" == "11" ]; then
	PRODUCT_TARGET=AV
	MODEL_TARGET=Hawk.A-Tizen
elif [ "$1" == "12" ]; then
	PRODUCT_TARGET=DTV
	MODEL_TARGET=NT16M-Tizen
elif [ "$1" == "21" ]; then
	PRODUCT_TARGET=DTV
	MODEL_TARGET=Jazz.M-Tizen
else
	echo "Wrong input"
	TARGET_INPUT_ERR=$TRUE
fi

mkdir -p $ROOT_DIR/../modules-release/modules
make distclean

if [ "$2" == "1" ]; then
	RELEASE_DEBUG_CHECK=release
	RELEASE_MODE=release
	RELEASE_MODE_STRING=release
elif [ "$2" == "2" ]; then
	RELEASE_DEBUG_CHECK=perf
	RELEASE_MODE=perf
	RELEASE_MODE_STRING=perf
elif [ "$2" == "3" ]; then
	RELEASE_DEBUG_CHECK=debug
	RELEASE_MODE=debug
	RELEASE_MODE_STRING=debug
fi

##############################################################################
# Create header file
cd $ROOT_DIR
echo "Creating Symbolic Link [vdlp_version.h -> vdlp_version_"$MODEL_TARGET".h]"
test -f include/linux/vdlp_version_$MODEL_TARGET.h
if [ $? == 0 ]; then
	cd ./include/linux
	rm -Rf vdlp_version.h
	ln -s vdlp_version_$MODEL_TARGET.h vdlp_version.h
else
	echo "vdlp_version_"$MODEL_TARGET".h not exist!"
	exit
fi

cd $ROOT_DIR

find ./include/linux/ -name "vdlp_version.h" | xargs perl -pi -e 's/release/'$RELEASE_MODE'/g'
find ./include/linux/ -name "vdlp_version.h" | xargs perl -pi -e 's/debug/'$RELEASE_MODE'/g'
find ./include/linux/ -name "vdlp_version.h" | xargs perl -pi -e 's/perf/'$RELEASE_MODE'/g'

echo =======================================================================================
cat ./include/linux/vdlp_version.h
echo =======================================================================================

##############################################################################
# build kernel
echo $MODEL_TARGET\_$RELEASE_MODE_STRING\_defconfig
make $MODEL_TARGET\_$RELEASE_MODE_STRING\_defconfig

export ARCH=arm
export CROSS_COMPILE=$TOOLCHAIN_PREFIX

mkdir -p $ROOT_DIR/../$MODEL_TARGET/image/$RELEASE_MODE_STRING
mkdir -p $ROOT_DIR/../modules-release/modules

	make uImage -j

#  Check uImage build
if [ $? -ne 0 ]; then
	echo "build uImage failed. Stopped..."
	exit 1
fi
cp -f arch/arm/boot/uImage $ROOT_DIR/../$MODEL_TARGET/image/$RELEASE_MODE_STRING/

##############################################################################
# dtb

	echo "Wrong input"
	TARGET_INPUT_ERR=$TRUE

mv vmlinux $ROOT_DIR/../$MODEL_TARGET/image/$RELEASE_MODE_STRING/vmlinux

##############################################################################
# make modules

make modules -j
if [ $? -ne 0 ]; then
	echo "build modules failed. Stopped..."
	exit 1
fi

make modules_install INSTALL_MOD_STRIP=1 INSTALL_MOD_PATH=$ROOT_DIR/../modules-release/modules
rm -f $ROOT_DIR/../modules-release/modules/lib/modules/3.10.30/build
rm -f $ROOT_DIR/../modules-release/modules/lib/modules/3.10.30/source
if [ $? -ne 0 ]; then
	echo "modules_install failed. Stopped..."
	exit 1
fi

#############################################################################
# add module config files
rm -f $ROOT_DIR/../modules-release/modules/lib/modules/3.10.30/modules.*
cp $ROOT_DIR/../modules-release/modules.* $ROOT_DIR/../modules-release/modules/lib/modules/3.10.30/


##############################################################################
# package modules
cd $ROOT_DIR/../modules-release/modules/lib/modules/
/bin/tar zcvf $ROOT_DIR/../modules-release/kmod.tgz 3.10.30
cp -f $ROOT_DIR/../modules-release/kmod.tgz $ROOT_DIR/../$MODEL_TARGET/image/$RELEASE_MODE_STRING/
cd $ROOT_DIR

##############################################################################
# rootfs
#cp  $ROOT_DIR/../$MODEL_TARGET/$RELEASE_MODE_STRING/rootfs.img $ROOT_DIR/$MODEL_TARGET/$RELEASE_MODE_STRING/

##############################################################################
# make kernel header for user
export VD_KERNEL_HEADERS_PATH=$ROOT_DIR/../$MODEL_TARGET/image/$RELEASE_MODE_STRING/

if [ -d $VD_KERNEL_HEADERS_PATH ]; then
	make ARCH=arm INSTALL_HDR_PATH=$VD_KERNEL_HEADERS_PATH headers_install
	rm -f `find $VD_KERNEL_HEADERS_PATH -name "*.cmd"`
	rm -f `find $VD_KERNEL_HEADERS_PATH -name "*.install"`
	cd $ROOT_DIR/../$MODEL_TARGET/image/$RELEASE_MODE_STRING/
	tar zcvf user_headers_install.tgz include
	rm -rf include
	cd -
else
	echo "$VD_KERNEL_HEADERS_PATH not exists. skip makeing kernel user headers."
fi

echo "#######################################################################"
echo " BUILD END"
echo "#######################################################################"
