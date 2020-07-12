#!/bin/bash


ROOT_DIR=$PWD
MODEL_TYPE="UHD"

chmod 777 autobuild.conf version.txt patch.txt 
chmod 755 include/linux/vdlp_version_*.h
echo auto > autobuild.conf

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

if [ "$MODEL_TARGET" = "Hawk.M-Tizen" ]
then
	echo ""
	echo " BSP Release Process"
	echo "================================================================"
	echo " Select DTV Type for $MODEL_TARGET"
	echo "================================================================"
	echo " 1. UHD"
	echo " 2. FHD"
	echo "================================================================"
	read answer

	if [ "$answer" = "1" ]
	then
		MODEL_TYPE="UHD"
	elif [ "$answer" = "2" ]
	then
		MODEL_TYPE="FHD"
	else
		echo "Wrong input"
		TARGET_INPUT_ERR=$TRUE
	fi
fi

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

        KERNEL_VERSION_MSG="$PATCH_NUM, $PRODUCT_TARGET, $MODEL_TARGET, debug, MAIN"

        echo "#define DTV_KERNEL_VERSION "\"$VERSION, debug\" > include/linux/vdlp_version_"$MODEL_TARGET".h
        echo "#define DTV_LAST_PATCH "\"$KERNEL_VERSION_MSG\" >> include/linux/vdlp_version_"$MODEL_TARGET".h

fi



echo "========================================================================"
cat ./include/linux/vdlp_version_"$MODEL_TARGET".h
echo "========================================================================"



cp  -f  ../DocTizen/DocForReleaseBSP/GBS.Conf.File/"$MODEL_TARGET"."$MODEL_TYPE".gbs.conf ~/.gbs.conf

cd ../

~/bin/vbs_build.sh

cd -


if [ $MODEL_TARGET = "PreHawk.P-Tizen" ]
then
	LOCAL_REPOS=~/GBS-ROOT/local/repos/preHawk
elif [ $MODEL_TARGET = "Golf.P-Tizen" ]
then
	LOCAL_REPOS=~/GBS-ROOT/local/repos/tztv_2.2.1_prehawk
elif [ $MODEL_TARGET = "Hawk.M-Tizen" ]
then
	if [ $MODEL_TYPE = "UHD" ]
	then
		LOCAL_REPOS=~/GBS-ROOT/local/repos/hawkM_uhd
	elif [ $MODEL_TYPE = "FHD" ]
	then
		LOCAL_REPOS=~/GBS-ROOT/local/repos/hawkM_fhd
	else
		echo "Wrong Model Type..."
		exit 1
	fi
elif [ $MODEL_TARGET = "Hawk.P-Tizen" ]
then
	LOCAL_REPOS=~/GBS-ROOT/local/repos/hawkP
elif [ $MODEL_TARGET = "Echo.P-Tizen" ]
then
	LOCAL_REPOS=~/GBS-ROOT/local/repos/hawkP
elif [ $MODEL_TARGET = "Fox.P-Tizen" ]
then
	LOCAL_REPOS=~/GBS-ROOT/local/repos/hawkP
else
	echo "Wrong Model Target..."
	exit 1
fi

cd $LOCAL_REPOS/armv7l/RPMS


echo "============================================="
echo "Current Dir is "
echo `pwd`
echo "============================================="

rm -rf temp
mkdir temp
cd temp

## debug
rpm2cpio ../tztv-hawk-kmodules-1-1.armv7l.rpm | cpio -di
if [ $MODEL_TARGET = "Echo.P-Tizen" ]
then
	cp -f  ./include/Image   "$ROOT_DIR"/../"$MODEL_TARGET"/image/debug/ || exit 1
else
	cp -f  ./include/uImage   "$ROOT_DIR"/../"$MODEL_TARGET"/image/debug/ || exit 1
fi
rpm2cpio ../tztv-hawk-kmodules-devel-1-1.armv7l.rpm | cpio -di
tar xzf ./usr/include/debug/vmlinux.tgz -C "$ROOT_DIR"/../"$MODEL_TARGET"/image/debug/
rm -rf *

## perf
rpm2cpio ../tztv-hawk-kmodules-perf-1-1.armv7l.rpm | cpio -di
if [ $MODEL_TARGET = "Echo.P-Tizen" ]
then
	cp -f  ./perf/Image  "$ROOT_DIR"/../"$MODEL_TARGET"/image/perf/ || exit 1
else
	cp -f  ./perf/uImage  "$ROOT_DIR"/../"$MODEL_TARGET"/image/perf/ || exit 1
fi
rpm2cpio ../tztv-hawk-kmodules-devel-1-1.armv7l.rpm | cpio -di
tar xzf ./usr/include/perf/vmlinux.tgz -C "$ROOT_DIR"/../"$MODEL_TARGET"/image/perf/
rm -rf *


