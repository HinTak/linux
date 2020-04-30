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
echo "  1. Hawk.P-TIZEN"
echo "  2. Hawk.M-TIZEN"
echo "================================================================"
echo " 21. Jazz.M-TIZEN"
echo " 23. NT16M-Tizen"
echo " 24. Hawk.A-TIZEN"
#echo " 25. Hawk.P-Tizen_BD"
echo "================================================================"
#echo " 90. Echo.P-TIZEN"
#echo " 91. Fox.P-TIZEN"
#echo " 92. Golf.P-TIZEN"
#echo " 93. Hawk.P-Tizen_SBB"
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
elif [ "$target" == "25" ]; then
	PRODUCT_TARGET=BD
	MODEL_TARGET=Hawk.P-Tizen_BD
elif [ "$target" == "90" ]; then
	PRODUCT_TARGET=DTV
	MODEL_TARGET=Echo.P-Tizen
elif [ "$target" == "91" ]; then
	PRODUCT_TARGET=DTV
	MODEL_TARGET=Fox.P-Tizen
elif [ "$target" == "92" ]; then
	PRODUCT_TARGET=DTV
	MODEL_TARGET=Golf.P-Tizen
elif [ "$target" == "93" ]; then
	PRODUCT_TARGET=SBB
	MODEL_TARGET=Hawk.P-Tizen_SBB
else
	echo "Wrong input"
	TARGET_INPUT_ERR=$TRUE
fi

if [ "$MODEL_TARGET" = "Hawk.M-Tizen" ]; then
	echo ""
	echo " BSP Release Process"
	echo "================================================================"
	echo " Select DTV Type for $MODEL_TARGET"
	echo "================================================================"
	echo " 1. UHD"
	echo " 2. FHD"
	echo "================================================================"
	read answer

	if [ "$answer" = "1" ]; then
		MODEL_TYPE="UHD"
	elif [ "$answer" = "2" ]; then
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
if [ $answer == "y" ]; then
	echo "Input KERNEL VERSION NUMBER"
	read answer
	VERSION=$answer

	echo "Input PATCH VERSION NUMBER"
	read answer
	PATCH_NUM=$answer

	KERNEL_VERSION_MSG="$PATCH_NUM, $PRODUCT_TARGET, $MODEL_TARGET, debug, MAIN"

	echo "#define DTV_KERNEL_VERSION "\"$VERSION, debug\" > include/linux/vdlp_version_"$MODEL_TARGET".h
	echo "#define DTV_LAST_PATCH "\"$KERNEL_VERSION_MSG\" >> include/linux/vdlp_version_"$MODEL_TARGET".h
fi

echo "========================================================================"
cat ./include/linux/vdlp_version_"$MODEL_TARGET".h
echo "========================================================================"



echo "========================================================================"
echo " Do you want to change kernel config (y/n)?                             "
echo "========================================================================"

read answer
if [ $answer == "y" ]; then
	./BUILD_Config.sh "$MODEL_TARGET"
elif [ $answer == "n" ]; then
	echo "keep going"
fi


cp  -f  ../DocTizen/DocForReleaseBSP/GBS.Conf.File/"$MODEL_TARGET"."$MODEL_TYPE".gbs.conf ~/.gbs.conf

cd ../

if [ -x ~/bin/vbs_build.sh ]; then
	~/bin/vbs_build.sh
else
	echo "================================================================="
	echo "There is no ~/bin/vbs_build.sh "
	echo "make your own vbs_build.sh like below " 
	echo "vbs -p P4_SERVER:PORT -P your_p4_pw -u your_P4ID -l your_P4_workspace build -A armv71 --include-all --clean-repos "
	echo "================================================================="
	exit 
fi
cd -

if [ $MODEL_TARGET = "PreHawk.P-Tizen" ]; then
	LOCAL_REPOS=~/GBS-ROOT/local/repos/preHawk
elif [ $MODEL_TARGET = "Golf.P-Tizen" ]; then
	LOCAL_REPOS=~/GBS-ROOT/local/repos/tztv_2.2.1_prehawk
elif [ $MODEL_TARGET = "Hawk.M-Tizen" ]; then
	if [ $MODEL_TYPE = "UHD" ]; then
		LOCAL_REPOS=~/GBS-ROOT/local/repos/hawkM_uhd
	elif [ $MODEL_TYPE = "FHD" ]; then
		LOCAL_REPOS=~/GBS-ROOT/local/repos/hawkM_fhd
	else
		echo "Wrong Model Type..."
		exit 1
	fi
