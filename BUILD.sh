#!/bin/bash

##############################################################################
# set envs
ROOT_DIR=$PWD

##############################################################################
# VERSION


chmod 755 version.txt patch.txt
chmod 755 include/linux/vdlp_version_PreHawk.P-Tizen.h
chmod 755 include/linux/vdlp_version_Hawk.P-Tizen.h
chmod 755 include/linux/vdlp_version_*.h

#VERSION=`cat version.txt`
#PATCH_NUM=`cat patch.txt`

##############################################################################

echo ""
echo " BSP Release Process"
echo "================================================================"
echo " Select target for Release"
echo "================================================================"
echo " 1. PreHawk.P-TIZEN"
echo " 2. Hawk.P-TIZEN"
echo " 3. Golf.P-TIZEN"
echo " 4. Hawk.M-TIZEN"
echo " 5. Echo.P-TIZEN"
echo " 6. Fox.P-TIZEN"
echo " 7. Hawk.P-Tizen_BD"
echo "================================================================"
read target
 
if [ "$target" == "1" ]
then
	PRODUCT_TARGET=DTV
	MODEL_TARGET=PreHawk.P-Tizen
	CORE_TYPE=ARM
	TOOLCHAIN_PREFIX=armv7l-tizen-linux-gnueabi-
	PRODUCT_TARGET=DTV
elif [ "$target" == "2" ]
then
	PRODUCT_TARGET=DTV
	MODEL_TARGET=Hawk.P-Tizen
	CORE_TYPE=ARM
	TOOLCHAIN_PREFIX=armv7l-tizen-linux-gnueabi-
	PRODUCT_TARGET=DTV  
elif [ "$target" == "3" ]
then
	PRODUCT_TARGET=DTV
	MODEL_TARGET=Golf.P-Tizen
	CORE_TYPE=ARM
	TOOLCHAIN_PREFIX=armv7l-tizen-linux-gnueabi-
	PRODUCT_TARGET=DTV
elif [ "$target" == "4" ]
then
	PRODUCT_TARGET=DTV
	MODEL_TARGET=Hawk.M-Tizen
	CORE_TYPE=ARM
	TOOLCHAIN_PREFIX=armv7l-tizen-linux-gnueabi-
	PRODUCT_TARGET=DTV
elif [ "$target" == "5" ]
then
        PRODUCT_TARGET=DTV
        MODEL_TARGET=Echo.P-Tizen
        CORE_TYPE=ARM
        TOOLCHAIN_PREFIX=armv7l-tizen-linux-gnueabi-
        PRODUCT_TARGET=DTV
elif [ "$target" == "6" ]
then
        PRODUCT_TARGET=DTV
        MODEL_TARGET=Fox.P-Tizen
        CORE_TYPE=ARM
        TOOLCHAIN_PREFIX=armv7l-tizen-linux-gnueabi-
        PRODUCT_TARGET=DTV
elif [ "$target" == "7" ]
then
        PRODUCT_TARGET=DTV
        MODEL_TARGET=Hawk.P-Tizen_BD
        CORE_TYPE=ARM
        TOOLCHAIN_PREFIX=armv7l-tizen-linux-gnueabi-
        PRODUCT_TARGET=BD
else
	echo "Wrong input"
	TARGET_INPUT_ERR=$TRUE	
fi

echo "========================================================================"
echo " Tizen Select Release Mode..."
echo "========================================================================"
echo " 1. release mode"
echo " 2. perf mode"
echo " 3. debug mode"
echo "========================================================================"
read mode

cd $ROOT_DIR

mkdir -p      $ROOT_DIR/../modules-release/modules
make distclean

if [ "$mode" == "1" ]
then
	RELEASE_DEBUG_CHECK=release
	RELEASE_MODE=release
	RELEASE_MODE_STRING=release
elif [ "$mode" == "2" ]
then
	RELEASE_DEBUG_CHECK=perf
	RELEASE_MODE=perf
	RELEASE_MODE_STRING=perf
