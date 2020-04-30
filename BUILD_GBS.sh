#!/bin/bash

##############################################################################
# set envs
ROOT_DIR=$PWD

##############################################################################
# make signature
function make_signature()
{
	cookie=~/cookies.txt
	if [ ! -f $cookie ];then
		echo "no cookies"
		./CheckCookieISMS.sh
	fi

	if [ "$3" == ""  ]
	then
		echo "[Signature] not enough parameters"
		return
	fi

	echo "input = $1, target = $2, output = $3"
	hash=$(openssl sha1 $1 | cut -d " " -f 2)
	echo "hash: [$hash]"
	sign_hash=$(curl -b $cookie -F "file=@$1" -F "hash=$hash" -F "target=$2" -F "mode=sign" -o $ROOT_DIR/uImage http://10.40.68.68/isms/open/masterkey/secosbin.do --noproxy 10.40.68.68 -D - | grep hash: | sed 's/hash://' | tr -d " \r")
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
# Default environment

CORE_TYPE=ARM
TOOLCHAIN_PREFIX=""
##############################################################################

if [ "$1" == "1" ]; then
	PRODUCT_TARGET=AV
	MODEL_TARGET=Hawk.A-Tizen
	MODEL_CHIP=HawkA
elif [ "$1" == "2" ]; then
	PRODUCT_TARGET=DTV
	MODEL_TARGET=Kant.M2-Tizen
	MODEL_CHIP=KantM
elif [ "$1" == "21" ]; then
        PRODUCT_TARGET=DTV
	MODEL_TARGET=Kant.M2-Tizen-KASAN
        MODEL_CHIP=KantM
elif [ "$1" == "22" ]; then
        PRODUCT_TARGET=DTV
        MODEL_TARGET=Kant.M2-Tizen-KUBSAN
        MODEL_CHIP=KantM
elif [ "$1" == "3" ]; then
	PRODUCT_TARGET=DTV
	MODEL_TARGET=Kant.SU-Tizen
	MODEL_CHIP=KantSU
elif [ "$1" == "4" ]; then
	PRODUCT_TARGET=AV
	MODEL_TARGET=Kant.M2-Tizen-AV
	MODEL_CHIP=KantM
elif [ "$1" == "5" ]; then
	PRODUCT_TARGET=DTV
	MODEL_TARGET=Muse.M-Tizen
	MODEL_CHIP=MuseM
elif [ "$1" == "23" ]; then
	PRODUCT_TARGET=DTV
	MODEL_TARGET=Muse.M-Tizen-KASAN
	MODEL_CHIP=MuseM
elif [ "$1" == "24" ]; then
	PRODUCT_TARGET=DTV
	MODEL_TARGET=Muse.M-Tizen-KUBSAN
	MODEL_CHIP=MuseM
elif [ "$1" == "11" ]; then
	PRODUCT_TARGET=DTV
	MODEL_TARGET=Kant.M-Tizen
	MODEL_CHIP=KantM
elif [ "$1" == "12" ]; then
	PRODUCT_TARGET=DTV
	MODEL_TARGET=Kant.S-Tizen
	MODEL_CHIP=KantS
elif [ "$1" == "13" ]; then
	PRODUCT_TARGET=AV
	MODEL_TARGET=Kant.M-Tizen-AV
	MODEL_CHIP=KantM
elif [ "$1" == "14" ]; then
	PRODUCT_TARGET=EP
	MODEL_TARGET=Kant.M-Tizen-EP
	MODEL_CHIP=KantM
elif [ "$1" == "15" ]; then
	PRODUCT_TARGET=AV
	MODEL_TARGET=MT8516-Tizen
	MODEL_CHIP=MT8516
elif [ "$1" == "16" ]; then
	PRODUCT_TARGET=AV
	MODEL_TARGET=MT8516-Tizen-SAT
	MODEL_CHIP=MT8516
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

	#  Check uImage build
	if [ $? -ne 0 ]; then
		echo "build uImage failed. Stopped..."
		exit 1
	fi
	cp -f arch/arm/boot/uImage $ROOT_DIR/../$MODEL_TARGET/image/$RELEASE_MODE_STRING/
	make_signature $ROOT_DIR/../$MODEL_TARGET/image/$RELEASE_MODE_STRING/uImage $MODEL_CHIP $ROOT_DIR/../$MODEL_TARGET/image/$RELEASE_MODE_STRING/uImage
fi

##############################################################################
# dtb

	echo "Wrong input"
	TARGET_INPUT_ERR=$TRUE

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