elif [ $MODEL_TARGET = "Hawk.P-Tizen" ]; then
	LOCAL_REPOS=~/GBS-ROOT/local/repos/hawkP
elif [ $MODEL_TARGET = "Hawk.P-Tizen_SBB" ]; then
	LOCAL_REPOS=~/GBS-ROOT/local/repos/hawk_sbb
elif [ $MODEL_TARGET = "Echo.P-Tizen" ]; then
	LOCAL_REPOS=~/GBS-ROOT/local/repos/echoP
elif [ $MODEL_TARGET = "Fox.P-Tizen" ]; then
	LOCAL_REPOS=~/GBS-ROOT/local/repos/foxP
elif [ $MODEL_TARGET = "Hawk.A-Tizen" ]; then
	LOCAL_REPOS=~/GBS-ROOT/local/repos/hawkA
elif [ "$MODEL_TARGET" = "NT16M-Tizen" ]; then
	LOCAL_REPOS=~/GBS-ROOT/local/repos/NT16M
elif [ $MODEL_TARGET = "Jazz.M-Tizen" ]; then
	LOCAL_REPOS=~/GBS-ROOT/local/repos/jazzM
else
	echo "Wrong Model Target..."
	exit 1
fi

mkdir -p $LOCAL_REPOS/armv7l/RPMS
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
cp -f  ./include/uImage   "$ROOT_DIR"/../"$MODEL_TARGET"/image/debug/ || exit 1
rm -rf *

rpm2cpio ../tztv-hawk-kmodules-devel-1-1.armv7l.rpm | cpio -di
tar xzf ./usr/include/debug/vmlinux.tgz -C "$ROOT_DIR"/../"$MODEL_TARGET"/image/debug/
rm -rf *

## perf
rpm2cpio ../tztv-hawk-kmodules-perf-1-1.armv7l.rpm | cpio -di
cp -f  ./perf/uImage  "$ROOT_DIR"/../"$MODEL_TARGET"/image/perf/ || exit 1
rm -rf *

rpm2cpio ../tztv-hawk-kmodules-devel-1-1.armv7l.rpm | cpio -di
tar xzf ./usr/include/perf/vmlinux.tgz -C "$ROOT_DIR"/../"$MODEL_TARGET"/image/perf/
rm -rf *

## release
rpm2cpio ../tztv-hawk-kmodules-release-1-1.armv7l.rpm | cpio -di
cp -f  ./release/uImage  "$ROOT_DIR"/../"$MODEL_TARGET"/image/release || exit 1
rm -rf ./include ./usr ./lib ./bin ./etc ./opt

rpm2cpio ../tztv-hawk-kmodules-devel-1-1.armv7l.rpm | cpio -di
tar xzf ./usr/include/release/vmlinux.tgz -C "$ROOT_DIR"/../"$MODEL_TARGET"/image/release/
rm -rf ./include ./usr ./lib ./bin ./etc ./opt

rpm2cpio ../tztv-hawk-kmodules-devel-1-1.armv7l.rpm | cpio -di

if [ -d ./usr/include/debug ]; then
	cp -f ./usr/include/debug/vdkernel_header.tgz ./usr/include/debug/kmod.tgz  "$ROOT_DIR"/../"$MODEL_TARGET"/image/debug/ || exit 1
	# kUEP
        cd "$ROOT_DIR"/../"$MODEL_TARGET"/image/debug/
	tar xzf kmod.tgz
	find . -name "*.ko" -exec curl -w %{http_code}  -f -F "file=@{}" -F "year=2016"  -o {} http://10.40.68.68/isms/open/masterkey/signKUEP.do \;
	tar czf kmod.tgz 3.10.30
	rm -rf 3.10.30
	cd -
fi
if [ -d ./usr/include/perf ]; then
	cp -f ./usr/include/perf/vdkernel_header.tgz ./usr/include/perf/kmod.tgz  "$ROOT_DIR"/../"$MODEL_TARGET"/image/perf || exit 1
	cd "$ROOT_DIR"/../"$MODEL_TARGET"/image/perf/
        tar xzf kmod.tgz
	find . -name "*.ko" -exec curl -w %{http_code}  -f -F "file=@{}" -F "year=2016"  -o {} http://10.40.68.68/isms/open/masterkey/signKUEP.do \;
        tar czf kmod.tgz 3.10.30
        rm -rf 3.10.30
        cd -