elif [ "$mode" == "3" ]
then
	RELEASE_DEBUG_CHECK=debug
	RELEASE_MODE=debug
	RELEASE_MODE_STRING=debug
fi

export RELEASE_MODE

rm -rf $ROOT_DIR/$MODEL_TARGET/$RELEASE_MODE_STRING
mkdir -p $ROOT_DIR/$MODEL_TARGET/$RELEASE_MODE_STRING

cd $ROOT_DIR
echo "Creating Symbolic Link [vdlp_version.h -> vdlp_version_"$MODEL_TARGET".h]"
test -f include/linux/vdlp_version_$MODEL_TARGET.h
if [ $? == 0 ]
then
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
if [ $answer == "y" ]
then
	echo "Input KERNEL VERSION NUMBER"
	read answer
	VERSION=$answer
	echo $VERSION > version.txt

	echo "Input PATCH VERSION NUMBER"
	read answer
	PATCH_NUM=$answer
	echo $PATCH_NUM > patch.txt

	KERNEL_VERSION_MSG="$PATCH_NUM, $PRODUCT_TARGET, $MODEL_TARGET, $RELEASE_MODE_STRING, MAIN"

	echo "#define DTV_KERNEL_VERSION "\"$VERSION, $RELEASE_MODE_STRING\" > include/linux/vdlp_version.h
	echo "#define DTV_LAST_PATCH "\"$KERNEL_VERSION_MSG\" >> include/linux/vdlp_version.h

elif [ $answer == "n" ]
then
       	find ./include/linux/ -name "vdlp_version.h" | xargs perl -pi -e 's/product/'$RELEASE_MODE'/g'
        find ./include/linux/ -name "vdlp_version.h" | xargs perl -pi -e 's/debug/'$RELEASE_MODE'/g'
        find ./include/linux/ -name "vdlp_version.h" | xargs perl -pi -e 's/perf/'$RELEASE_MODE'/g'
fi



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

if [ $answer == "y" ]
then
       make menuconfig
elif [ $answer == "n" ]
then
       echo "Keep going"
fi

diff  arch/arm/configs/"$MODEL_TARGET"_"$RELEASE_MODE"_defconfig .config 


echo "========================================================================"
echo " Make sure configs that you want y/n?"
echo "========================================================================"

read answer 

if [ $answer == "y" ]
then
	echo " Kepp going" 
elif [ $answer == "n" ]
then
	./Clean.sh
	exit 1 
fi


export ARCH=arm
export CROSS_COMPILE=$TOOLCHAIN_PREFIX


if [ "$target" == "3" ]
then
	make uzImage -j8 $*
	if [ $? -ne 0 ]; then
		echo "build uImage failed. Stopped..."
		exit 1
	fi
elif [ "$target" == "5" ]
then
	make uzImage -j8 $*
	if [ $? -ne 0 ]; then
		echo "build uImage failed. Stopped..."
		exit 1
	fi
elif [ "$target" == "6" ]
then
        make uzImage -j8 $*
        if [ $? -ne 0 ]; then
                echo "build uImage failed. Stopped..."
                exit 1
        fi
else
	make uImage -j8 $*
	if [ $? -ne 0 ]; then
		echo "build uImage failed. Stopped..."
		exit 1
	fi
fi

##############################################################################
# dtb was removed except for Golf.P-Tizen and Echo.P-Tizen
##############################################################################
	
if [ "$target" == "3" ]
then
	make sdp1304-dtv-fhd-gptizen.dtb $* RELEASE_MODE=$RELEASE_MODE
	if [ $? -ne 0 ]; then
		echo "build dtb failed. Stopped..."
		exit 1
	fi 
elif [ "$target" == "5" ]
then
	make sdp1106.dtb $* RELEASE_MODE=$RELEASE_MODE
	if [ $? -ne 0 ]; then
		echo "build dtb failed. Stopped..."
		exit 1
	fi