## release
rpm2cpio ../tztv-hawk-kmodules-release-1-1.armv7l.rpm | cpio -di
if [ $MODEL_TARGET = "Echo.P-Tizen" ]
then
	cp -f  ./release/Image  "$ROOT_DIR"/../"$MODEL_TARGET"/image/release || exit 1
else
	cp -f  ./release/uImage  "$ROOT_DIR"/../"$MODEL_TARGET"/image/release || exit 1
fi
rpm2cpio ../tztv-hawk-kmodules-devel-1-1.armv7l.rpm | cpio -di
tar xzf ./usr/include/release/vmlinux.tgz -C "$ROOT_DIR"/../"$MODEL_TARGET"/image/release/

rm -rf ./include ./usr ./lib ./bin ./etc ./opt


rpm2cpio ../tztv-hawk-kmodules-devel-1-1.armv7l.rpm | cpio -di 

cp -f  ./usr/include/debug/vdkernel_header.tgz ./usr/include/debug/kmod.tgz  "$ROOT_DIR"/../"$MODEL_TARGET"/image/debug/ || exit 1
# kUEP
cd "$ROOT_DIR"/../"$MODEL_TARGET"/image/debug/
tar xzf kmod.tgz
find . -name "*.ko" -exec curl -w %{http_code} -f  -F "file=@{}" -o {} http://10.40.68.68/isms/open/masterkey/signKUEP.do \;
tar czf kmod.tgz 3.10.30
rm -rf 3.10.30
cd -

cp -f  ./usr/include/perf/vdkernel_header.tgz ./usr/include/perf/kmod.tgz  "$ROOT_DIR"/../"$MODEL_TARGET"/image/perf || exit 1
cd "$ROOT_DIR"/../"$MODEL_TARGET"/image/perf/
tar xzf kmod.tgz
find . -name "*.ko" -exec curl -w %{http_code} -f  -F "file=@{}" -o {} http://10.40.68.68/isms/open/masterkey/signKUEP.do \;
tar czf kmod.tgz 3.10.30
rm -rf 3.10.30
cd -

cp -f  ./usr/include/release/vdkernel_header.tgz ./usr/include/release/kmod.tgz  "$ROOT_DIR"/../"$MODEL_TARGET"/image/release/ || exit 1
cd "$ROOT_DIR"/../"$MODEL_TARGET"/image/release/
tar xzf kmod.tgz
find . -name "*.ko" -exec curl -w %{http_code} -f  -F "file=@{}" -o {} http://10.40.68.68/isms/open/masterkey/signKUEP.do \;
tar czf kmod.tgz 3.10.30
rm -rf 3.10.30
cd -

##
## copy user header files
##
rm -rf "$ROOT_DIR"/../../vd_kernel-headers/"$MODEL_TARGET"/include 

tar xzf ./usr/include/debug/user_headers_install.tgz -C "$ROOT_DIR"/../../vd_kernel-headers/"$MODEL_TARGET"/ 

rm -rf ./usr

cd ..
rm -rf temp


cd $ROOT_DIR 

## generator



if [ "$MODEL_TARGET" == "PreHawk.P-Tizen" ] || [ "$MODEL_TARGET" == "Golf.P-Tizen" ]
then
        if [ -e ~/rsa_gen_golf ]
        then
		cp -f ../$MODEL_TARGET/image/debug/uImage ../$MODEL_TARGET/image/debug/uImage.tmp
		cp -f ../$MODEL_TARGET/image/perf/uImage ../$MODEL_TARGET/image/perf/uImage.tmp
		cp -f ../$MODEL_TARGET/image/release/uImage ../$MODEL_TARGET/image/release/uImage.tmp
		
                sudo ~/rsa_gen_golf ../$MODEL_TARGET/image/debug/uImage.tmp ../$MODEL_TARGET/image/debug/uImage
                sudo ~/rsa_gen_golf ../$MODEL_TARGET/image/perf/uImage.tmp ../$MODEL_TARGET/image/perf/uImage
                sudo ~/rsa_gen_golf ../$MODEL_TARGET/image/release/uImage.tmp ../$MODEL_TARGET/image/release/uImage
		sudo rm -f ../$MODEL_TARGET/image/debug/uImage.tmp
		sudo rm -f ../$MODEL_TARGET/image/perf/uImage.tmp
		sudo rm -f ../$MODEL_TARGET/image/release/uImage.tmp
        else
                echo " There is no generator "
                exit
        fi