fi
if [ -d ./usr/include/release ]; then
	cp -f ./usr/include/release/vdkernel_header.tgz ./usr/include/release/kmod.tgz  "$ROOT_DIR"/../"$MODEL_TARGET"/image/release/ || exit 1
        cd "$ROOT_DIR"/../"$MODEL_TARGET"/image/release/
        tar xzf kmod.tgz 
	find . -name "*.ko" -exec curl -w %{http_code}  -f -F "file=@{}" -F "year=2016"  -o {} http://10.40.68.68/isms/open/masterkey/signKUEP.do \;
        tar czf kmod.tgz 3.10.30
        rm -rf 3.10.30
        cd -
fi

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
if [ "$MODEL_TARGET" == "Hawk.P-Tizen" ]; then
	if [ -e ~/sig_gen ]; then
		cp -f ../$MODEL_TARGET/image/debug/uImage ../$MODEL_TARGET/image/debug/uImage.tmp
		cp -f ../$MODEL_TARGET/image/perf/uImage ../$MODEL_TARGET/image/perf/uImage.tmp
		cp -f ../$MODEL_TARGET/image/release/uImage ../$MODEL_TARGET/image/release/uImage.tmp
		~/sig_gen HawkP ../$MODEL_TARGET/image/debug/uImage.tmp ../$MODEL_TARGET/image/debug/uImage
		~/sig_gen HawkP ../$MODEL_TARGET/image/perf/uImage.tmp ../$MODEL_TARGET/image/perf/uImage
		~/sig_gen HawkP ../$MODEL_TARGET/image/release/uImage.tmp ../$MODEL_TARGET/image/release/uImage
	else
		echo "*================================================================*"
		echo "*                                                                *"
		echo "*	There is no generator                                          *"
		echo "* kernel image was made but not applied by generator             *"
		echo "* DO NOT USE FOR RELEASE                                         *"
		echo "*================================================================*"
		exit
	fi
elif [ "$MODEL_TARGET" == "Hawk.M-Tizen" ]; then
	if [ -e ~/sig_gen ]; then
		cp -f ../$MODEL_TARGET/image/debug/uImage ../$MODEL_TARGET/image/debug/uImage.tmp
		cp -f ../$MODEL_TARGET/image/perf/uImage ../$MODEL_TARGET/image/perf/uImage.tmp
		cp -f ../$MODEL_TARGET/image/release/uImage ../$MODEL_TARGET/image/release/uImage.tmp
		~/sig_gen HawkM ../$MODEL_TARGET/image/debug/uImage.tmp ../$MODEL_TARGET/image/debug/uImage
		~/sig_gen HawkM ../$MODEL_TARGET/image/perf/uImage.tmp ../$MODEL_TARGET/image/perf/uImage
		~/sig_gen HawkM ../$MODEL_TARGET/image/release/uImage.tmp ../$MODEL_TARGET/image/release/uImage
	else	
		echo "*================================================================*"
		echo "*                                                                *"
		echo "*	There is no generator                                          *"
		echo "* kernel image was made but not applied by generator             *"
		echo "* DO NOT USE FOR RELEASE                                         *"
		echo "*================================================================*"
		exit
	fi
elif [ "$MODEL_TARGET" == "Hawk.A-Tizen" ]; then
	if [ -e ~/sig_gen ]; then
		cp -f ../$MODEL_TARGET/image/debug/uImage ../$MODEL_TARGET/image/debug/uImage.tmp
		cp -f ../$MODEL_TARGET/image/perf/uImage ../$MODEL_TARGET/image/perf/uImage.tmp
		cp -f ../$MODEL_TARGET/image/release/uImage ../$MODEL_TARGET/image/release/uImage.tmp

		~/sig_gen HawkA ../$MODEL_TARGET/image/debug/uImage.tmp ../$MODEL_TARGET/image/debug/uImage
		~/sig_gen HawkA ../$MODEL_TARGET/image/perf/uImage.tmp ../$MODEL_TARGET/image/perf/uImage
		~/sig_gen HawkA ../$MODEL_TARGET/image/release/uImage.tmp ../$MODEL_TARGET/image/release/uImage
	else
		echo "*================================================================*"
		echo "*                                                                *"
		echo "*	There is no generator                                          *"
		echo "* kernel image was made but not applied by generator             *"
		echo "* DO NOT USE FOR RELEASE                                         *"
		echo "*================================================================*"
		exit
	fi