elif [ "$target" == "6" ]
then
        make sdp1202.dtb $* RELEASE_MODE=$RELEASE_MODE
        if [ $? -ne 0 ]; then
                echo "build dtb failed. Stopped..."
                exit 1
        fi
else
	echo "Wrong input"
	TARGET_INPUT_ERR=$TRUE	
fi

cp -f arch/arm/boot/uImage $ROOT_DIR/$MODEL_TARGET/$RELEASE_MODE_STRING/uImage
mv vmlinux $ROOT_DIR/$MODEL_TARGET/$RELEASE_MODE_STRING/vmlinux


##############################################################################
# dtb added to uImage

pwd

if [ "$target" == "3" ]
then
	cp arch/arm/boot/uzImage $ROOT_DIR/$MODEL_TARGET/$RELEASE_MODE_STRING/uImage
	cat arch/arm/boot/dts/sdp1304-dtv-fhd-gptizen.dtb >> $ROOT_DIR/$MODEL_TARGET/$RELEASE_MODE_STRING/uImage
elif [ "$target" == "5" ]
then
	cp -f  arch/arm/boot/uzImage $ROOT_DIR/$MODEL_TARGET/$RELEASE_MODE_STRING/Image
	cat arch/arm/boot/dts/sdp1106.dtb >> $ROOT_DIR/$MODEL_TARGET/$RELEASE_MODE_STRING/Image
        cd scripts/dtb_size/
	make clean
        make
        cd -
        scripts/dtb_size/dtb_size $ROOT_DIR/$MODEL_TARGET/$RELEASE_MODE_STRING/Image arch/arm/boot/dts/sdp1106.dtb
	cd -
	make clean
	cd -
elif [ "$target" == "6" ]
then
        cp -f  arch/arm/boot/uzImage $ROOT_DIR/$MODEL_TARGET/$RELEASE_MODE_STRING/uImage
        cat arch/arm/boot/dts/sdp1202.dtb >> $ROOT_DIR/$MODEL_TARGET/$RELEASE_MODE_STRING/uImage
        cd scripts/dtb_size/
        make clean
        make
        cd -
        scripts/dtb_size/dtb_size $ROOT_DIR/$MODEL_TARGET/$RELEASE_MODE_STRING/uImage arch/arm/boot/dts/sdp1202.dtb
        cd -
        make clean
        cd -
fi


##############################################################################

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
find . -name "*.ko" -exec curl -w %{http_code} -f  -F "file=@{}" -o {} http://10.40.68.68/isms/open/masterkey/signKUEP.do \;
# 