elif [ "$MODEL_TARGET" == "Hawk.P-Tizen" ] || [ "MODEL_TARGET" ==  "Hawk.P-Tizen_BD" ]
then
        if [ -e ~/sig_gen ]
        then
		cp -f ../$MODEL_TARGET/image/debug/uImage ../$MODEL_TARGET/image/debug/uImage.tmp
		cp -f ../$MODEL_TARGET/image/perf/uImage ../$MODEL_TARGET/image/perf/uImage.tmp
		cp -f ../$MODEL_TARGET/image/release/uImage ../$MODEL_TARGET/image/release/uImage.tmp
		

                sudo ~/sig_gen HawkP ../$MODEL_TARGET/image/debug/uImage.tmp ../$MODEL_TARGET/image/debug/uImage
                sudo ~/sig_gen HawkP ../$MODEL_TARGET/image/perf/uImage.tmp ../$MODEL_TARGET/image/perf/uImage
                sudo ~/sig_gen HawkP ../$MODEL_TARGET/image/release/uImage.tmp ../$MODEL_TARGET/image/release/uImage
		sudo rm -f ../$MODEL_TARGET/image/debug/uImage.tmp
		sudo rm -f ../$MODEL_TARGET/image/perf/uImage.tmp
		sudo rm -f ../$MODEL_TARGET/image/release/uImage.tmp
        else
                echo " There is no generator "
                exit
        fi
elif [ "$MODEL_TARGET" == "Hawk.M-Tizen" ]
then
        if [ -e ~/sig_gen ]
        then
		cp -f ../$MODEL_TARGET/image/debug/uImage ../$MODEL_TARGET/image/debug/uImage.tmp
		cp -f ../$MODEL_TARGET/image/perf/uImage ../$MODEL_TARGET/image/perf/uImage.tmp
		cp -f ../$MODEL_TARGET/image/release/uImage ../$MODEL_TARGET/image/release/uImage.tmp
		

                sudo ~/sig_gen HawkM ../$MODEL_TARGET/image/debug/uImage.tmp ../$MODEL_TARGET/image/debug/uImage
                sudo ~/sig_gen HawkM ../$MODEL_TARGET/image/perf/uImage.tmp ../$MODEL_TARGET/image/perf/uImage
                sudo ~/sig_gen HawkM ../$MODEL_TARGET/image/release/uImage.tmp ../$MODEL_TARGET/image/release/uImage
		sudo rm -f ../$MODEL_TARGET/image/debug/uImage.tmp
		sudo rm -f ../$MODEL_TARGET/image/perf/uImage.tmp
		sudo rm -f ../$MODEL_TARGET/image/release/uImage.tmp
        else
                echo " There is no generator "
                exit
        fi

elif [ "$MODEL_TARGET" == "Echo.P-Tizen" ]
then
        if [ -e ~/cmac_gen ]
        then
		cp -f ../$MODEL_TARGET/image/debug/Image ../$MODEL_TARGET/image/debug/Image.tmp
		cp -f ../$MODEL_TARGET/image/perf/Image ../$MODEL_TARGET/image/perf/Image.tmp
		cp -f ../$MODEL_TARGET/image/release/Image ../$MODEL_TARGET/image/release/Image.tmp
		

                sudo ~/cmac_gen ../$MODEL_TARGET/image/debug/Image.tmp ../$MODEL_TARGET/image/debug/Image
                sudo ~/cmac_gen ../$MODEL_TARGET/image/perf/Image.tmp ../$MODEL_TARGET/image/perf/Image
                sudo ~/cmac_gen ../$MODEL_TARGET/image/release/Image.tmp ../$MODEL_TARGET/image/release/Image
		
		sudo rm -f ../$MODEL_TARGET/image/debug/Image.tmp
		sudo rm -f ../$MODEL_TARGET/image/perf/Image.tmp
		sudo rm -f ../$MODEL_TARGET/image/release/Image.tmp
        else
                echo " There is no generator "
                exit
        fi
elif [ "$MODEL_TARGET" == "Fox.P-Tizen" ]
then
        if [ -e ~/rsa_gen ]
        then
		cp -f ../$MODEL_TARGET/image/debug/uImage ../$MODEL_TARGET/image/debug/uImage.tmp
		cp -f ../$MODEL_TARGET/image/perf/uImage ../$MODEL_TARGET/image/perf/uImage.tmp
		cp -f ../$MODEL_TARGET/image/release/uImage ../$MODEL_TARGET/image/release/uImage.tmp
		

                sudo ~/rsa_gen ../$MODEL_TARGET/image/debug/uImage.tmp ../$MODEL_TARGET/image/debug/uImage
                sudo ~/rsa_gen ../$MODEL_TARGET/image/perf/uImage.tmp ../$MODEL_TARGET/image/perf/uImage
                sudo ~/rsa_gen ../$MODEL_TARGET/image/perf/uImage.tmp ../$MODEL_TARGET/image/release/uImage

		sudo rm -f ../$MODEL_TARGET/image/debug/uImage.tmp
		sudo rm -f ../$MODEL_TARGET/image/perf/uImage.tmp
		sudo rm -f ../$MODEL_TARGET/image/release/uImage.tmp
        else
                echo " There is no generator "
                exit
        fi
fi


echo "static" > autobuild.conf

echo "============================================="
echo "Build_Release.sh is done"
echo "============================================="
