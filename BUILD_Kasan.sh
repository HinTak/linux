#!/bin/bash

##############################################################################
# set envs
ROOT_DIR=$PWD

##############################################################################
# VERSION


chmod 755 version.txt patch.txt
chmod 755 include/linux/vdlp_version_*.h

#VERSION=`cat version.txt`
#PATCH_NUM=`cat patch.txt`

##############################################################################

echo ""
echo " BSP Release Process"
echo "================================================================"
echo " Select target for Release"
echo "================================================================"
echo " 1. Hawk.P-TIZEN"
echo " 2. Hawk.M-TIZEN"
echo "================================================================"
echo " 21. Jazz.M-TIZEN"
echo " 23. NT16M-Tizen"
echo " 24. Hawk.A-TIZEN"
#echo " 25. Hawk.P-Tizen_BD"
echo "================================================================"
#echo " 90. Echo.P-Tizen"
#echo " 91. Fox.P-Tizen"
#echo " 92. Golf.P-Tizen"
#echo " 93. Hawk.P-Tizen_SBB"
echo "================================================================"
read target

###################################################
# Default environment

CORE_TYPE=ARM
TOOLCHAIN_PREFIX=armv7l-tizen-linux-gnueabi-
###################################################



if [ "$target" == "1" ]; then
	PRODUCT_TARGET=DTV
	MODEL_TARGET=Hawk.P-Tizen
elif [ "$target" == "2" ]; then
	PRODUCT_TARGET=DTV
	MODEL_TARGET=Hawk.M-Tizen
elif [ "$target" == "21" ]; then
	PRODUCT_TARGET=DTV
	MODEL_TARGET=Jazz.M-Tizen
elif [ "$target" == "23" ]; then
	PRODUCT_TARGET=DTV
	MODEL_TARGET=NT16M-Tizen
elif [ "$target" == "24" ]; then
	PRODUCT_TARGET=AV
	MODEL_TARGET=Hawk.A-Tizen
#elif [ "$target" == "25" ]; then
#	PRODUCT_TARGET=BD
#	MODEL_TARGET=Hawk.P-Tizen_BD
#elif [ "$target" == "90" ]; then
#	PRODUCT_TARGET=DTV
#	MODEL_TARGET=Echo.P-Tizen
#elif [ "$target" == "91" ]; then
#	PRODUCT_TARGET=DTV
#	MODEL_TARGET=Fox.P-Tizen
#elif [ "$target" == "92" ]; then
#	PRODUCT_TARGET=DTV
#	MODEL_TARGET=Golf.P-Tizen
#elif [ "$target" == "93" ]; then
#	PRODUCT_TARGET=SBB
#	MODEL_TARGET=Hawk.P-Tizen_SBB
else
	echo "Wrong input"
	TARGET_INPUT_ERR=$TRUE
fi


echo "===================================================="
echo " Checking tool chain version .. .. ."