elif [ "$MODEL_TARGET" == "NT16M-Tizen" ]; then
	if [ -e $ROOT_DIR/sig_nvt.sh ]; then
		cp -f ../$MODEL_TARGET/image/debug/uImage ../$MODEL_TARGET/image/debug/uImage.tmp
		cp -f ../$MODEL_TARGET/image/perf/uImage ../$MODEL_TARGET/image/perf/uImage.tmp
		cp -f ../$MODEL_TARGET/image/release/uImage ../$MODEL_TARGET/image/release/uImage.tmp

		$ROOT_DIR/sig_nvt.sh ../$MODEL_TARGET/image/debug/uImage.tmp ../$MODEL_TARGET/image/debug/uImage
		$ROOT_DIR/sig_nvt.sh ../$MODEL_TARGET/image/perf/uImage.tmp ../$MODEL_TARGET/image/perf/uImage
		$ROOT_DIR/sig_nvt.sh ../$MODEL_TARGET/image/release/uImage.tmp ../$MODEL_TARGET/image/release/uImage
	else
		echo "*================================================================*"
		echo "*                                                                *"
		echo "*	There is no generator                                          *"
		echo "* kernel image was made but not applied by generator             *"
		echo "* DO NOT USE FOR RELEASE                                         *"
		echo "*================================================================*"
		exit
	fi
elif [ "$MODEL_TARGET" == "Jazz.M-Tizen" ]; then
	if [ -e ~/sig_gen ]; then
		cp -f ../$MODEL_TARGET/image/debug/uImage ../$MODEL_TARGET/image/debug/uImage.tmp
		cp -f ../$MODEL_TARGET/image/perf/uImage ../$MODEL_TARGET/image/perf/uImage.tmp
		cp -f ../$MODEL_TARGET/image/release/uImage ../$MODEL_TARGET/image/release/uImage.tmp
		~/sig_gen JazzM ../$MODEL_TARGET/image/debug/uImage.tmp ../$MODEL_TARGET/image/debug/uImage
		~/sig_gen JazzM ../$MODEL_TARGET/image/perf/uImage.tmp ../$MODEL_TARGET/image/perf/uImage
		~/sig_gen JazzM ../$MODEL_TARGET/image/release/uImage.tmp ../$MODEL_TARGET/image/release/uImage
	else	
		echo "*================================================================*"
		echo "*                                                                *"
		echo "*	There is no generator                                          *"
		echo "* kernel image was made but not applied by generator             *"
		echo "* DO NOT USE FOR RELEASE                                         *"
		echo "*================================================================*"
		exit
	fi
fi

# Remove temporal uImage
rm -f ../$MODEL_TARGET/image/debug/uImage.tmp
rm -f ../$MODEL_TARGET/image/perf/uImage.tmp
rm -f ../$MODEL_TARGET/image/release/uImage.tmp

echo "static" > autobuild.conf

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

	if [ "$1" == debug ]
	then
		RELEASE_DEBUG_CHECK="debug"
	elif [ "$1" == perf ]
	then 
		RELEASE_DEBUG_CHECK="perf"
	elif [ "$1" == release ]
	then 
		RELEASE_DEBUG_CHECK="release"
	fi
	
		
	if [ $MODEL_TARGET == "Echo.P-Tizen" ];	then 
		cp -f ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/Image .
		chmod 777 Image
	else 	
		cp -f ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/uImage .
		chmod 777 uImage
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
		~/rsa_gen_golf uImage uImage.tmp 
		mv -f uImage.tmp uImage
	elif [ $MODEL_TARGET == "Echo.P-Tizen" ]
	then
		if [ -x ~/cmac_gen ]; then SIG_GEN_FOUND=1; fi
		~/cmac_gen Image Image.tmp 
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
		~/rsa_gen uImage uImage.tmp 
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
	echo "============================================================================="
	echo "Signature "$MODEL_TARGET"  : "$RELEASE_DEBUG_CHECK" check  :  result : OK"
	echo "============================================================================="
	echo ""
}


check_signature debug
check_signature perf 
check_signature release 


echo "============================================="
echo "Build_Release.sh is done"
echo "============================================="