/bin/tar zcvf $ROOT_DIR/../modules-release/kmod.tgz 3.10.30
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
if [ $answer == "y" ] 
then
	cp -f $MODEL_TARGET/$RELEASE_MODE/*  ../$MODEL_TARGET/image/$RELEASE_MODE/
	if [ $MODEL_TARGET == "Echo.P-Tizen" ]
	then
		cp -f ../$MODEL_TARGET/image/$RELEASE_MODE/Image ../$MODEL_TARGET/image/$RELEASE_MODE/Image.tmp
	else
		cp -f ../$MODEL_TARGET/image/$RELEASE_MODE/uImage ../$MODEL_TARGET/image/$RELEASE_MODE/uImage.tmp
	fi	
else
	echo "do nothing"
fi


## generator 
if [ $answer == "y" ]
then
	if [ "$MODEL_TARGET" == "PreHawk.P-Tizen" ] || [ "$MODEL_TARGET" == "Golf.P-Tizen" ]
	then 
		if [ -e ~/rsa_gen_golf ]
		then
			sudo ~/rsa_gen_golf ../$MODEL_TARGET/image/$RELEASE_MODE/uImage.tmp ../$MODEL_TARGET/image/$RELEASE_MODE/uImage
			sudo rm -f ../$MODEL_TARGET/image/$RELEASE_MODE/uImage.tmp 
		else
			echo " There is no generator " 
			mv -f ../$MODEL_TARGET/image/$RELEASE_MODE/uImage.tmp ../$MODEL_TARGET/image/$RELEASE_MODE/uImage
			exit 
		fi
	elif [ "$MODEL_TARGET" == "Hawk.P-Tizen" ] || [ "$MODEL_TARGET" == "Hawk.P-Tizen_BD" ]
	then
		if [ -e ~/sig_gen ]
		then
			sudo ~/sig_gen HawkP ../$MODEL_TARGET/image/$RELEASE_MODE/uImage.tmp ../$MODEL_TARGET/image/$RELEASE_MODE/uImage
			sudo rm -f ../$MODEL_TARGET/image/$RELEASE_MODE/uImage.tmp 
		else
			echo " There is no generator " 
			mv -f ../$MODEL_TARGET/image/$RELEASE_MODE/uImage.tmp ../$MODEL_TARGET/image/$RELEASE_MODE/uImage
			exit  
		fi
	elif [ "$MODEL_TARGET" == "Hawk.M-Tizen" ]  
	then
		if [ -e ~/sig_gen ]
		then
			sudo ~/sig_gen HawkM ../$MODEL_TARGET/image/$RELEASE_MODE/uImage.tmp ../$MODEL_TARGET/image/$RELEASE_MODE/uImage
			sudo rm -f ../$MODEL_TARGET/image/$RELEASE_MODE/uImage.tmp 
		else
			echo " There is no generator " 
			mv  -f ../$MODEL_TARGET/image/$RELEASE_MODE/uImage.tmp ../$MODEL_TARGET/image/$RELEASE_MODE/uImage
			exit  
		fi

	elif [ "$MODEL_TARGET" == "Echo.P-Tizen" ]
	then
		if [ -e ~/cmac_gen ]
		then
			sudo ~/cmac_gen ../$MODEL_TARGET/image/$RELEASE_MODE/Image.tmp ../$MODEL_TARGET/image/$RELEASE_MODE/Image
			sudo rm -f ../$MODEL_TARGET/image/$RELEASE_MODE/Image.tmp 
		else
			echo " There is no generator "
			mv -f ../$MODEL_TARGET/image/$RELEASE_MODE/Image.tmp ../$MODEL_TARGET/image/$RELEASE_MODE/Image
			exit  
		fi
	elif [ "$MODEL_TARGET" == "Fox.P-Tizen" ]
	then
		if [ -e ~/rsa_gen ]
		then
			sudo ~/rsa_gen ../$MODEL_TARGET/image/$RELEASE_MODE/uImage.tmp ../$MODEL_TARGET/image/$RELEASE_MODE/uImage
			sudo rm -f ../$MODEL_TARGET/image/$RELEASE_MODE/uImage.tmp 
		else
			echo " There is no generator "
			mv -f ../$MODEL_TARGET/image/$RELEASE_MODE/uImage.tmp ../$MODEL_TARGET/image/$RELEASE_MODE/uImage
			exit  
		fi
	fi
else
	echo "do not generator"
fi

echo "##################################################################"
echo "Do you want to clean result y/n "
echo "##################################################################"

read answer
if [ $answer == "y" ]
then 
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
if [ $answer == "y" ] 
then
	cp -f .config  arch/arm/configs/"$MODEL_TARGET"_"$RELEASE_MODE"_defconfig
else
	echo "do nothing"
fi


echo "##################################################################"
echo "Do you want make user header files y/n ?"
echo "##################################################################"

read answer
if [ $answer == "y" ]
then
	./BUILD_header.sh
else
	echo "keep going"
fi

echo "##################################################################"
echo "Do you want to Do Clean.sh y/n"
echo "##################################################################"
read answer
if [ $answer == "y" ] 
then
	./Clean.sh
else
	echo "do nothing"
fi


echo "BUILD DONE"