checkToolChainVersion()
{
##############################################################################
# Check Toolchain Version : Here we are choosen method of compilation of a
#       new code and checking version of compiled binary.
# Please update the following macros for tool chain version
# YEAR.MONTH-DAY
TOOL_VERS_Y=2015        # YEAR
TOOL_VERS_M=07          # MONTH
TOOL_VERS_D=28          # DAY

__CC__=${TOOLCHAIN_PREFIX}gcc
TOOL_VERS_CHK_FILE=dymmy_tool_vers_check
TOOL_VERS_CHK_PATH=/tmp/${TOOL_VERS_CHK_FILE}.c

/bin/echo "#include <stdio.h>" > $TOOL_VERS_CHK_PATH;
${__CC__} -c $TOOL_VERS_CHK_PATH;
__TOOL_VERS__=`${__CC__} --version ${TOOL_VERS_CHK_FILE}.o | /bin/grep "Tizen"`;
/bin/rm $TOOL_VERS_CHK_PATH ${TOOL_VERS_CHK_FILE}.o;

if [ ! -z "${__TOOL_VERS__}" ]
then
        __TOOL_DATE__=`/bin/echo ${__TOOL_VERS__} | grep -oE "[0-9][0-9][0-9][0-9]\-[0-1][0-9]\-[0-9][0-9]"`;
        __YEAR__=`/bin/echo ${__TOOL_DATE__} | sed 's/\-.*//g'`;
        __MONTH__=`/bin/echo ${__TOOL_DATE__} | cut -c6- | sed 's/\-.*//g'`;
        __DAY__=`/bin/echo ${__TOOL_DATE__} | sed 's/.*\-//g'`;
else
	/bin/echo "Please use Tizen Toolchain! - Dated (yyyy mm dd) - $TOOL_VERS_Y $TOOL_VERS_M $TOOL_VERS_D";
        exit 1;
fi

if [ "${__YEAR__}" != "$TOOL_VERS_Y" ]
then
        /bin/echo "Your toolchain version is not $TOOL_VERS_Y-$TOOL_VERS_M-$TOOL_VERS_D!";
        ./Clean.sh;
        exit 1;
fi

if [ "${__MONTH__}" != "$TOOL_VERS_M" ]
then
        /bin/echo "Your toolchain version is not $TOOL_VERS_Y-$TOOL_VERS_M-$TOOL_VERS_D!";
        ./Clean.sh;
        exit 1;
fi

if [ "${__DAY__}" != "$TOOL_VERS_D" ]
then
        /bin/echo "Your toolchain version is not $TOOL_VERS_Y-$TOOL_VERS_M-$TOOL_VERS_D!";
        ./Clean.sh;
        exit 1;
fi

/bin/echo "Toolchain version is up-to-date...";
##############################################################################

}


checkToolChainVersion


echo "========================================================================"
echo " Tizen Select Release Mode..."
echo "========================================================================"
echo " 1. release mode"
echo " 2. perf mode"
echo " 3. debug mode"
echo " 4. KASAN perf mode"
echo "========================================================================"
read mode

cd $ROOT_DIR

mkdir -p      $ROOT_DIR/../modules-release/modules
make distclean

if [ "$mode" == "1" ]; then
	RELEASE_DEBUG_CHECK=release
	RELEASE_MODE=release
	RELEASE_MODE_STRING=release
	RELEASE_MODE_CONFIG=release
elif [ "$mode" == "2" ]; then
	RELEASE_DEBUG_CHECK=perf
	RELEASE_MODE=perf
	RELEASE_MODE_STRING=perf
	RELEASE_MODE_CONFIG=perf
elif [ "$mode" == "3" ]; then
	RELEASE_DEBUG_CHECK=debug
	RELEASE_MODE=debug
	RELEASE_MODE_STRING=debug
	RELEASE_MODE_CONFIG=debug
elif [ "$mode" == "4" ]; then
	RELEASE_DEBUG_CHECK=perf
	RELEASE_MODE=perf
	RELEASE_MODE_STRING=perf
	RELEASE_MODE_CONFIG=kasan
fi

export RELEASE_MODE

rm -rf $ROOT_DIR/$MODEL_TARGET/$RELEASE_MODE_STRING
mkdir -p $ROOT_DIR/$MODEL_TARGET/$RELEASE_MODE_STRING

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
echo "========================================================================"
echo "Do you want to change kernel and patch version number y/n?"
echo "========================================================================"

read answer
if [ $answer == "y" ]; then
	echo "Input KERNEL VERSION NUMBER"
	read answer
	VERSION=$answer

	echo "Input PATCH VERSION NUMBER"
	read answer
	PATCH_NUM=$answer

	KERNEL_VERSION_MSG="$PATCH_NUM, $PRODUCT_TARGET, $MODEL_TARGET, $RELEASE_MODE_STRING, MAIN"

	echo "#define DTV_KERNEL_VERSION "\"$VERSION, $RELEASE_MODE_STRING\" > include/linux/vdlp_version.h
	echo "#define DTV_LAST_PATCH "\"$KERNEL_VERSION_MSG\" >> include/linux/vdlp_version.h

