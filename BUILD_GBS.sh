#!/bin/bash

##############################################################################
# set envs
ROOT_DIR=$PWD

##############################################################################
# Default environment

CORE_TYPE=ARM
TOOLCHAIN_PREFIX=""
##############################################################################

PRODUCT_TARGET=$PRODUCT_TYPE

if [ $# != 3 ]; then
	echo "Failed because input is not enough. Stopped..."
	exit
else
# get the PROJECT and RELEASE MODE and sign chip name from spec file.
# PROJECT string should be matched with kernel config and output folder name.
# ex) PROJECT = Muse.M-Tizen , config name = Muse.M-Tizen_xxx_defconfig.

	MODEL_TARGET=$1
	RELEASE_MODE=$2
	MODEL_CHIP=$3	#sign type.

	echo "PRODUCT_TARGET=$PRODUCT_TARGET, MODEL_TARGET=$MODEL_TARGET, MODEL_CHIP=$MODEL_CHIP"
fi

mkdir -p $ROOT_DIR/../modules-release/modules
make distclean

RELEASE_MODE_STRING=$RELEASE_MODE

if [[ "$MODEL_TARGET" =~ "LICENSING" ]]; then
	echo "Licensing model"
	MODEL_CHIP=SKIP_SIGN
fi

# dtb build.
if [ "$CHIP_NAME" = "CHIP_iMX8M" ]; then
	DTB_BUILD=yes
	if [ "$MODEL_TARGET" == "iMX8M-Tizen" ]; then
		DTB_NAME=fsl-imx8mm-sbp-tizen
		export LOADADDR=0x40008000
		export DTB_LOADADDR=0x43000000
	fi
fi

##############################################################################
# VERSION
VCS=$(cat "/home/abuild/rpmbuild/SOURCES/tztv-kernel-drivers.spec" | grep "VCS:")
VERSION=$(echo $VCS | cut -d'#' -f2)
VERSION=${VERSION##*"-0-"}
BRANCH=$(echo $VCS | grep "MAIN")
echo "VCS=$VCS"
if [ ${#VERSION} -gt 7 ]; then
	VERSION=$(cat "version.txt"|grep -v '^#'|grep -v '^[ \t\n]*$')
	BRANCH="LOCAL"
else
	if [ -z "$BRANCH"]; then
		BRANCH=$(echo $VCS|cut -d'/' -f5)
	else
		BRANCH=$(echo $VCS|cut -d'/' -f4)
	fi
	BRANCH=$(echo $BRANCH | tr -d '[]')
	BRANCH=${BRANCH%%_Prj}
fi	
KERNEL_VERSION_MSG="$PRODUCT_TARGET, $MODEL_TARGET, $RELEASE_MODE_STRING, $BRANCH"
echo "#define DTV_KERNEL_VERSION "\"$VERSION, $RELEASE_MODE_STRING\" > include/linux/vdlp_version.h
echo "#define DTV_LAST_PATCH "\"$KERNEL_VERSION_MSG\" >> include/linux/vdlp_version.h

##############################################################################
# Create header file
#cd $ROOT_DIR
#echo "Creating Symbolic Link [vdlp_version.h -> vdlp_version_"$MODEL_TARGET".h]"
#test -f include/linux/vdlp_version_$MODEL_TARGET.h
#if [ $? == 0 ]; then
#	cd ./include/linux
#	rm -Rf vdlp_version.h
#	ln -s vdlp_version_$MODEL_TARGET.h vdlp_version.h
#else
#	echo "vdlp_version_"$MODEL_TARGET".h not exist!"
#	exit
#fi

cd $ROOT_DIR

find ./include/linux/ -name "vdlp_version.h" | xargs perl -pi -e 's/release/'$RELEASE_MODE_STRING'/g'
find ./include/linux/ -name "vdlp_version.h" | xargs perl -pi -e 's/debug/'$RELEASE_MODE_STRING'/g'
find ./include/linux/ -name "vdlp_version.h" | xargs perl -pi -e 's/perf/'$RELEASE_MODE_STRING'/g'

echo =======================================================================================
cat ./include/linux/vdlp_version.h
echo =======================================================================================

##############################################################################
# build kernel
echo $MODEL_TARGET\_$RELEASE_MODE_STRING\_defconfig
make $MODEL_TARGET\_$RELEASE_MODE_STRING\_defconfig

mkdir -p $ROOT_DIR/../$MODEL_TARGET/image/$RELEASE_MODE_STRING
mkdir -p $ROOT_DIR/../modules-release/modules

export CROSS_COMPILE=$TOOLCHAIN_PREFIX

if [ "$MODEL_TARGET" == "Kant.M64-Tizen" ]; then
	export ARCH=arm64
	echo "make Image.gz"
	make Image.gz -j
	# Check Image.gz build
	if [ $? -ne 0 ]; then
		echo "build Image.gz failed. Stopped..."
		exit 1
	fi

	cp -f arch/$ARCH/boot/Image.gz $ROOT_DIR/../$MODEL_TARGET/image/$RELEASE_MODE_STRING/uImage
else
	export ARCH=arm
	echo "make uImage"
	make uImage -j
	# Check uImage build
	if [ $? -ne 0 ]; then
		echo "build uImage failed. Stopped..."
		exit 1
	fi
	cp -f arch/$ARCH/boot/uImage $ROOT_DIR/../$MODEL_TARGET/image/$RELEASE_MODE_STRING/

	# dtb build
	if [ "$DTB_BUILD" == "yes" ]; then
		echo "make dtbs"
		make dtbs
		if [ $? -ne 0 ]; then
			echo "build dtb failed. Stopped..."
			exit 1
		fi
		cp -f arch/$ARCH/boot/dts/$DTB_NAME.dtb $ROOT_DIR/../$MODEL_TARGET/image/$RELEASE_MODE_STRING/dtb.bin
	fi

	# sign image
	if [[ "$MODEL_TARGET" == "iMX8M-Tizen" ]]; then
		cd $ROOT_DIR/scripts
		$ROOT_DIR/scripts/secos_auth.sh
		$ROOT_DIR/scripts/sign_raw_NXP.sh $ROOT_DIR/../$MODEL_TARGET/image/$RELEASE_MODE_STRING uImage 0x60000000 0 $MODEL_CHIP
		$ROOT_DIR/scripts/sign_raw_NXP.sh $ROOT_DIR/../$MODEL_TARGET/image/$RELEASE_MODE_STRING dtb.bin $DTB_LOADADDR 0 $MODEL_CHIP
		cd -
	else
		$ROOT_DIR/BUILD_sign.sh $ROOT_DIR/../$MODEL_TARGET/image/$RELEASE_MODE_STRING/uImage $MODEL_CHIP $ROOT_DIR/../$MODEL_TARGET/image/$RELEASE_MODE_STRING/uImage
	fi

	if [ $? -ne 0 ]; then
		echo "sign failed. Stopped..."
		exit 1
	fi

fi

mv vmlinux $ROOT_DIR/../$MODEL_TARGET/image/$RELEASE_MODE_STRING/vmlinux

##############################################################################
# make modules
export LIN_MAIN_VER=$(cat include/generated/autoconf.h | sed -n '/Linux\/arm/p' | awk '{ print $3 }')
#echo $ROOT_DIR/../modules-release/modules/$LIN_MAIN_VER/

make modules -j
if [ $? -ne 0 ]; then
	echo "build modules failed. Stopped..."
	exit 1
fi

make modules_install INSTALL_MOD_PATH=$ROOT_DIR/../modules-release/modules
rm -f $ROOT_DIR/../modules-release/modules/lib/modules/$LIN_MAIN_VER/build
rm -f $ROOT_DIR/../modules-release/modules/lib/modules/$LIN_MAIN_VER/source
if [ $? -ne 0 ]; then
	echo "modules_install failed. Stopped..."
	exit 1
fi

##############################################################################
# package modules
cd $ROOT_DIR/../modules-release/modules/lib/modules/
/bin/tar zcvfh $ROOT_DIR/../modules-release/kmod.tgz $LIN_MAIN_VER
cp -f $ROOT_DIR/../modules-release/kmod.tgz $ROOT_DIR/../$MODEL_TARGET/image/$RELEASE_MODE_STRING/
cd $ROOT_DIR

##############################################################################
# rootfs
#cp  $ROOT_DIR/../$MODEL_TARGET/$RELEASE_MODE_STRING/rootfs.img $ROOT_DIR/$MODEL_TARGET/$RELEASE_MODE_STRING/

##############################################################################
# make kernel header for user
#export VD_KERNEL_HEADERS_PATH=$ROOT_DIR/../$MODEL_TARGET/image/$RELEASE_MODE_STRING/

#if [ -d $VD_KERNEL_HEADERS_PATH ]; then
#	make ARCH=arm INSTALL_HDR_PATH=$VD_KERNEL_HEADERS_PATH headers_install
#	rm -f `find $VD_KERNEL_HEADERS_PATH -name "*.cmd"`
#	rm -f `find $VD_KERNEL_HEADERS_PATH -name "*.install"`
#	cd $ROOT_DIR/../$MODEL_TARGET/image/$RELEASE_MODE_STRING/
#	tar zcvf user_headers_install.tgz include
#	rm -rf include
#	cd -
#else
#	echo "$VD_KERNEL_HEADERS_PATH not exists. skip makeing kernel user headers."
#fi

echo "#######################################################################"
echo " BUILD END"
echo "#######################################################################"
