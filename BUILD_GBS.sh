#!/bin/bash

##############################################################################
# set envs
ROOT_DIR=$PWD

##############################################################################
# VERSION


##############################################################################


if [ "$1" == "1" ]
then
		PRODUCT_TARGET=DTV
		MODEL_TARGET=PreHawk.P-Tizen
		CORE_TYPE=ARM
		TOOLCHAIN_PREFIX=""
	elif [ "$1" == "2" ]
	then
		PRODUCT_TARGET=DTV
		MODEL_TARGET=Hawk.P-Tizen
		CORE_TYPE=ARM
		TOOLCHAIN_PREFIX=""
	elif [ "$1" == "3" ]
	then
		PRODUCT_TARGET=DTV
		MODEL_TARGET=Golf.P-Tizen
		CORE_TYPE=ARM
		TOOLCHAIN_PREFIX=""
	elif [ "$1" == "4" ]
	then
		PRODUCT_TARGET=DTV
		MODEL_TARGET=Hawk.M-Tizen
		CORE_TYPE=ARM
		TOOLCHAIN_PREFIX=""
	elif [ "$1" == "5" ]
	then
		PRODUCT_TARGET=DTV
        	MODEL_TARGET=Echo.P-Tizen
		CORE_TYPE=ARM
        	TOOLCHAIN_PREFIX=""
	elif [ "$1" == "6" ]
	then
        	PRODUCT_TARGET=DTV
        	MODEL_TARGET=Fox.P-Tizen
        	CORE_TYPE=ARM
	        TOOLCHAIN_PREFIX=""
	elif [ "$1" == "7" ]
	then
        	PRODUCT_TARGET=BD
        	MODEL_TARGET=Hawk.P-Tizen_BD
        	CORE_TYPE=ARM
	        TOOLCHAIN_PREFIX=""
	else
		echo "Wrong input"
		TARGET_INPUT_ERR=$TRUE	
	fi


mkdir -p      $ROOT_DIR/../modules-release/modules
make distclean

if [ "$2" == "1" ]
then
	RELEASE_DEBUG_CHECK=release
	RELEASE_MODE=release
	RELEASE_MODE_STRING=release
elif [ "$2" == "2" ]
then
	RELEASE_DEBUG_CHECK=perf
	RELEASE_MODE=perf
	RELEASE_MODE_STRING=perf
elif [ "$2" == "3" ]
then
	RELEASE_DEBUG_CHECK=debug
	RELEASE_MODE=debug
	RELEASE_MODE_STRING=debug
fi


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

if [ "$1" == "3" ]
then
        make uzImage -j 
        if [ $? -ne 0 ]; then
                echo "build uImage failed. Stopped..."
                exit 1
        fi
	cp -f arch/arm/boot/uzImage $ROOT_DIR/../$MODEL_TARGET/image/$RELEASE_MODE_STRING/uImage
elif [ "$1" == "5" ]
then
        make uzImage -j 
        if [ $? -ne 0 ]; then
                echo "build uImage failed. Stopped..."
                exit 1
        fi
	cp -f arch/arm/boot/uzImage $ROOT_DIR/../$MODEL_TARGET/image/$RELEASE_MODE_STRING/Image
elif [ "$1" == "6" ]
then
        make uzImage -j 
        if [ $? -ne 0 ]; then
                echo "build uImage failed. Stopped..."
                exit 1
        fi
	cp -f arch/arm/boot/uzImage $ROOT_DIR/../$MODEL_TARGET/image/$RELEASE_MODE_STRING/uImage
else
        make uImage -j 
        if [ $? -ne 0 ]; then
                echo "build uImage failed. Stopped..."
                exit 1
        fi
	cp -f arch/arm/boot/uImage $ROOT_DIR/../$MODEL_TARGET/image/$RELEASE_MODE_STRING/
fi



##############################################################################
# dtb

 	if [ "$1" == "3" ]
	then
		rm -f arch/arm/boot/compressed/vmlinux
		make sdp1304-dtv-fhd-gptizen.dtb RELEASE_MODE=$RELEASE_MODE
		if [ $? -ne 0 ]; then
			echo "build dtb failed. Stopped..."
			exit 1
		fi 
		cat arch/arm/boot/dts/sdp1304-dtv-fhd-gptizen.dtb >> $ROOT_DIR/../$MODEL_TARGET/image/$RELEASE_MODE_STRING/uImage
        elif [ "$1" == "5" ]
        then
		rm -f arch/arm/boot/compressed/vmlinux
                make sdp1106.dtb RELEASE_MODE=$RELEASE_MODE
                if [ $? -ne 0 ]; then
                        echo "build dtb failed. Stopped..."
                        exit 1
                fi
                cat arch/arm/boot/dts/sdp1106.dtb >> $ROOT_DIR/../$MODEL_TARGET/image/$RELEASE_MODE_STRING/Image
	        cd scripts/dtb_size/
		make clean
        	make
        	cd -
        	scripts/dtb_size/dtb_size $ROOT_DIR/../$MODEL_TARGET/image/$RELEASE_MODE_STRING/Image arch/arm/boot/dts/sdp1106.dtb
		cd -
		make clean
		cd -
        elif [ "$1" == "6" ]
        then
		rm -f arch/arm/boot/compressed/vmlinux
                make sdp1202.dtb RELEASE_MODE=$RELEASE_MODE
                if [ $? -ne 0 ]; then
                        echo "build dtb failed. Stopped..."
                        exit 1
                fi
                cat arch/arm/boot/dts/sdp1202.dtb >> $ROOT_DIR/../$MODEL_TARGET/image/$RELEASE_MODE_STRING/uImage
	        cd scripts/dtb_size/
		make clean
        	make
        	cd -
        	scripts/dtb_size/dtb_size $ROOT_DIR/../$MODEL_TARGET/image/$RELEASE_MODE_STRING/uImage arch/arm/boot/dts/sdp1202.dtb
		cd -
		make clean
		cd -
	else
		echo "Wrong input"
		TARGET_INPUT_ERR=$TRUE	
	fi

mv vmlinux $ROOT_DIR/../$MODEL_TARGET/image/$RELEASE_MODE_STRING/vmlinux

##############################################################################

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
cp $ROOT_DIR/../modules-release/kmod.tgz $ROOT_DIR/../$MODEL_TARGET/image/$RELEASE_MODE_STRING/
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