elif [ $answer == "n" ]; then
	find ./include/linux/ -name "vdlp_version.h" | xargs perl -pi -e 's/product/'$RELEASE_MODE'/g'
	find ./include/linux/ -name "vdlp_version.h" | xargs perl -pi -e 's/debug/'$RELEASE_MODE'/g'
	find ./include/linux/ -name "vdlp_version.h" | xargs perl -pi -e 's/perf/'$RELEASE_MODE'/g'
fi

echo "========================================================================"
cat ./include/linux/vdlp_version.h
echo "========================================================================"

##############################################################################
# build kernel
echo $MODEL_TARGET\_$RELEASE_MODE_CONFIG\_defconfig
make $MODEL_TARGET\_$RELEASE_MODE_CONFIG\_defconfig

echo "========================================================================"
echo "Do you want to change kernel config (y/n)?"
echo "========================================================================"

read answer

if [ $answer == "y" ]; then
	make menuconfig
elif [ $answer == "n" ]; then
	echo "Keep going"
fi

diff  arch/arm/configs/"$MODEL_TARGET"_"$RELEASE_MODE_CONFIG"_defconfig .config

echo "========================================================================"
echo " Make sure configs that you want y/n?"
echo "========================================================================"

read answer

if [ $answer == "y" ]; then
	echo " Kepp going"
elif [ $answer == "n" ]; then
	./Clean.sh
	exit 1
fi

export ARCH=arm
export CROSS_COMPILE=$TOOLCHAIN_PREFIX

	make uImage -j8 $*

# Check uImage build
if [ $? -ne 0 ]; then
	echo "build uImage failed. Stopped..."
	exit 1
fi

cp -f arch/arm/boot/uImage $ROOT_DIR/$MODEL_TARGET/$RELEASE_MODE_STRING/uImage
mv vmlinux $ROOT_DIR/$MODEL_TARGET/$RELEASE_MODE_STRING/vmlinux

##############################################################################
# make modules

make modules -j8 $*
if [ $? -ne 0 ]; then
	echo "build modules failed. Stopped..."
	exit 1
fi

make modules_install INSTALL_MOD_STRIP=1 INSTALL_MOD_PATH=$ROOT_DIR/../modules-release/modules $*
rm -f $ROOT_DIR/../modules-release/modules/lib/modules/3.10.30/build
rm -f $ROOT_DIR/../modules-release/modules/lib/modules/3.10.30/source
if [ $? -ne 0 ]; then
	echo "modules_install failed. Stopped..."
	exit 1
fi

#############################################################################
# add module config files

rm -f $ROOT_DIR/../modules-release/modules/lib/modules/3.10.30/modules.*
cp -f $ROOT_DIR/../modules-release/modules.* $ROOT_DIR/../modules-release/modules/lib/modules/3.10.30/

##############################################################################
# package modules

cd $ROOT_DIR/../modules-release/modules/lib/modules/

# KUEP
find . -name "*.ko" -exec curl -w %{http_code}  -f -F "file=@{}" -F "year=2016" -o {}  http://10.40.68.68/isms/open/masterkey/signKUEP.do \;
#

/bin/tar zcvf $ROOT_DIR/../modules-release/kmod.tgz 3.10.30
cp -f $ROOT_DIR/../modules-release/kmod.tgz $ROOT_DIR/$MODEL_TARGET/$RELEASE_MODE_STRING/
cd $ROOT_DIR


#------------------------------------------
error_qemuarmstatic ()
{
echo "--------------------------------------------"
echo "Please check qemu-arm-static installed on build system and linked with interpreter"
echo "--------------------------------------------"
exit 1
}

cd $ROOT_DIR
echo "Repreparing binaries for $CORE_TYPE "
FOR_QB_GCC=gcc

make HOSTCC="${TOOLCHAIN_PREFIX}${FOR_QB_GCC} -static" scripts || error_qemuarmstatic
make HOSTCC="${TOOLCHAIN_PREFIX}${FOR_QB_GCC} -static" silentoldconfig || error_qemuarmstatic



echo " wait ... .. ."
#-----------------------------------------


# create vdkernel_header

HEADER_BUILD_ROOT=$ROOT_DIR/del
mkdir -p $HEADER_BUILD_ROOT/usr/include/kernel_header/linux-3.10.28
mkdir -p $HEADER_BUILD_ROOT/usr/include/$RELEASE_MODE_STRING
mkdir -p $HEADER_BUILD_ROOT/usr/include/kernel_header/linux-3.10.28/kernel/t2ddebugd/include/t2ddebugd/

cp ./drivers ./arch ./scripts ./include $HEADER_BUILD_ROOT/usr/include/kernel_header/linux-3.10.28 -ar
cp ./kernel/t2ddebugd/include/t2ddebugd/* $HEADER_BUILD_ROOT/usr/include/kernel_header/linux-3.10.28/kernel/t2ddebugd/include/t2ddebugd/ -ar
cp ./Kbuild ./Makefile ./Module.symvers $HEADER_BUILD_ROOT/usr/include/kernel_header/linux-3.10.28
cp build.config $HEADER_BUILD_ROOT/usr/include/kernel_header/linux-3.10.28
cp off_tree_module_build.mk $HEADER_BUILD_ROOT/usr/include/kernel_header/linux-3.10.28
cp .config $HEADER_BUILD_ROOT/usr/include/kernel_header/linux-3.10.28

find $HEADER_BUILD_ROOT/usr/include/kernel_header/linux-3.10.28/drivers -name "*.[c;s;S]" -exec rm {} -rf \;
find $HEADER_BUILD_ROOT/usr/include/kernel_header/linux-3.10.28/arch -name "*.[c;s;S]" -exec rm {} -rf \;
find $HEADER_BUILD_ROOT/usr/include/kernel_header/linux-3.10.28 -name "*.ko" -exec rm {} -rf \;
find $HEADER_BUILD_ROOT/usr/include/kernel_header/linux-3.10.28 -name "*.[o;a]" -exec rm {} -rf \;
find $HEADER_BUILD_ROOT/usr/include/kernel_header/linux-3.10.28 -name "*.cmd" -exec rm {} -rf \;
rm -f $HEADER_BUILD_ROOT/usr/include/kernel_header/linux-3.10.28/arch/arm/boot/mkimage
rm -f $HEADER_BUILD_ROOT/usr/include/kernel_header/linux-3.10.28/arch/arm/boot/compressed/vmlinux

mv $HEADER_BUILD_ROOT/usr/include/kernel_header/linux-3.10.28 $HEADER_BUILD_ROOT/usr/include/kernel_header/$RELEASE_MODE_STRING
rm -f $HEADER_BUILD_ROOT/usr/include/kernel_header/$RELEASE_MODE_STRING/arch/arm/boot/mkimage

cd $HEADER_BUILD_ROOT/usr/include/kernel_header/
tar zcf vdkernel_header.tgz ./$RELEASE_MODE_STRING
mv vdkernel_header.tgz $ROOT_DIR/$MODEL_TARGET/$RELEASE_MODE/

cd $ROOT_DIR
rm -rf $ROOT_DIR/del

answer=y
## generator

if [ $answer == "y" ]; then
        if [ $MODEL_TARGET == "Echo.P-Tizen" ]; then
                cp -f $MODEL_TARGET/$RELEASE_MODE/Image $MODEL_TARGET/$RELEASE_MODE/Image.tmp
        else
                cp -f $MODEL_TARGET/$RELEASE_MODE/uImage $MODEL_TARGET/$RELEASE_MODE/uImage.tmp
        fi
else
        echo "do nothing"
fi

if [ $answer == "y" ]; then
	if [ "$MODEL_TARGET" == "Hawk.P-Tizen" ]; then
		if [ -e ~/sig_gen ]; then
			~/sig_gen HawkP ./$MODEL_TARGET/$RELEASE_MODE/uImage.tmp ./$MODEL_TARGET/$RELEASE_MODE/uImage
			rm -f ./$MODEL_TARGET/image/$RELEASE_MODE/uImage.tmp
		else
			echo " There is no generator "
			mv -f ./$MODEL_TARGET/$RELEASE_MODE/uImage.tmp ./$MODEL_TARGET/$RELEASE_MODE/uImage
			exit
		fi
	elif [ "$MODEL_TARGET" == "Hawk.M-Tizen" ]; then
		if [ -e ~/sig_gen ]; then
			~/sig_gen HawkM ./$MODEL_TARGET/$RELEASE_MODE/uImage.tmp ./$MODEL_TARGET/$RELEASE_MODE/uImage
			rm -f ./$MODEL_TARGET/$RELEASE_MODE/uImage.tmp
		else
			echo " There is no generator "
			mv  -f ./$MODEL_TARGET/$RELEASE_MODE/uImage.tmp ./$MODEL_TARGET/$RELEASE_MODE/uImage
			exit
		fi
	elif [ "$MODEL_TARGET" == "Hawk.A-Tizen" ]; then
		if [ -e ~/sig_gen ]; then
			~/sig_gen HawkA ./$MODEL_TARGET/$RELEASE_MODE/uImage.tmp ./$MODEL_TARGET/$RELEASE_MODE/uImage
			rm -f ./$MODEL_TARGET/$RELEASE_MODE/uImage.tmp
		else
			echo " There is no generator "
			mv  -f ./$MODEL_TARGET/$RELEASE_MODE/uImage.tmp ./$MODEL_TARGET/$RELEASE_MODE/uImage
			exit
		fi
	elif [ "$MODEL_TARGET" == "NT16M-Tizen" ]; then
		if [ -f $ROOT_DIR/sig_nvt.sh ]; then
			$ROOT_DIR/sig_nvt.sh ./$MODEL_TARGET/$RELEASE_MODE/uImage.tmp ./$MODEL_TARGET/$RELEASE_MODE/uImage
			sudo rm -f ./$MODEL_TARGET/$RELEASE_MODE/uImage.tmp
		else
			echo " There is no generator "
			mv -f ./$MODEL_TARGET/$RELEASE_MODE/uImage.tmp ./$MODEL_TARGET/$RELEASE_MODE/uImage
			exit
		fi
	elif [ "$MODEL_TARGET" == "Jazz.M-Tizen" ]; then
		if [ -e ~/sig_gen ]; then
			~/sig_gen JazzM ./$MODEL_TARGET/$RELEASE_MODE/uImage.tmp ./$MODEL_TARGET/$RELEASE_MODE/uImage
			rm -f ./$MODEL_TARGET/$RELEASE_MODE/uImage.tmp
		else
			echo " There is no generator "
			mv  -f ./$MODEL_TARGET/$RELEASE_MODE/uImage.tmp ./$MODEL_TARGET/$RELEASE_MODE/uImage
			exit
		fi
	fi
else
	echo "do not generator"
fi



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
	cp -f $MODEL_TARGET/$RELEASE_MODE/*  ../$MODEL_TARGET/image/$RELEASE_MODE/
	if [ $MODEL_TARGET == "Echo.P-Tizen" ]; then
		cp -f ../$MODEL_TARGET/image/$RELEASE_MODE/Image ../$MODEL_TARGET/image/$RELEASE_MODE/Image.tmp
	else
		cp -f ../$MODEL_TARGET/image/$RELEASE_MODE/uImage ../$MODEL_TARGET/image/$RELEASE_MODE/uImage.tmp
	fi
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
	cp -f .config  arch/arm/configs/"$MODEL_TARGET"_"$RELEASE_MODE_CONFIG"_defconfig

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


check_signature()
{
	cd $ROOT_DIR

	echo ""
	echo "===================================================================="
	echo "Checking signature"
	echo "===================================================================="
	echo ""
	
	TOTAL_SIG_LEN=0

	if [ $MODEL_TARGET == "Echo.P-Tizen" ]
	then
		let TOTAL_SIG_LEN=16
	elif [ $MODEL_TARGET == "NT16M-Tizen" ]
	then
		let TOTAL_SIG_LEN=260
	else
		let TOTAL_SIG_LEN=256
	fi	

	if [ $TOTAL_SIG_LEN == 0 ]
	then
		echo "Error : Estimated length of signature is 0"
		exit 1
	fi

	if [ $MODEL_TARGET == "Echo.P-Tizen" ];	then 
		cp -f ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/Image .
		chmod 777 Image
		sudo chown $USER Image 
	else 	
		cp -f ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/uImage .
		chmod 777 uImage
		sudo chown $USER uImage
	fi
	
	
	if [ $MODEL_TARGET == "Echo.P-Tizen" ];	then 
		UIMAGE_LEN=$(stat -c %s Image)
	else 
		UIMAGE_LEN=$(stat -c %s uImage)
	fi

	let "UIMAGE_LEN=$UIMAGE_LEN - $TOTAL_SIG_LEN"

	if [ $MODEL_TARGET == "Echo.P-Tizen" ];	then 
		truncate -s $UIMAGE_LEN Image
	else
		truncate -s $UIMAGE_LEN uImage
	fi

	SIG_GEN_FOUND=0

	if [ $MODEL_TARGET == "Golf.P-Tizen" ]
	then 
		if [ -x ~/rsa_gen_golf ]; then SIG_GEN_FOUND=1; fi
		sudo ~/rsa_gen_golf uImage uImage.tmp 
		sudo mv -f uImage.tmp uImage
	elif [ $MODEL_TARGET == "Echo.P-Tizen" ]
	then
		if [ -x ~/cmac_gen ]; then SIG_GEN_FOUND=1; fi
		sudo ~/cmac_gen Image Image.tmp 
		mv -f Image.tmp Image
	elif  [ $MODEL_TARGET == "Hawk.M-Tizen" ]
	then 
		if [ -x ~/sig_gen ]; then SIG_GEN_FOUND=1; fi
		~/sig_gen HawkM uImage  uImage.tmp 
		mv -f uImage.tmp uImage 
	elif  [ $MODEL_TARGET == "Hawk.P-Tizen" ] || [ $MODEL_TARGET == "Hawk.P-Tizen_SBB" ] 
	then 
		if [ -x ~/sig_gen ]; then SIG_GEN_FOUND=1; fi
		~/sig_gen HawkP uImage uImage.tmp
		mv -f uImage.tmp uImage
	elif [ $MODEL_TARGET == "Fox.P-Tizen" ]
	then 
		if [ -x ~/rsa_gen ]; then SIG_GEN_FOUND=1; fi
		sudo ~/rsa_gen uImage uImage.tmp 
		mv -f uImage.tmp uImage	
	elif  [ $MODEL_TARGET == "Hawk.A-Tizen" ]
	then 
		if [ -x ~/sig_gen ]; then SIG_GEN_FOUND=1; fi
		~/sig_gen HawkA uImage  uImage.tmp 
		mv -f uImage.tmp uImage
	elif  [ $MODEL_TARGET == "NT16M-Tizen" ]
        then
                if [ -x $ROOT_DIR/sig_nvt.sh ]; then SIG_GEN_FOUND=1; fi
                $ROOT_DIR/sig_nvt.sh uImage uImage.tmp
                mv -f uImage.tmp uImage
	elif  [ $MODEL_TARGET == "Jazz.M-Tizen" ]
	then
		if [ -x ~/sig_gen ]; then SIG_GEN_FOUND=1; fi
		~/sig_gen JazzM uImage uImage.tmp
		mv -f uImage.tmp uImage
	fi

	if [ $SIG_GEN_FOUND == 0 ]
	then
		echo ""
		echo "#######################################################################"
		echo "Warning : Signature generator not found."
		echo "          You have to run a signal generator to release kernel image."
		echo "#######################################################################"
		echo "Enter return key to continue..."
		read answer
	fi
	
	if [ $MODEL_TARGET == "Echo.P-Tizen" ]; then
		DIFF_RESULT=$(diff ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/Image Image)
	else 
		DIFF_RESULT=$(diff ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/uImage uImage)
	fi

	if [ "$DIFF_RESULT" != "" ]
	then
		echo "Error : Signature is incorrect"
		exit 1
	fi

	rm -f uImage Image  

	echo ""
	echo "===================================================================="
	echo "Signature check result : OK"
	echo "===================================================================="
	echo ""
}

check_signature

echo "BUILD DONE"

