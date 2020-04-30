#!/bin/bash

######################################################################################
# FUNCTION SECTION
######################################################################################

set_value()
{
	ROOT_DIR=$PWD

	FALSE=0
	TRUE=1
	INIT=NULL
 
	TARGET_INPUT_ERR=$INIT
	SELECT_INPUT_ERR=$INIT
	ACTION_INPUT_ERR=$INIT
	SELECT_RELEASE_MODE_ERR=$INIT
	CHANGE_TARGET=$INIT
  
	PRODUCT_TARGET=$INIT
	MODEL_TARGET=$INIT  
	CONFIG_PRODUCT_TARGET=$INIT
	CONFIG_MODEL_TARGET=$INIT
  
	FIRST_RUN_ENV=$INIT
	CORE_TYPE=$INIT
  
	PRINT_MSG=$INIT
	PRINT_MSG_END=$INIT
  
	CONFIG_FILE_PATH=$INIT
  
	RELEASE_MODE=$INIT
	ADDITIONAL_FUNCTION=$INIT #This can be bootchart, etc..
	RELEASE_DEBUG_CHECK=$INIT
	RELEASE_MODE_FILE=$INIT
  
	KERNEL_VERSION_MSG=$INIT
	ROOTFS_VERSION_MSG=$INIT
  
	TOOLCHAIN_PREFIX=$INIT
  
	RFS_LinuStoreIII_FSR_RTM="RFS_3.0.0_b047-LinuStoreIII_1.2.0_b040-FSR_1.2.1p1_b146_RTM"
	BSP_P4_DIR=../../../BSP
}

print_msg()
{
	echo ""
	echo "================================================================"
	echo " $PRINT_MSG"
	echo "================================================================"
	echo ""
}

main_menu()
{
	echo ""
	echo " BSP Release Process"
	echo "============================================================================"
	echo " Select target for Release"
	echo "============================================================================"
	#echo " 1. PreHawk"
	#echo " 2. PreHawk_AV"
	echo " 3. HawkP AV Orsay"
	echo " 4. HawkP EW (Early Warning)"
	echo " 5. HawkM EW (Early Warning)"
	echo " 6. HAWKA" 
	echo "============================================================================"
	read target
  
	if [ "$target" == "1" ]
	then
		PRODUCT_TARGET=DTV
		MODEL_TARGET=PreHawk
		CORE_TYPE=ARM
		TOOLCHAIN_PREFIX=arm-v7a15a7v5r1-linux-gnueabi
	elif [ "$target" == "2" ]
	then
		PRODUCT_TARGET=AV
		MODEL_TARGET=PreHawk_AV
		CORE_TYPE=ARM
		TOOLCHAIN_PREFIX=arm-v7a15v4r3-linux-gnueabi
	elif [ "$target" == "3" ]
	then
		PRODUCT_TARGET=AV
		MODEL_TARGET=HawkP
		CORE_TYPE=ARM
		TOOLCHAIN_PREFIX=arm-v7a15a7v5r2-linux-gnueabi
	elif [ "$target" == "4" ]
	then
		PRODUCT_TARGET=EW
		MODEL_TARGET=HawkP_TV
		CORE_TYPE=ARM
		TOOLCHAIN_PREFIX=arm-v7a15a7v5r1-linux-gnueabi
	elif [ "$target" == "5" ]
	then
		PRODUCT_TARGET=EW
		MODEL_TARGET=HawkM
		CORE_TYPE=ARM
		TOOLCHAIN_PREFIX=arm-v7a12v5r2-linux-gnueabi
        elif [ "$target" == "6" ]
        then
                PRODUCT_TARGET=AV
                MODEL_TARGET=HAWKA
                CORE_TYPE=ARM
                TOOLCHAIN_PREFIX=arm-v7a7v5r2-linux-gnueabi
	else
		echo "Wrong input"
		TARGET_INPUT_ERR=$TRUE	
	fi
}

check_compile_env()
{
	if [ -e .config ] 
	then
		if [ $CORE_TYPE == "ARM" ]
		then
			test -f .PreHawk
			if [ $? == 0 ]
			then
				CONFIG_PRODUCT_TARGET=DTV
				CONFIG_MODEL_TARGET=PreHawk
			fi

			test -f .PreHawk_AV
			if [ $? == 0 ]
			then
				CONFIG_PRODUCT_TARGET=AV
				CONFIG_MODEL_TARGET=PreHawk_AV
			fi

			test -f .HawkP
			if [ $? == 0 ]
			then
				CONFIG_PRODUCT_TARGET=AV
				CONFIG_MODEL_TARGET=HawkP
			fi

			test -f .HawkP_TV
			if [ $? == 0 ]
			then
				CONFIG_PRODUCT_TARGET=EW
				CONFIG_MODEL_TARGET=HawkP_TV
			fi

			test -f .HawkM
			if [ $? == 0 ]
			then
				CONFIG_PRODUCT_TARGET=EW
				CONFIG_MODEL_TARGET=HawkM
			fi

                        test -f .HAWKA
                        if [ $? == 0 ]
                        then
                                CONFIG_PRODUCT_TARGET=AV
                                CONFIG_MODEL_TARGET=HAWKA
                        fi
		fi
	fi

	if [ $CONFIG_PRODUCT_TARGET != $INIT ] && [ $CONFIG_MODEL_TARGET != $INIT ]
	then
		if [ $PRODUCT_TARGET != $CONFIG_PRODUCT_TARGET ] || [ $MODEL_TARGET != $CONFIG_MODEL_TARGET ]
		then
			echo "============================================================================"
			echo " Now Select Target      : $PRODUCT_TARGET, $MODEL_TARGET"
			echo " Before Selected Target : $CONFIG_PRODUCT_TARGET, $CONFIG_MODEL_TARGET"  
			echo " Do you want to change Target?"
			echo "============================================================================"
			echo " 1. YES"
			echo " 2. NO"
			echo "============================================================================"
			read select

			if [ "$select" == "1" ]
			then
				CHANGE_TARGET=$TRUE	
			elif [ "$select" == "2" ]
			then
				CHANGE_TARGET=$FALSE
				SELECT_INPUT_ERR=$TRUE     		
			else
				echo "Wrong input"
				SELECT_INPUT_ERR=$TRUE
			fi
		fi
	else
		FIRST_RUN_ENV=$TRUE
	fi
}

set_compile_env()
{
	cd $ROOT_DIR

	echo "Copy [vdlp_version_"$MODEL_TARGET".h -> vdlp_version.h]"
	test -f include/linux/vdlp_version_$MODEL_TARGET.h
	if [ $? == 0 ]
	then
		cd ./include/linux
		rm -Rf vdlp_version.h
		cp vdlp_version_$MODEL_TARGET.h vdlp_version.h
	else
		echo "vdlp_version_"$MODEL_TARGET".h not exist!!"
		exit
	fi

	cd $ROOT_DIR
}

copy_config_file()
{
	if [ $CORE_TYPE == "ARM" ]
	then
		if [ $RELEASE_DEBUG_CHECK == "product" ]
		then
			CONFIG_FILE_PATH="./arch/arm/configs/"$MODEL_TARGET"_product_defconfig"
			RELEASE_MODE_FILE=".product"
		elif [ $RELEASE_DEBUG_CHECK == "debug" ]
		then
			CONFIG_FILE_PATH="./arch/arm/configs/"$MODEL_TARGET"_debug_defconfig"
			RELEASE_MODE_FILE=".debug"
		elif [ $RELEASE_DEBUG_CHECK == "perf" ]
		then
			CONFIG_FILE_PATH="./arch/arm/configs/"$MODEL_TARGET"_perf_defconfig"
			RELEASE_MODE_FILE=".perf"
                elif [ $RELEASE_DEBUG_CHECK == "kasan" ]
                then
                        CONFIG_FILE_PATH="./arch/arm/configs/"$MODEL_TARGET"_kasan_defconfig"
                        RELEASE_MODE_FILE=".kasan"
		fi
	fi
  
	test -f $CONFIG_FILE_PATH
	if [ $? != 0 ] 
	then
		PRINT_MSG=" Not find $MODEL_TARGET config file..."
		print_msg    
		exit
	else
		rm -f .config
		rm -f $RELEASE_MODE_FILE
		rm -f ."$MODEL_TARGET"
		cp $CONFIG_FILE_PATH .config
		cp $CONFIG_FILE_PATH $RELEASE_MODE_FILE
		cp $CONFIG_FILE_PATH ."$MODEL_TARGET"
	fi	

	make oldconfig
}

select_release_mode()
{
	echo "============================================================================"
	echo " Select Release Mode..."
	echo "============================================================================"

	echo " 1. Debug mode"
	#if false
	#then
	        if [ $MODEL_TARGET == "HawkP" ] || [ $MODEL_TARGET == "HawkM" ] || [ $MODEL_TARGET == "HAWKA" ]
                then
	                echo " 2. Perf mode"
                fi

                if [ $MODEL_TARGET == "HawkP" ] || [ $MODEL_TARGET == "HawkM" ] || [ $MODEL_TARGET == "HAWKA" ]
                then
	                echo " 3. Product mode"
                fi
	#fi

	if [ $MODEL_TARGET == "HawkP_TV" ] || [ $MODEL_TARGET == "HawkM" ]
	then	
        echo " 4. KASAN mode"
	fi	

	echo "============================================================================"
	read mode


	if [ "$mode" == "1" ]
	then
		RELEASE_DEBUG_CHECK=debug
		RELEASE_MODE=debug
	elif [ "$mode" == "2" ]
	then
		RELEASE_DEBUG_CHECK=perf
		RELEASE_MODE=perf	
	elif [ "$mode" == "3" ]
	then
		RELEASE_DEBUG_CHECK=product
		RELEASE_MODE=product
        elif [ "$mode" == "4" ]
        then
                RELEASE_DEBUG_CHECK=kasan
                RELEASE_MODE=kasan
	else
		echo "Wrong input"
		SELECT_RELEASE_MODE_ERR=$TRUE
	fi
}

check_release_mode()
{
	if [ $RELEASE_DEBUG_CHECK == $INIT ]
	then

		test -f .debug
		if [ $? == 0 ] 
		then
			RELEASE_DEBUG_CHECK=debug
			RELEASE_MODE=debug
		fi
 
		test -f .perf
		if [ $? == 0 ] 
		then
			RELEASE_DEBUG_CHECK=perf
			RELEASE_MODE=perf
		fi
		
 		test -f .product
		if [ $? == 0 ] 
		then
			RELEASE_DEBUG_CHECK=product
			RELEASE_MODE=product
		fi

                test -f .kasan
                if [ $? == 0 ]
                then
                        RELEASE_DEBUG_CHECK=kasan
                        RELEASE_MODE=kasan
		fi
	fi
}

set_bootchart_config()
{
	find . -name ".config" | xargs perl -pi -e 's/# CONFIG_BOOTPROFILE is not set/CONFIG_BOOTPROFILE=y/g'
#	find . -name ".config" | xargs perl -pi -e 's/# CONFIG_MODULE_VER_CHECK_SKIP is not set/CONFIG_MODULE_VER_CHECK_SKIP=y/g'

	make oldconfig
}

action_menu()
{
	echo "========================================================"
	echo " Select action for target $PRODUCT_TARGET, $MODEL_TARGET"
	echo "========================================================"

	if [ $CHANGE_TARGET == $TRUE ] || [ $FIRST_RUN_ENV == $TRUE ]
	then
		echo " 1. Release env Setting (Required menu)"
	else
		if [ $RELEASE_DEBUG_CHECK == "debug" ]
		then
			echo " 2. Debug Ver. Release"
		elif [ $RELEASE_DEBUG_CHECK == "perf" ]
		then
			echo " 3. Perf Ver. Release"
		elif [ $RELEASE_DEBUG_CHECK == "product" ]
		then
			echo " 4. Product Ver. Release"
		fi
		#echo " 5. Bootchart Ver. Release"
	fi
	
	echo " 6. Rootfs Release"
	echo " 7. Onboot Release"

        if [ $MODEL_TARGET == "HawkP_TV" ] || [ $MODEL_TARGET == "HawkM" ]
        then
		if [ $CHANGE_TARGET != $TRUE ] && [ $FIRST_RUN_ENV != $TRUE ]
                then
                        if [ $RELEASE_DEBUG_CHECK == "kasan" ]
                        then
		        	echo " 8. KASAN Ver. Release"
			fi
		fi
	fi

	echo " 9. Copy config"

	echo "========================================================"
	read action
  
	if [ "$action" == "1" ]
	then
		if [ $CHANGE_TARGET == $TRUE ] || [ $FIRST_RUN_ENV == $TRUE ]
		then
			until [ "$SELECT_RELEASE_MODE_ERR" == "$FALSE" ]
			do
				SELECT_RELEASE_MODE_ERR=$FALSE
				select_release_mode
			done
     
			PRINT_MSG=" Release env for $PRODUCT_TARGET, $MODEL_TARGET Setting START..."
			print_msg    

			set_compile_env
			copy_config_file

			PRINT_MSG=" Release env for $PRODUCT_TARGET, $MODEL_TARGET Setting END..."
			print_msg 
	
			exit 0
		else
			echo "Wrong input"
			ACTION_INPUT_ERR=$TRUE
		fi
	elif [ "$action" == "2" ]
	then
		if [ $RELEASE_DEBUG_CHECK == "product" ] || [ $RELEASE_DEBUG_CHECK == "perf" ] || [ $RELEASE_DEBUG_CHECK == "kasan" ]
		then
			echo "Wrong input"
			ACTION_INPUT_ERR=$TRUE
		else
			PRINT_MSG=" $PRODUCT_TARGET, $MODEL_TARGET Debug Ver. Release START..."
			PRINT_MSG_END=" $PRODUCT_TARGET, $MODEL_TARGET Debug Ver. Release END..."    
			print_msg 
		fi
	elif [ "$action" == "3" ]
	then
		if [ $RELEASE_DEBUG_CHECK == "product" ] || [ $RELEASE_DEBUG_CHECK == "debug" ] || [ $RELEASE_DEBUG_CHECK == "kasan" ]
		then
			echo "Wrong input"
			ACTION_INPUT_ERR=$TRUE
		else
			PRINT_MSG=" $PRODUCT_TARGET, $MODEL_TARGET Perf Ver. Release START..."
			PRINT_MSG_END=" $PRODUCT_TARGET, $MODEL_TARGET Perf Ver. Release END..."    
			print_msg 
		fi
	elif [ "$action" == "4" ]
	then
		if [ $RELEASE_DEBUG_CHECK == "debug" ] || [ $RELEASE_DEBUG_CHECK == "perf" ]  || [ $RELEASE_DEBUG_CHECK == "kasan" ]
		then
			echo "Wrong input"
			ACTION_INPUT_ERR=$TRUE
		else   
			PRINT_MSG=" $PRODUCT_TARGET, $MODEL_TARGET Product Ver. Release START..."
			PRINT_MSG_END=" $PRODUCT_TARGET, $MODEL_TARGET Product Ver. Release END..."    
			print_msg 
		fi

        elif [ "$action" == "8" ]
        then
                if [ $RELEASE_DEBUG_CHECK == "product" ] || [ $RELEASE_DEBUG_CHECK == "debug" ] || [ $RELEASE_DEBUG_CHECK == "perf" ]
                then
                        echo "Wrong input"
                        ACTION_INPUT_ERR=$TRUE
                else
                        PRINT_MSG=" $PRODUCT_TARGET, $MODEL_TARGET KASAN Ver. Release START..."
                        PRINT_MSG_END=" $PRODUCT_TARGET, $MODEL_TARGET KASAN Ver. Release END..."
                        print_msg
                fi

	elif [ "$action" == "5" ]
	then
		PRINT_MSG=" $PRODUCT_TARGET, $MODEL_TARGET Bootchart Ver. Release START..."
		PRINT_MSG_END=" $PRODUCT_TARGET, $MODEL_TARGET Bootchart Ver. Release END..."    
		print_msg

		ADDITIONAL_FUNCTION=bootchart

		set_bootchart_config
	elif [ "$action" == "6" ]
	then
		
		PRINT_MSG=" $PRODUCT_TARGET, $MODEL_TARGET Rootfs Release START..."
		PRINT_MSG_END=" $PRODUCT_TARGET, $MODEL_TARGET Rootfs Release END..."    
		print_msg
		select_release_mode
		prepare_rootfs
		rootfs_version_change	

		echo "Do you want to re-build ROOTFS utils? [y/n]"
		read answer
		if [ $answer == "y" ]
		then
			make_rootfs_util
		fi
		
		cd $ROOT_DIR

		test -d ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"
		if [ $? != 0 ]
		then
			mkdir -p ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"
			sync
		fi

		cd ../$MODEL_TARGET/source/rootfs-src
		sudo tar cvzf ./rootfs-"$MODEL_TARGET"_"$RELEASE_MODE".tgz ./rootfs-vdlinux
		cd -

		sudo $TOOLCHAIN_PREFIX-strip ../$MODEL_TARGET/source/rootfs-src/rootfs-vdlinux/mtd_exe/lib/*.*

		test -d rootfs.img
		if [ $? != 0 ]
		then
			sudo rm -f rootfs.img
			sync
		fi

		echo "making rootfs image"

		if [ $MODEL_TARGET == "HawkP" ]
                then
			if [ $RELEASE_MODE == "debug" ] || [ $RELEASE_MODE == "perf" ] || [ $RELEASE_MODE == "product" ]
			then
				cd $BSP_P4_DIR/Tools/vdfs-tools
				make clean
                	        make
				cd $ROOT_DIR
				sudo $BSP_P4_DIR/Tools/vdfs-tools/mkfs.vdfs -q config_rootfs.conf -H $BSP_P4_DIR/Tools/CIP_UTIL/res/prkey_2048.txt -P $BSP_P4_DIR/Tools/CIP_UTIL/res/pukey_2048.txt -r ../$MODEL_TARGET/source/rootfs-src/rootfs-vdlinux/ -i rootfs.img --no-all-root
			fi
		else
			sudo mksquashfs4.2.a ../$MODEL_TARGET/source/rootfs-src/rootfs-vdlinux/ rootfs.img
		fi

		if [ $? != "0" ]
		then
			echo "================================================================"
			echo "rootfs.img : mksquashfs4.2.a failed !!!!!!!!!!!!!!!!!!!!!!!!!"
			echo "================================================================"
			exit
		fi

		sudo chmod +rw rootfs.img
		sudo mv -f rootfs.img ../$MODEL_TARGET/image/"$RELEASE_MODE"/rootfs.img
		sudo rm ../$MODEL_TARGET/source/rootfs-src/rootfs-vdlinux -rf

		cd $ROOT_DIR
		./Orsay.Clean.sh

		echo "=============================================="
		echo "unsquashfs4.2.a test routine"
		echo "=============================================="
		cd ../$MODEL_TARGET/image/"$RELEASE_MODE"/

                if [ $MODEL_TARGET == "HawkP" ]
                then
			cd $ROOT_DIR
			sudo $BSP_P4_DIR/Tools/vdfs-tools/unpack.vdfs ../$MODEL_TARGET/image/"$RELEASE_MODE"/rootfs.img
			cd $BSP_P4_DIR/Tools/vdfs-tools
                        make clean
		else
			sudo unsquashfs4.2.a rootfs.img
		fi

		if [ $? != "0" ]
		then
			echo "================================================================"
			echo "Test rootfs.img : unsquashfs4.2.a failed !!!!!!!!!!!!!!!!!!!!!!!!!"
			echo "================================================================"
		fi

                if [ $MODEL_TARGET == "HawkP" ]
                then
			cd $ROOT_DIR
			sudo rm -rf vdfs_root
		else
			sudo rm -rf squashfs-root  
		fi

		cd $ROOT_DIR

		if [ $MODEL_TARGET == "HawkP_TV" ] || [ $MODEL_TARGET == "HawkM" ]
		then

		MakeRootFSImageEW

		fi

		MakeRootFSDataToCheckRelease

		release_img_for_P4

		check_signature

		exit 0
	elif [ "$action" == "7" ]
	then
		release_onboot
	elif [ "$action" == "9" ]
	then
		copy_config_file_perf_product
	else
		echo "Wrong input"
		ACTION_INPUT_ERR=$TRUE

	fi  
}


prepare_rootfs()
{
	echo "prepare rootfs for $PRODUCT_TARGET, $MODEL_TARGET modules copy"

	test -d ../KO
	if [ $? == 0 ]
	then
		sudo rm -rf ../KO
	fi

	mkdir ../KO

	test -d ../$MODEL_TARGET/source/rootfs-src/rootfs-vdlinux
	if [ $? == 0 ]
	then
		sudo rm -r ../$MODEL_TARGET/source/rootfs-src/rootfs-vdlinux
	fi
	
	test -d rootfs-vdlinux
	if [ $? == 0 ]
	then
		sudo rm -r rootfs-vdlinux
	fi

	sudo tar xvzf ../$MODEL_TARGET/source/rootfs-src/rootfs-"$MODEL_TARGET"_"$RELEASE_MODE".tgz
	
	if [ $? != 0 ]
	then
		echo "============"
		echo "TAR ERROR..."
		echo "============"
		exit
	fi
  
	# DAC
	if [ $MODEL_TARGET == "PreHawk" ] || [ $MODEL_TARGET == "PreHawk_AV" ] || [ $MODEL_TARGET == "HawkP" ] || [ $MODEL_TARGET == "HawkP_TV" ] || [ $MODEL_TARGET == "HawkM" ] || [ $MODEL_TARGET == "HAWKA" ]
	then
		echo "### SETCAP ######################################"
		sudo setcap CAP_SETGID,CAP_SETUID,CAP_SYS_ADMIN,CAP_NET_RAW,CAP_NET_BIND_SERVICE,CAP_NET_ADMIN,CAP_SYS_MODULE,CAP_SYS_PTRACE,CAP_IPC_OWNER,CAP_SYS_NICE,CAP_FOWNER,CAP_SYS_RAWIO=+eip ./rootfs-vdlinux/bin/busybox
		if [ $? != 0 ]
		then
			echo "setcap faild.............."
			exit
		else
			echo "### GETCAP ######################################"
			getcap ./rootfs-vdlinux/bin/busybox
			echo ""
		fi
	else
		sudo chown -Rf root:root ./rootfs-vdlinux
	fi

	sudo mv rootfs-vdlinux ../$MODEL_TARGET/source/rootfs-src
	if [ "$action" != "6" ]
	then
		sudo rm -f ../$MODEL_TARGET/source/rootfs-src/rootfs-vdlinux/lib/modules/*
	fi
}

kernel_version_change()
{
	echo ""
	echo "Do you want to change KERNEL Version? [y/n]"
	read answer

	if [ $answer == "y" ]
	then
		echo "Kernel Version Imformation"
		cat ./include/linux/vdlp_version.h
		echo ""

		chmod +rw ./include/linux/vdlp_version.h

		echo -n "kernel version? > "
		read VERSION
		echo -n "last commit number? > "
		read PATCH_NUM
		echo -n "Test Release name? > "
		read TEST_BSP

		if [ "$TEST_BSP" == "" ]
		then
			KERNEL_VERSION_MSG="$PATCH_NUM, $PRODUCT_TARGET, $MODEL_TARGET, $RELEASE_MODE, MAIN"
		else
			KERNEL_VERSION_MSG="$PATCH_NUM, $PRODUCT_TARGET, $MODEL_TARGET, $RELEASE_MODE, MAIN, $TEST_BSP"
		fi

		echo "#define DTV_KERNEL_VERSION "\"$VERSION, $RELEASE_MODE\" > ./include/linux/vdlp_version.h
		echo "#define DTV_LAST_PATCH "\"$KERNEL_VERSION_MSG\" >> ./include/linux/vdlp_version.h
		sync

		echo ""
		echo "Change Kernel Version Information"
		cat ./include/linux/vdlp_version.h
		echo ""

		cp -f ./include/linux/vdlp_version.h ./include/linux/vdlp_version_$MODEL_TARGET.h

		echo "*******************************"
		echo "*   KERNEL VERSION FINISHED   *"
		echo "*******************************"	
	fi
}

rootfs_version_change()
{
	echo ""
	echo "Do you want to change ROOTFS Version? [y/n]"
	read answer

	if [ $answer == "y" ]
	then
		echo "Rootfs Version Information"
		cat ../$MODEL_TARGET/source/rootfs-src/rootfs-vdlinux/.version
		echo ""

		sudo chmod 777 ../$MODEL_TARGET/source/rootfs-src/rootfs-vdlinux/.version

		echo -n "rootfs version? > "
		read RFS_VERSION

#		if [ "$action" != "6" ]
#		then
#			echo "last patch number? > $VERSION.$PATCH_NUM "
#		fi
		echo -n "last commit number? > "
		read RFS_PATCH_NUM
      
		echo -n "Test Release name? > "
		read TEST_BSP

		if [ "$TEST_BSP" == "" ]
		then
			ROOTFS_VERSION_MSG="$RFS_PATCH_NUM, $PRODUCT_TARGET, $MODEL_TARGET, $RELEASE_MODE, MAIN"
		else
			ROOTFS_VERSION_MSG="$RFS_PATCH_NUM, $PRODUCT_TARGET, $MODEL_TARGET, $RELEASE_MODE, MAIN, $TEST_BSP"
		fi
		sudo echo "\"$MODEL_TARGET "$RFS_VERSION\"		KERNEL MODULE VERSION : "\"$ROOTFS_VERSION_MSG\""\ > ../$MODEL_TARGET/source/rootfs-src/rootfs-vdlinux/.version

		echo ""
		echo "Change Rootfs Version Information"
		cat ../$MODEL_TARGET/source/rootfs-src/rootfs-vdlinux/.version
		echo ""

		echo "*******************************"
		echo "*   ROOTFS VERSION FINISHED   *"
		echo "*******************************" 
	fi
}

kernel_compile()
{
	if [ $CORE_TYPE == "ARM" ]
	then
		echo "Setting for $PRODUCT_TARGET, $MODEL_TARGET kernel configuration"

		make clean
		set_compile_env

		make uImage -j8 -k
		if [ $? != "0" ]
		then
			exit
		fi

		if [ $MODEL_TARGET == "PreHawk" ] || [ $MODEL_TARGET == "PreHawk_AV" ]
		then 
			chmod +rw arch/arm/boot/dts/sdp1304-dtv-us-1mp.dtb
			chmod +rw arch/arm/boot/dts/sdp1304-dtv-fhd.prehawk.orsay.dtb

			make sdp1304-dtv-us-1mp.dtb
			if [ $? != "0" ]
			then
				exit
			fi
			make sdp1304-dtv-fhd.prehawk.orsay.dtb
			if [ $? != "0" ]
			then
				exit
			fi
		elif [ $MODEL_TARGET == "HawkP" ]
		then
			make sdp1404-av.dtb
			if [ $? -ne 0 ]
			then
				echo "build dtb failed. Stopped..."
				exit
			fi
		elif [ $MODEL_TARGET == "HawkP_TV" ]
		then
			make sdp1404-dtv.dtb
			if [ $? -ne 0 ]
			then
				echo "build dtb failed. Stopped..."
				exit
			fi
		elif [ $MODEL_TARGET == "HawkM" ]
		then
			make sdp1406.uhd.1.5G.dtb
			if [ $? -ne 0 ]
			then
				echo "build dtb failed. Stopped..."
				exit
			fi
                        make sdp1406.uhd.2.5G.dtb
                        if [ $? -ne 0 ]
                        then
                                echo "build dtb failed. Stopped..."
                                exit
                        fi
                        make sdp1406.uhd.2.0G.dtb
                        if [ $? -ne 0 ]
                        then
                                echo "build dtb failed. Stopped..."
                                exit
                        fi
			make sdp1406.fhd.dtb
			if [ $? -ne 0 ]
			then
				echo "build dtb failed. Stopped..."
				exit
			fi
                        make sdp1406.fhd.1.5G.dtb
                        if [ $? -ne 0 ]
                        then
                                echo "build dtb failed. Stopped..."
                                exit
                        fi
                elif [ $MODEL_TARGET == "HAWKA" ]
                then
			make sdp1412.dtb
                        if [ $? -ne 0 ]
                        then
                                echo "build dtb failed. Stopped..."
                                exit
                        fi
		fi

		make modules -j8 -k
		if [ $? != "0" ]
		then
			exit
		fi

		if [ $MODEL_TARGET == "PreHawk" ] || [ $MODEL_TARGET == "PreHawk_AV" ]
                then
			cd $ROOT_DIR
			chmod 777 scripts/rsa_tool/rsa_golf.sh
			scripts/rsa_tool/rsa_golf.sh arch/arm/boot/uImage uImage arch/arm/boot/dts/sdp1304-dtv-fhd.prehawk.orsay.dtb
			scripts/rsa_tool/rsa_golf.sh arch/arm/boot/uImage uImage_US_1MP arch/arm/boot/dts/sdp1304-dtv-us-1mp.dtb
		elif [ $MODEL_TARGET == "HawkP" ] || [ $MODEL_TARGET == "HawkP_TV" ] || [ $MODEL_TARGET == "HawkM" ] || [ $MODEL_TARGET == "HAWKA" ]
		then
			cd $ROOT_DIR
			cp arch/arm/boot/uImage uImage
		else
			cd $ROOT_DIR
			cp arch/arm/boot/uImage uImage
		fi
	fi
}

make_rootfs_util()
{
	echo ""
	echo "=============================================="
	echo " Rootfs Util auto compile"
	echo "=============================================="
	echo ""

	cd $ROOT_DIR

	sudo rm -rf ../UTIL
	mkdir ../UTIL
	mkdir ../UTIL/NonStrip

	cd $ROOT_DIR
	echo ""
	echo "=============================================="
	echo " MicomCtrl compile"
	echo "=============================================="
	echo ""
	
	echo "Warning : Check CROSS_COMPILE prefix in MicomCtrl_util/Makefile !!!!!!!"
	echo "Do you want to re-build MicomCtrl? [y/n]"
	read answer
	if [ $answer == "y" ]
	then
		cd $BSP_P4_DIR/Tools/MicomCtrl_util
		make clean

		echo ""
		echo "=============================================="
		echo " usb_gpio compile"
		echo "=============================================="
		echo ""

		if [ $MODEL_TARGET == "PreHawkP" ] || [ $MODEL_TARGET == "PreHawk_AV" ]
		then
			CROSS_COMPILE=$TOOLCHAIN_PREFIX- make PreHawkP
			cp -ar ./bin/PreHawkP/micom $ROOT_DIR/../UTIL
		fi

		if [ $MODEL_TARGET == "HawkP" ]
		then
			CROSS_COMPILE=$TOOLCHAIN_PREFIX- make HawkP
			cp -ar ./bin/HawkP/micom $ROOT_DIR/../UTIL
		fi

		if [ $MODEL_TARGET == "HawkP_TV" ]
		then
			CROSS_COMPILE=$TOOLCHAIN_PREFIX- make HawkP_TV
			cp -ar ./bin/HawkP_TV/micom $ROOT_DIR/../UTIL
		fi

		if [ $MODEL_TARGET == "HawkM" ]
		then
			CROSS_COMPILE=$TOOLCHAIN_PREFIX- make HawkM
			cp -ar ./bin/HawkM/micom $ROOT_DIR/../UTIL
		fi

                if [ $MODEL_TARGET == "HAWKA" ]
                then
                        CROSS_COMPILE=$TOOLCHAIN_PREFIX- make HAWKA
                        cp -ar ./bin/HAWKA/micom $ROOT_DIR/../UTIL
                fi

		sync
		make clean
	fi
	
	if [ $MODEL_TARGET = "PreHawk" ] || [ $MODEL_TARGET = "PreHawk_AV" ] || [ $MODEL_TARGET == "HawkP" ] || [ $MODEL_TARGET == "HawkP_TV" ] || [ $MODEL_TARGET == "HawkM" ] || [ $MODEL_TARGET == "HAWKA" ]
	then
		cd $ROOT_DIR
		echo ""
		echo "=============================================="
		echo " mkfs.vdfs compile"
		echo "=============================================="
		echo ""

		echo "Do you want to re-build mkfs.vdfs? [y/n]"
		read answer
		if [ $answer == "y" ]
		then
			cd $BSP_P4_DIR/Tools/vdfs-tools/
			make clean

			make CROSS_COMPILE=$TOOLCHAIN_PREFIX-
			make CROSS_COMPILE=$TOOLCHAIN_PREFIX- page-types

			cp -ar mkfs.vdfs $ROOT_DIR/../UTIL/
			cp -ar install.vdfs $ROOT_DIR/../UTIL/
			
			cp -ar page-types.vdfs $ROOT_DIR/../UTIL/

			sync

			make clean
		fi
	fi

	cd $ROOT_DIR
	echo ""
	echo "=============================================="
	echo " libflash.so compile"
	echo "=============================================="
	echo ""

	echo "Do you want to re-build libflash.so? [y/n]"
	read answer
	if [ $answer == "y" ]
	then
		cd $BSP_P4_DIR/Tools/libflash
		make clean

		make CROSS_COMPILE=$TOOLCHAIN_PREFIX-

		cp -ar libflash.so $ROOT_DIR/../UTIL/NonStrip/
		sync

		make clean
	fi

	cd $ROOT_DIR
	echo ""
	echo "=============================================="
	echo " Authuld compile"
	echo "=============================================="
	echo ""

	echo "Do you want to copy Authuld? [y/n]"
	read answer
	if [ $answer == "y" ]
	then
		cd $BSP_P4_DIR/Tools/CIP_UTIL

		if [ $MODEL_TARGET == "HawkP" ]
		then
			cp -ar bin_HawkP/authuld $ROOT_DIR/../UTIL
		elif [ $MODEL_TARGET == "HawkP_TV" ]
		then
			cp -ar bin_HawkP_TV/authuld $ROOT_DIR/../UTIL
		elif [ $MODEL_TARGET == "HawkM" ]
		then
			cp -ar bin_HawkM/authuld $ROOT_DIR/../UTIL
                elif [ $MODEL_TARGET == "HAWKA" ]
                then
                        cp -ar bin_HAWKA/authuld $ROOT_DIR/../UTIL
		fi
                sync
	fi

        if [ $RELEASE_MODE == "debug" ] || [ $RELEASE_MODE == "kasan" ]
        then
		cd $ROOT_DIR
		echo ""
		echo "=============================================="
		echo " mmc.restore compile"
		echo "=============================================="
		echo ""

		echo "Do you want to rebuild mmc.restore? [y/n]"
		read answer

	       	if [ $answer == "y" ]
    	        then
  		       cd $BSP_P4_DIR/Tools/MMC_Tools
   		       make clean

		       make common CROSS_COMPILE=$TOOLCHAIN_PREFIX-
		       cp -ar mmc.restore $ROOT_DIR/../UTIL
		       sync

		       make clean
	        fi
        fi

	if [ $MODEL_TARGET == "PreHawk" ] || [ $MODEL_TARGET == "PreHawk_AV" ]
	then
		cd $ROOT_DIR
		echo ""
		echo "=============================================="
		echo " MDI Loopback Test tool compile"
		echo "=============================================="
		echo ""
		
		echo "Do you want to rebuild ethtool_selftest? [y/n]"
		read answer

		if [ $answer == "y" ]
		then
			cd ../../Tools/ethernet_loopback
			make clean
			make CROSS=$TOOLCHAIN_PREFIX-

			cp -ar ethtool_selftest ../../VDLinux_3.10.28/UTIL
			sync

			make clean
		fi
	fi

	cd $ROOT_DIR
	echo ""
	echo "=============================================="
	echo " save error log compile"
	echo "=============================================="
	echo ""

	echo "Do you want to rebuild save_error_log? [y/n]"
	read answer
	if [ $answer == "y" ]
	then
		cd ../vd_src/save_error_log
		make clean

		make CROSS_COMPILE=$TOOLCHAIN_PREFIX-

		cp -ar save_error_log $ROOT_DIR/../UTIL
		sync

		make clean
	fi
	
	cd $ROOT_DIR
	echo ""
	echo "=============================================="
	echo " usb_automount_util copy"
	echo "=============================================="
	echo ""

	echo "Do you want to copy usb_automount_util? [y/n]"
	read answer
	if [ $answer == "y" ]
	then
		mkdir ../UTIL/usb_automount_util

		cd $BSP_P4_DIR/Tools/usb_automount_util/2014/Lib
		
		if [ $MODEL_TARGET == "PreHawk" ]
		then
			cp -aRf PREHAWKP/sbin  $ROOT_DIR/../UTIL/usb_automount_util/
		fi
		if [ $MODEL_TARGET == "PreHawk_AV" ]
		then
			cp -aRf PreHAWK_AV/sbin  $ROOT_DIR/../UTIL/usb_automount_util/
		fi
		if [ $MODEL_TARGET == "HawkP" ]
		then
			cp -aRf HAWKP/sbin  $ROOT_DIR/../UTIL/usb_automount_util/
		fi
		if [ $MODEL_TARGET == "HawkP_TV" ]
		then
			cp -aRf HAWKP_TV/sbin  $ROOT_DIR/../UTIL/usb_automount_util/
		fi
		if [ $MODEL_TARGET == "HawkM" ]
		then
			cp -aRf HAWKM/sbin  $ROOT_DIR/../UTIL/usb_automount_util/
		fi
                if [ $MODEL_TARGET == "HAWKA" ]
                then
                        cp -aRf HAWKA/sbin  $ROOT_DIR/../UTIL/usb_automount_util/
                fi
		sync
	fi

	cd $ROOT_DIR
	  
	echo ""
	echo "Copying utils to rootfs source......."
	echo ""

	ls -alR ../UTIL
	
	echo ""
	echo "Do you want to copy utils to rootfs? [y/n]"
	read answer
	if [ $answer == "y" ]
	then
		sudo $TOOLCHAIN_PREFIX-strip ../UTIL/*
		sudo $TOOLCHAIN_PREFIX-strip ../UTIL/usb_automount_util/sbin/usb_mount/*
		sudo chmod -R 777 ../UTIL/* 
		sudo chmod 755 ../UTIL/NonStrip/* 
		sudo chown -R root:root ../UTIL/* 

		if [ -f ../UTIL/micom ]
		then
			sudo cp -af ../UTIL/micom ../$MODEL_TARGET/source/rootfs-src/rootfs-vdlinux/sbin
		fi
		
		if [ -f ../UTIL/NonStrip/libflash.so ]
		then
			sudo cp -af ../UTIL/NonStrip/libflash.so ../$MODEL_TARGET/source/rootfs-src/rootfs-vdlinux/mtd_exe/lib
		fi
		
		if [ -f ../UTIL/authuld ]
		then
			sudo cp -af ../UTIL/authuld ../$MODEL_TARGET/source/rootfs-src/rootfs-vdlinux/bin
		fi
		
		if [ -e ../UTIL/usb_automount_util/sbin ]
		then
			sudo cp -aRf ../UTIL/usb_automount_util/sbin/*  ../$MODEL_TARGET/source/rootfs-src/rootfs-vdlinux/sbin
		fi
		
                if [ $RELEASE_MODE == "debug" ] || [ $RELEASE_MODE == "kasan" ]
                then
			if [ -f ../UTIL/mmc.restore ]
			then
				sudo cp -af ../UTIL/mmc.restore ../$MODEL_TARGET/source/rootfs-src/rootfs-vdlinux/util
			fi
                fi

		if [ -f ../UTIL/save_error_log ]
		then
			sudo cp -af ../UTIL/save_error_log ../$MODEL_TARGET/source/rootfs-src/rootfs-vdlinux/util
		fi

		if [ -f ../UTIL/mkfs.vdfs ]
		then
			sudo cp -af ../UTIL/mkfs.vdfs ../$MODEL_TARGET/source/rootfs-src/rootfs-vdlinux/util
		fi

		if [ -f ../UTIL/install.vdfs ]
		then
			sudo cp -af ../UTIL/install.vdfs ../$MODEL_TARGET/source/rootfs-src/rootfs-vdlinux/util
		fi

		if [ -f ../UTIL/page-types.vdfs ]
                then
                        sudo cp -af ../UTIL/page-types.vdfs ../$MODEL_TARGET/source/rootfs-src/rootfs-vdlinux/util
                fi

		if [ -f ../UTIL/ethtool_selftest ]
		then
			sudo cp -af ../UTIL/ethtool_selftest ../$MODEL_TARGET/source/rootfs-src/rootfs-vdlinux/util
		fi
	fi
 
	rm -Rf ../UTIL
	sync

	echo ""
	echo "=============================================="
	echo " Rootfs Util auto compile END"
	echo "=============================================="
	echo ""
}

make_rootfs_img()
{
	echo ""
	echo "previous rootfs remove"

	sudo rm -f rootfs.img

	echo "copying modules for $PRODUCT_TARGET, $MODEL_TARGET"

	if [ $CORE_TYPE == "ARM" ]
	then
		echo "Add moudules to copy"

		# Exfat and tntfs
		cp fs/exfat/exfat_core.ko ../KO/.	
		cp fs/exfat/exfat_fs.ko ../KO/.	
		
		cp fs/tntfs/tntfs.ko ../KO/.	

		# HID
		cp drivers/input/ff-memless.ko ../KO/.
		cp drivers/input/mousedev.ko ../KO/.
		cp drivers/input/joydev.ko ../KO/.
		cp drivers/input/evdev.ko ../KO/.
		cp drivers/input/joystick/xpad.ko ../KO/.
		cp drivers/hid/hid.ko ../KO/.
		cp drivers/hid/hid-generic.ko ../KO/.
		cp drivers/hid/hid-apple.ko ../KO/.
		cp drivers/hid/hid-chicony.ko ../KO/.
		cp drivers/hid/hid-logitech.ko ../KO/.
		cp drivers/hid/hid-microsoft.ko ../KO/.
		cp drivers/hid/usbhid/usbhid.ko ../KO/.

		# nfs client
		if [ $RELEASE_MODE == "debug" ]
		then
			cp fs/lockd/lockd.ko ../KO/.
			cp fs/nfs/nfs.ko ../KO/.
			cp fs/nfs/nfsv3.ko ../KO/.
			cp net/sunrpc/sunrpc.ko ../KO/.
		fi

		cp net/ipv4/netfilter/iptable_filter.ko ../KO/.
		cp net/ipv4/netfilter/ipt_REJECT.ko ../KO/.

		# 20140801 USB
		cp drivers/usb/gadget/udc-core.ko ../KO/.
		cp drivers/usb/phy/phy-hawk.ko ../KO/.

		if [ $MODEL_TARGET == "PreHawk" ]
		then
			cp sound/core/snd-hwdep.ko ../KO/.
			cp sound/core/snd-rawmidi.ko ../KO/.
			cp sound/core/snd-pcm.ko ../KO/.
			cp sound/core/snd.ko ../KO/.
			cp sound/core/snd-page-alloc.ko ../KO/.
			cp sound/core/snd-timer.ko ../KO/.
			cp sound/core/seq/snd-seq-device.ko ../KO/.
			cp sound/core/seq/snd-seq.ko ../KO/.
			cp sound/soundcore.ko ../KO/.
			cp sound/usb/snd-usb-audio.ko ../KO/.
			cp sound/usb/snd-usbmidi-lib.ko ../KO/.		
			cp sound/core/seq/snd-seq-midi-event.ko ../KO/.
			cp sound/core/seq/snd-seq-midi.ko ../KO/.
			cp net/wireless/cfg80211.ko ../KO/.
			cp drivers/usb/core/usbcore.ko ../KO/.
			cp drivers/usb/usb-common.ko ../KO/.
			cp drivers/usb/storage/usb-storage.ko ../KO/.
			cp drivers/usb/host/ehci-hcd.ko ../KO/.
			cp drivers/usb/host/ohci-hcd.ko ../KO/.
			cp arch/arm/mach-sdp/sdp_mac.ko ../KO/.
			cp drivers/usb/serial/usbserial.ko ../KO/.
			cp drivers/usb/serial/ftdi_sio.ko ../KO/.	
			cp drivers/usb/host/xhci-hcd.ko ../KO/.
			cp drivers/usb/class/cdc-acm.ko ../KO/.
			cp sound/core/seq/snd-seq-midi.ko ../KO/.
			cp sound/core/seq/snd-seq-midi-event.ko ../KO/.
			cp drivers/usb/gadget/g_sam_multi.ko ../KO/.
			cp drivers/usb/gadget/udc-core.ko ../KO/.
			cp drivers/net/usb/cdc_eem.ko ../KO/.
			cp drivers/net/usb/usbnet.ko ../KO/.
			cp net/ipv4/netfilter/iptable_filter.ko ../KO/.
			cp net/ipv4/netfilter/ipt_REJECT.ko ../KO/.
			cp drivers/hid/hid-multitouch.ko ../KO/.
			cp drivers/usb/dwc3/dwc3.ko ../KO/.
			cp drivers/usb/dwc3/dwc3-sdp.ko ../KO/.
			cp drivers/usb/otg/nop-usb-xceiv.ko ../KO/.
                        # 20131022
                        cp  drivers/usb/serial/option.ko ../KO/.
                        cp  drivers/usb/serial/usb_wwan.ko ../KO/.
                        cp  drivers/usb/serial/usbserial.ko ../KO/.
                        cp  drivers/net/ppp/bsd_comp.ko ../KO/.
                        cp  drivers/net/ppp/ppp_async.ko ../KO/.
                        cp  drivers/net/ppp/ppp_deflate.ko ../KO/.
                        cp  drivers/net/ppp/ppp_generic.ko ../KO/.
                        cp  drivers/net/ppp/ppp_synctty.ko ../KO/.
                        cp  drivers/net/slip/slhc.ko ../KO/.
                        cp  lib/crc-ccitt.ko ../KO/.
			# 20131210
                        cp  drivers/spi/spi-gpio.ko ../KO/.
			# 20140225 		
                        cp drivers/usb/serial/pl2303.ko ../KO/.
			# 20140625
			cp drivers/scsi/sg.ko ../KO/.
        	fi

		if [ $MODEL_TARGET == "PreHawk_AV" ]
		then
			cp sound/core/snd-hwdep.ko ../KO/.
			cp sound/core/snd-rawmidi.ko ../KO/.
			cp sound/core/snd-pcm.ko ../KO/.
			cp sound/core/snd.ko ../KO/.
			cp sound/core/snd-page-alloc.ko ../KO/.
			cp sound/core/snd-timer.ko ../KO/.
			cp sound/core/seq/snd-seq-device.ko ../KO/.
			cp sound/core/seq/snd-seq.ko ../KO/.
			cp sound/soundcore.ko ../KO/.
			cp sound/usb/snd-usb-audio.ko ../KO/.
			cp sound/usb/snd-usbmidi-lib.ko ../KO/.		
			cp sound/core/seq/snd-seq-midi-event.ko ../KO/.
			cp sound/core/seq/snd-seq-midi.ko ../KO/.
			cp net/wireless/cfg80211.ko ../KO/.
			cp drivers/usb/core/usbcore.ko ../KO/.
			cp drivers/usb/usb-common.ko ../KO/.
			cp drivers/usb/storage/usb-storage.ko ../KO/.
			cp drivers/usb/host/ehci-hcd.ko ../KO/.
			cp drivers/usb/host/ohci-hcd.ko ../KO/.
			cp arch/arm/mach-sdp/sdp_mac.ko ../KO/.
			cp drivers/usb/serial/usbserial.ko ../KO/.
			cp drivers/usb/serial/ftdi_sio.ko ../KO/.	
			cp drivers/usb/host/xhci-hcd.ko ../KO/.
			cp drivers/usb/class/cdc-acm.ko ../KO/.
			cp sound/core/seq/snd-seq-midi.ko ../KO/.
			cp sound/core/seq/snd-seq-midi-event.ko ../KO/.
			cp drivers/usb/gadget/g_sam_multi.ko ../KO/.
			cp drivers/usb/gadget/udc-core.ko ../KO/.
			cp drivers/net/usb/cdc_eem.ko ../KO/.
			cp drivers/net/usb/usbnet.ko ../KO/.
			cp net/ipv4/netfilter/iptable_filter.ko ../KO/.
			cp net/ipv4/netfilter/ipt_REJECT.ko ../KO/.
			cp drivers/hid/hid-multitouch.ko ../KO/.
			cp drivers/usb/dwc3/dwc3.ko ../KO/.
			cp drivers/usb/dwc3/dwc3-sdp.ko ../KO/.
			cp drivers/usb/otg/nop-usb-xceiv.ko ../KO/.
                        # 20131022
                        cp  drivers/usb/serial/option.ko ../KO/.
                        cp  drivers/usb/serial/usb_wwan.ko ../KO/.
                        cp  drivers/usb/serial/usbserial.ko ../KO/.
                        cp  drivers/net/ppp/bsd_comp.ko ../KO/.
                        cp  drivers/net/ppp/ppp_async.ko ../KO/.
                        cp  drivers/net/ppp/ppp_deflate.ko ../KO/.
                        cp  drivers/net/ppp/ppp_generic.ko ../KO/.
                        cp  drivers/net/ppp/ppp_synctty.ko ../KO/.
                        cp  drivers/net/slip/slhc.ko ../KO/.
                        cp  lib/crc-ccitt.ko ../KO/.
			# 20131210
                        cp  drivers/spi/spi-gpio.ko ../KO/.
			# 20140225 		
                        cp drivers/usb/serial/pl2303.ko ../KO/.

			# 20140625
			cp drivers/ata/libata.ko ../KO/.
			cp drivers/cdrom/cdrom.ko ../KO/.
			cp drivers/scsi/sd_mod.ko ../KO/.
			cp drivers/scsi/sr_mod.ko ../KO/.

			cp drivers/scsi/sg.ko ../KO/.
        	fi

		if [ $MODEL_TARGET == "HawkP" ]
		then
			cp sound/core/snd-hwdep.ko ../KO/.
			cp sound/core/snd-rawmidi.ko ../KO/.
			cp sound/core/snd-pcm.ko ../KO/.
			cp sound/core/snd.ko ../KO/.
			cp sound/core/snd-page-alloc.ko ../KO/.
			cp sound/core/snd-timer.ko ../KO/.
			cp sound/core/seq/snd-seq-device.ko ../KO/.
			cp sound/core/seq/snd-seq.ko ../KO/.
			cp sound/soundcore.ko ../KO/.
			cp sound/usb/snd-usb-audio.ko ../KO/.
			cp sound/usb/snd-usbmidi-lib.ko ../KO/.		
			cp sound/core/seq/snd-seq-midi-event.ko ../KO/.
			cp sound/core/seq/snd-seq-midi.ko ../KO/.
			cp net/wireless/cfg80211.ko ../KO/.
			cp drivers/usb/core/usbcore.ko ../KO/.
			cp drivers/usb/usb-common.ko ../KO/.
			cp drivers/usb/storage/usb-storage.ko ../KO/.
			cp drivers/usb/host/ehci-hcd.ko ../KO/.
			cp drivers/usb/host/ohci-hcd.ko ../KO/.
			cp arch/arm/mach-sdp/sdp_mac.ko ../KO/.
			cp drivers/usb/serial/usbserial.ko ../KO/.
			cp drivers/usb/serial/ftdi_sio.ko ../KO/.	
			cp drivers/usb/host/xhci-hcd.ko ../KO/.
			cp drivers/usb/class/cdc-acm.ko ../KO/.
			cp sound/core/seq/snd-seq-midi.ko ../KO/.
			cp sound/core/seq/snd-seq-midi-event.ko ../KO/.
			cp drivers/usb/gadget/g_sam_multi.ko ../KO/.
			cp drivers/usb/gadget/udc-core.ko ../KO/.
			cp drivers/net/usb/cdc_eem.ko ../KO/.
			cp drivers/net/usb/usbnet.ko ../KO/.
			cp net/ipv4/netfilter/iptable_filter.ko ../KO/.
			cp net/ipv4/netfilter/ipt_REJECT.ko ../KO/.
			cp drivers/hid/hid-multitouch.ko ../KO/.
			cp drivers/usb/dwc3/dwc3.ko ../KO/.
			cp drivers/usb/dwc3/dwc3-sdp.ko ../KO/.
			cp drivers/usb/otg/nop-usb-xceiv.ko ../KO/.
                        # 20131022
                        cp  drivers/usb/serial/option.ko ../KO/.
                        cp  drivers/usb/serial/usb_wwan.ko ../KO/.
                        cp  drivers/usb/serial/usbserial.ko ../KO/.
                        cp  drivers/net/ppp/bsd_comp.ko ../KO/.
                        cp  drivers/net/ppp/ppp_async.ko ../KO/.
                        cp  drivers/net/ppp/ppp_deflate.ko ../KO/.
                        cp  drivers/net/ppp/ppp_generic.ko ../KO/.
                        cp  drivers/net/ppp/ppp_synctty.ko ../KO/.
                        cp  drivers/net/slip/slhc.ko ../KO/.
                        cp  lib/crc-ccitt.ko ../KO/.
			# 20131210
                        cp  drivers/spi/spi-gpio.ko ../KO/.
			# 20140225 		
                        cp drivers/usb/serial/pl2303.ko ../KO/.
			# 20140625
			cp drivers/scsi/sg.ko ../KO/.
			# 20140718
			cp drivers/ata/ahci_sdp.ko ../KO/.
			cp drivers/ata/libahci.ko ../KO/.
			cp drivers/ata/ahci_sdp_phy_hawkp.ko ../KO/.
			# 20150127
			cp drivers/cdrom/cdrom.ko ../KO/.
			cp drivers/scsi/sr_mod.ko ../KO/.
			# 20150128
			cp drivers/ata/libata.ko ../KO/.
			cp drivers/ata/ahci_sdp_phy.ko ../KO/.
        	fi

		if [ $MODEL_TARGET == "HawkP_TV" ]
		then
			cp sound/core/snd-hwdep.ko ../KO/.
			cp sound/core/snd-rawmidi.ko ../KO/.
			cp sound/core/snd-pcm.ko ../KO/.
			cp sound/core/snd.ko ../KO/.
			cp sound/core/snd-page-alloc.ko ../KO/.
			cp sound/core/snd-timer.ko ../KO/.
			cp sound/core/seq/snd-seq-device.ko ../KO/.
			cp sound/core/seq/snd-seq.ko ../KO/.
			cp sound/soundcore.ko ../KO/.
			cp sound/usb/snd-usb-audio.ko ../KO/.
			cp sound/usb/snd-usbmidi-lib.ko ../KO/.		
			cp sound/core/seq/snd-seq-midi-event.ko ../KO/.
			cp sound/core/seq/snd-seq-midi.ko ../KO/.
			cp net/wireless/cfg80211.ko ../KO/.
			cp drivers/usb/core/usbcore.ko ../KO/.
			cp drivers/usb/usb-common.ko ../KO/.
			cp drivers/usb/storage/usb-storage.ko ../KO/.
			cp drivers/usb/host/ehci-hcd.ko ../KO/.
			cp drivers/usb/host/ohci-hcd.ko ../KO/.
			cp arch/arm/mach-sdp/sdp_mac.ko ../KO/.
			cp drivers/usb/serial/usbserial.ko ../KO/.
			cp drivers/usb/serial/ftdi_sio.ko ../KO/.	
			cp drivers/usb/host/xhci-hcd.ko ../KO/.
			cp drivers/usb/class/cdc-acm.ko ../KO/.
			cp sound/core/seq/snd-seq-midi.ko ../KO/.
			cp sound/core/seq/snd-seq-midi-event.ko ../KO/.
			cp drivers/usb/gadget/g_sam_multi.ko ../KO/.
			cp drivers/usb/gadget/udc-core.ko ../KO/.
			cp drivers/net/usb/cdc_eem.ko ../KO/.
			cp drivers/net/usb/usbnet.ko ../KO/.
			cp net/ipv4/netfilter/iptable_filter.ko ../KO/.
			cp net/ipv4/netfilter/ipt_REJECT.ko ../KO/.
			cp drivers/hid/hid-multitouch.ko ../KO/.
			cp drivers/usb/dwc3/dwc3.ko ../KO/.
			cp drivers/usb/dwc3/dwc3-sdp.ko ../KO/.
			cp drivers/usb/otg/nop-usb-xceiv.ko ../KO/.
                        # 20131022
                        cp  drivers/usb/serial/option.ko ../KO/.
                        cp  drivers/usb/serial/usb_wwan.ko ../KO/.
                        cp  drivers/usb/serial/usbserial.ko ../KO/.
                        cp  drivers/net/ppp/bsd_comp.ko ../KO/.
                        cp  drivers/net/ppp/ppp_async.ko ../KO/.
                        cp  drivers/net/ppp/ppp_deflate.ko ../KO/.
                        cp  drivers/net/ppp/ppp_generic.ko ../KO/.
                        cp  drivers/net/ppp/ppp_synctty.ko ../KO/.
                        cp  drivers/net/slip/slhc.ko ../KO/.
                        cp  lib/crc-ccitt.ko ../KO/.
			# 20131210
                        cp  drivers/spi/spi-gpio.ko ../KO/.
			# 20140225 		
                        cp drivers/usb/serial/pl2303.ko ../KO/.
			# 20140625
			cp drivers/scsi/sg.ko ../KO/.
			# 20140924
			cp drivers/pci/pcie-sdp.ko ../KO/.
        	fi

		if [ $MODEL_TARGET == "HawkM" ]
		then
			cp sound/core/snd-hwdep.ko ../KO/.
			cp sound/core/snd-rawmidi.ko ../KO/.
			cp sound/core/snd-pcm.ko ../KO/.
			cp sound/core/snd.ko ../KO/.
			cp sound/core/snd-page-alloc.ko ../KO/.
			cp sound/core/snd-timer.ko ../KO/.
			cp sound/core/seq/snd-seq-device.ko ../KO/.
			cp sound/core/seq/snd-seq.ko ../KO/.
			cp sound/soundcore.ko ../KO/.
			cp sound/usb/snd-usb-audio.ko ../KO/.
			cp sound/usb/snd-usbmidi-lib.ko ../KO/.		
			cp sound/core/seq/snd-seq-midi-event.ko ../KO/.
			cp sound/core/seq/snd-seq-midi.ko ../KO/.
			cp net/wireless/cfg80211.ko ../KO/.
			cp drivers/usb/core/usbcore.ko ../KO/.
			cp drivers/usb/usb-common.ko ../KO/.
			cp drivers/usb/storage/usb-storage.ko ../KO/.
			cp drivers/usb/host/ehci-hcd.ko ../KO/.
			cp drivers/usb/host/ohci-hcd.ko ../KO/.
			cp arch/arm/mach-sdp/sdp_mac.ko ../KO/.
			cp drivers/usb/serial/usbserial.ko ../KO/.
			cp drivers/usb/serial/ftdi_sio.ko ../KO/.	
			cp drivers/usb/host/xhci-hcd.ko ../KO/.
			cp drivers/usb/class/cdc-acm.ko ../KO/.
			cp sound/core/seq/snd-seq-midi.ko ../KO/.
			cp sound/core/seq/snd-seq-midi-event.ko ../KO/.
			cp drivers/usb/gadget/g_sam_multi.ko ../KO/.
			cp drivers/usb/gadget/udc-core.ko ../KO/.
			cp drivers/net/usb/cdc_eem.ko ../KO/.
			cp drivers/net/usb/usbnet.ko ../KO/.
			cp net/ipv4/netfilter/iptable_filter.ko ../KO/.
			cp net/ipv4/netfilter/ipt_REJECT.ko ../KO/.
			cp drivers/hid/hid-multitouch.ko ../KO/.
			cp drivers/usb/dwc3/dwc3.ko ../KO/.
			cp drivers/usb/dwc3/dwc3-sdp.ko ../KO/.
			cp drivers/usb/otg/nop-usb-xceiv.ko ../KO/.
                        # 20131022
                        cp  drivers/usb/serial/option.ko ../KO/.
                        cp  drivers/usb/serial/usb_wwan.ko ../KO/.
                        cp  drivers/usb/serial/usbserial.ko ../KO/.
                        cp  drivers/net/ppp/bsd_comp.ko ../KO/.
                        cp  drivers/net/ppp/ppp_async.ko ../KO/.
                        cp  drivers/net/ppp/ppp_deflate.ko ../KO/.
                        cp  drivers/net/ppp/ppp_generic.ko ../KO/.
                        cp  drivers/net/ppp/ppp_synctty.ko ../KO/.
                        cp  drivers/net/slip/slhc.ko ../KO/.
                        cp  lib/crc-ccitt.ko ../KO/.
			# 20131210
                        cp  drivers/spi/spi-gpio.ko ../KO/.
			# 20140225 		
                        cp drivers/usb/serial/pl2303.ko ../KO/.
			# 20140625
			cp drivers/scsi/sg.ko ../KO/.
			# 20140718
			cp drivers/ata/ahci_sdp.ko ../KO/.
			cp drivers/ata/libahci.ko ../KO/.
			cp drivers/ata/ahci_sdp_phy_hawkp.ko ../KO/.
        	fi

                if [ $MODEL_TARGET == "HAWKA" ]
                then
                        cp sound/core/snd-hwdep.ko ../KO/.
                        cp sound/core/snd-rawmidi.ko ../KO/.
                        cp sound/core/snd-pcm.ko ../KO/.
                        cp sound/core/snd.ko ../KO/.
                        cp sound/core/snd-page-alloc.ko ../KO/.
                        cp sound/core/snd-timer.ko ../KO/.
                        cp sound/core/seq/snd-seq-device.ko ../KO/.
                        cp sound/core/seq/snd-seq.ko ../KO/.
                        cp sound/soundcore.ko ../KO/.
                        cp sound/usb/snd-usb-audio.ko ../KO/.
                        cp sound/usb/snd-usbmidi-lib.ko ../KO/.
                        cp sound/core/seq/snd-seq-midi-event.ko ../KO/.
                        cp sound/core/seq/snd-seq-midi.ko ../KO/.
                        cp net/wireless/cfg80211.ko ../KO/.
                        cp drivers/usb/core/usbcore.ko ../KO/.
                        cp drivers/usb/usb-common.ko ../KO/.
                        cp drivers/usb/storage/usb-storage.ko ../KO/.
                        cp drivers/usb/host/ehci-hcd.ko ../KO/.
                        cp drivers/usb/host/ohci-hcd.ko ../KO/.
                        cp arch/arm/mach-sdp/sdp_mac.ko ../KO/.
                        cp drivers/usb/serial/usbserial.ko ../KO/.
                        cp drivers/usb/serial/ftdi_sio.ko ../KO/.
                        cp drivers/usb/host/xhci-hcd.ko ../KO/.
                        cp drivers/usb/class/cdc-acm.ko ../KO/.
                        cp sound/core/seq/snd-seq-midi.ko ../KO/.
                        cp sound/core/seq/snd-seq-midi-event.ko ../KO/.
                        cp drivers/usb/gadget/g_sam_multi.ko ../KO/.
                        cp drivers/usb/gadget/udc-core.ko ../KO/.
                        cp drivers/net/usb/cdc_eem.ko ../KO/.
                        cp drivers/net/usb/usbnet.ko ../KO/.
                        cp net/ipv4/netfilter/iptable_filter.ko ../KO/.
                        cp net/ipv4/netfilter/ipt_REJECT.ko ../KO/.
                        cp drivers/hid/hid-multitouch.ko ../KO/.
                        cp drivers/usb/dwc3/dwc3.ko ../KO/.
                        cp drivers/usb/dwc3/dwc3-sdp.ko ../KO/.
                        cp drivers/usb/otg/nop-usb-xceiv.ko ../KO/.
                        # 20131022
                        cp  drivers/usb/serial/option.ko ../KO/.
                        cp  drivers/usb/serial/usb_wwan.ko ../KO/.
                        cp  drivers/usb/serial/usbserial.ko ../KO/.
                        cp  drivers/net/ppp/bsd_comp.ko ../KO/.
                        cp  drivers/net/ppp/ppp_async.ko ../KO/.
                        cp  drivers/net/ppp/ppp_deflate.ko ../KO/.
                        cp  drivers/net/ppp/ppp_generic.ko ../KO/.
                        cp  drivers/net/ppp/ppp_synctty.ko ../KO/.
                        cp  drivers/net/slip/slhc.ko ../KO/.
                        cp  lib/crc-ccitt.ko ../KO/.
                        # 20131210
                        cp  drivers/spi/spi-gpio.ko ../KO/.
                        # 20140225
                        cp drivers/usb/serial/pl2303.ko ../KO/.
                        # 20140625
                        cp drivers/scsi/sg.ko ../KO/.
                        # 20140718
                        cp drivers/ata/ahci_sdp.ko ../KO/.
                        cp drivers/ata/libahci.ko ../KO/.
                        cp drivers/ata/ahci_sdp_phy_hawkp.ko ../KO/.
			# 20150112
			cp drivers/net/usb/ax88772.ko ../KO/.
                fi

	fi
  
	sleep 1 

	echo ""
	echo "modules strip"
	sudo $TOOLCHAIN_PREFIX-strip -g  ../KO/*
	sudo cp ../KO/* ../$MODEL_TARGET/source/rootfs-src/rootfs-vdlinux/lib/modules/.
	sudo rm -rf ../KO

	echo "Do you want to re-build ROOTFS utils? [y/n]"
	read answer
	if [ $answer == "y" ]
	then
		make_rootfs_util
	fi

	cd ../$MODEL_TARGET/source/rootfs-src
	sudo tar cvzf ./rootfs-$MODEL_TARGET-new.tgz ./rootfs-vdlinux
	sudo mv -f ./rootfs-$MODEL_TARGET-new.tgz ./rootfs-"$MODEL_TARGET"_"$RELEASE_MODE".tgz
	cd -

	sudo $TOOLCHAIN_PREFIX-strip ../$MODEL_TARGET/source/rootfs-src/rootfs-vdlinux/mtd_exe/lib/*.*

	echo "making rootfs image"

	if [ $MODEL_TARGET == "HawkP" ]
        then
		if [ $RELEASE_MODE == "debug" ] || [ $RELEASE_MODE == "perf" ] || [ $RELEASE_MODE == "product" ]
		then
			cd $BSP_P4_DIR/Tools/vdfs-tools
		        make clean
		        make
		        cd $ROOT_DIR
			sudo $BSP_P4_DIR/Tools/vdfs-tools/mkfs.vdfs -q $ROOT_DIR/config_rootfs.conf -H $BSP_P4_DIR/Tools/CIP_UTIL/res/prkey_2048.txt -P $BSP_P4_DIR/Tools/CIP_UTIL/res/pukey_2048.txt -r ../$MODEL_TARGET/source/rootfs-src/rootfs-vdlinux/ -i rootfs.img --no-all-root
		fi
        else
		sudo mksquashfs4.2.a ../$MODEL_TARGET/source/rootfs-src/rootfs-vdlinux/ rootfs.img
	fi

	if [ $? != "0" ]
	then
		echo "================================================================"
		echo "rootfs.img : mksquashfs4.2.a failed !!!!!!!!!!!!!!!!!!!!!!!!!"
		echo "================================================================"
		exit
	fi

	sudo chmod +rw rootfs.img

	sudo rm ../$MODEL_TARGET/source/rootfs-src/rootfs-vdlinux -rf
}

make_product_config_file()
{
	RELEASE_DEF_CONFIG=$1

	perl -pi -e 's/CONFIG_RELAY=y/# CONFIG_RELAY is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_KALLSYMS=y/# CONFIG_KALLSYMS is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_PERF_EVENTS=y/# CONFIG_PERF_EVENTS is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_PROFILING=y/# CONFIG_PROFILING is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_KPROBES=y/# CONFIG_KPROBES is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_SPLIT_PTLOCK_CPUS=999999/CONFIG_SPLIT_PTLOCK_CPUS=4/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_CC_STACKPROTECTOR=y/# CONFIG_CC_STACKPROTECTOR is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_USB_DEBUG=y/# CONFIG_USB_DEBUG is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_USB_ANNOUNCE_NEW_DEVICES=y/# CONFIG_USB_ANNOUNCE_NEW_DEVICES is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_SCSI_LOGGING=y/# CONFIG_SCSI_LOGGING is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_EMMCFS_DEBUG=y/# CONFIG_EMMCFS_DEBUG is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_NETWORK_FILESYSTEMS=y/# CONFIG_NETWORK_FILESYSTEMS is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_PRINTK_TIME=y/# CONFIG_PRINTK_TIME is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_HEADERS_CHECK=y/# CONFIG_HEADERS_CHECK is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_LOCKUP_DETECTOR=y/# CONFIG_LOCKUP_DETECTOR is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_DEBUG_SPINLOCK=y/# CONFIG_DEBUG_SPINLOCK is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_DEBUG_INFO=y/# CONFIG_DEBUG_INFO is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_FTRACE=y/# CONFIG_FTRACE is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_DEBUG_USER=y/# CONFIG_DEBUG_USER is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/# CONFIG_MOUNT_SECURITY is not set/CONFIG_MOUNT_SECURITY=y/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_SERIAL_INPUT_ENABLE_HELP_MSG=y/# CONFIG_SERIAL_INPUT_ENABLE_HELP_MSG is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_KDEBUGD=y/# CONFIG_KDEBUGD is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_DEBUG_STACKOVERFLOW=y/# CONFIG_DEBUG_STACKOVERFLOW is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_IRQ_TIME=y/# CONFIG_IRQ_TIME is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_OPEN_FILE_CHECKER=y/# CONFIG_OPEN_FILE_CHECKER is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_CRYPTO_DEFLATE=y/# CONFIG_CRYPTO_DEFLATE is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_BINARY_PRINTF=y/# CONFIG_BINARY_PRINTF is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/# CONFIG_VD_RELEASE is not set/CONFIG_VD_RELEASE=y/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_DEBUG_KERNEL=y/# CONFIG_DEBUG_KERNEL is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_CRYPTO=y/# CONFIG_CRYPTO is not set/g' $RELEASE_DEF_CONFIG
	# 20150123 CONFIG_HW_PERF_EVENTS=n (debug/perf/product)
	# 20150320 CONFIG_HW_PERF_EVENTS=y (debug/perf)
	if [ $MODEL_TARGET == "HAWKA" ]
	then
		perl -pi -e 's/CONFIG_HW_PERF_EVENTS=y/# CONFIG_HW_PERF_EVENTS is not set/g' $RELEASE_DEF_CONFIG
	fi

	perl -pi -e 's/CONFIG_KALLSYMS_ALL=y/# CONFIG_KALLSYMS_ALL is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_VM_EVENT_COUNTERS=y/# CONFIG_VM_EVENT_COUNTERS is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_IOSCHED_DEADLINE=y/# CONFIG_IOSCHED_DEADLINE is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_ORSAY_DEBUG=y/# CONFIG_ORSAY_DEBUG is not set/g' $RELEASE_DEF_CONFIG
		
	# echo -e "\n# CONFIG_UIDGID_STRICT_TYPE_CHECKS is not set" >> $RELEASE_DEF_CONFIG
	echo -e "\nCONFIG_ENABLE_OOM_DEBUG_LOGS=y" >> $RELEASE_DEF_CONFIG

	#############################################################################################
	# For PR
	# LPJ Setting 

        if [ "$MODEL_TARGET" = "PreHawk" ] || [ "$MODEL_TARGET" = "PreHawk_AV" ] || [ "$MODEL_TARGET" = "HawkP" ] || [ "$MODEL_TARGET" = "HawkP_TV" ] || [ $MODEL_TARGET == "HawkM" ]
        then
                perl -pi -e 's/# CONFIG_LPJ_MANUAL_SETTING is not set/CONFIG_LPJ_MANUAL_SETTING=y\nCONFIG_LPJ_VALUE=96000/g' $RELEASE_DEF_CONFIG
        fi

        if [ $MODEL_TARGET == "HAWKA" ]
        then
                perl -pi -e 's/# CONFIG_LPJ_MANUAL_SETTING is not set/CONFIG_LPJ_MANUAL_SETTING=y\nCONFIG_LPJ_VALUE=3174400/g' $RELEASE_DEF_CONFIG
        fi

	# SHUTDOWN & SERIAL_INPUT_ENABLE_ONLY_NUMBER Setting
	perl -pi -e 's/CONFIG_ELF_CORE=y/# CONFIG_ELF_CORE is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/# CONFIG_SHUTDOWN is not set/CONFIG_SHUTDOWN=y/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/# CONFIG_SERIAL_INPUT_ENABLE_ONLY_NUMBER is not set/CONFIG_SERIAL_INPUT_ENABLE_ONLY_NUMBER=y/g' $RELEASE_DEF_CONFIG
	#############################################################################################
	
	# TNTFS disable
	if [ "$MODEL_TARGET" == "PreHawk" ] || [ "$MODEL_TARGET" == "PreHawk_AV" ] || [ "$MODEL_TARGET" = "HawkP" ] || [ "$MODEL_TARGET" = "HawkP_TV" ] || [ $MODEL_TARGET == "HawkM" ] || [ $MODEL_TARGET == "HAWKA" ]
	then
		perl -pi -e 's/CONFIG_TNTFS_FS=m/# CONFIG_TNTFS_FS is not set/g' $RELEASE_DEF_CONFIG
	fi

	# 20131022
	perl -pi -e 's/CONFIG_ALLOW_ALL_USER_COREDUMP=y/# CONFIG_ALLOW_ALL_USER_COREDUMP is not set/g' $RELEASE_DEF_CONFIG
	# 20131205
	perl -pi -e 's/CONFIG_UNHANDLED_IRQ_TRACE_DEBUGGING=y/# CONFIG_UNHANDLED_IRQ_TRACE_DEBUGGING is not set/g' $RELEASE_DEF_CONFIG
	
	# 20131217 Existing Config of the Linux Team
	perl -pi -e 's/CONFIG_BINFMT_ELF_COMP=y/# CONFIG_BINFMT_ELF_COMP is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_FUNCTION_TRACER=y/# CONFIG_FUNCTION_TRACER is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_OPROFILE=y/# CONFIG_OPROFILE is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_RING_BUFFER_ALLOW_SWAP=y/# CONFIG_RING_BUFFER_ALLOW_SWAP is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_SCHED_TRACER=y/# CONFIG_SCHED_TRACER is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_STACK_TRACER=y/# CONFIG_STACK_TRACER is not set/g' $RELEASE_DEF_CONFIG
	# 20131217 New Config of the Linux Team
	perl -pi -e 's/CONFIG_BLK_DEV_IO_TRACE=y/# CONFIG_BLK_DEV_IO_TRACE is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_IRQSOFF_TRACER=y/# CONFIG_IRQSOFF_TRACER is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_PREEMPT_TRACER=y/# CONFIG_PREEMPT_TRACER is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_BRANCH_PROFILE_NONE=y/# CONFIG_BRANCH_PROFILE_NONE is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_CACHE_ANALYZER=y/# CONFIG_CACHE_ANALYZER is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_CONTEXT_SWITCH_TRACER=y/# CONFIG_CONTEXT_SWITCH_TRACER is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_DWARF_MODULE=y/# CONFIG_DWARF_MODULE is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_DYNAMIC_FTRACE=y/# CONFIG_DYNAMIC_FTRACE is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_ELF_MODULE=y/# CONFIG_ELF_MODULE is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_EVENT_POWER_TRACING_DEPRECATED=y/# CONFIG_EVENT_POWER_TRACING_DEPRECATED is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_EVENT_TRACING=y/# CONFIG_EVENT_TRACING is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_FUNCTION_GRAPH_TRACER=y/# CONFIG_FUNCTION_GRAPH_TRACER is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_GENERIC_TRACER=y/# CONFIG_GENERIC_TRACER is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_IRQ_WORK=y/# CONFIG_IRQ_WORK is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_MEMORY_VALIDATOR=y/# CONFIG_MEMORY_VALIDATOR is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_NOP_TRACER=y/# CONFIG_NOP_TRACER is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_RING_BUFFER=y/# CONFIG_RING_BUFFER is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_SCHED_HISTORY=y/# CONFIG_SCHED_HISTORY is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_SCHED_TRACER=y/# CONFIG_SCHED_TRACER is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_SHOW_TASK_PRIORITY=y/# CONFIG_SHOW_TASK_PRIORITY is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_SHOW_TASK_STATE=y/# CONFIG_SHOW_TASK_STATE is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_SHOW_USER_MAPS_WITH_PID=y/# CONFIG_SHOW_USER_MAPS_WITH_PID is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_SHOW_USER_STACK_WITH_PID=y/# CONFIG_SHOW_USER_STACK_WITH_PID is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_SHOW_USER_THREAD_REGS=y/# CONFIG_SHOW_USER_THREAD_REGS is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_STACKTRACE=y/# CONFIG_STACKTRACE is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_TASK_FOR_COREDUMP=y/# CONFIG_TASK_FOR_COREDUMP is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_TASK_STATE_BACKTRACE=y/# CONFIG_TASK_STATE_BACKTRACE is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_TRACE_CLOCK=y/# CONFIG_TRACE_CLOCK is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_TRACE_IRQFLAGS=y/# CONFIG_TRACE_IRQFLAGS is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_TRACE_THREAD_PC=y/# CONFIG_TRACE_THREAD_PC is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_TRACEPOINTS=y/# CONFIG_TRACEPOINTS is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_TRACER_MAX_TRACE=y/# CONFIG_TRACER_MAX_TRACE is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_TRACING=y/# CONFIG_TRACING is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_VIRTUAL_TO_PHYSICAL=y/# CONFIG_VIRTUAL_TO_PHYSICAL is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_OLD_MCOUNT=y/# CONFIG_OLD_MCOUNT is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_ZRAM_DEBUG=y/# CONFIG_ZRAM_DEBUG is not set/g' $RELEASE_DEF_CONFIG

	# 20140102 
	perl -pi -e 's/CONFIG_PM_BUG_ON_PROBLEM=y/# CONFIG_PM_BUG_ON_PROBLEM is not set/g' $RELEASE_DEF_CONFIG
	
	# 20140114 Disable CONFIG_PRINTK_CPUID for product mode
	perl -pi -e 's/CONFIG_PRINTK_CPUID=y/# CONFIG_PRINTK_CPUID is not set/g' $RELEASE_DEF_CONFIG
	
	# 20140207 Disable CONFIG_DEBUG_OBJECTS, CONFIG_DEBUG_OBJECTS_PERCPU_COUNTER (Only DEBUG mode enable) 
	perl -pi -e 's/CONFIG_DEBUG_OBJECTS=y/# CONFIG_DEBUG_OBJECTS is not set/g' $RELEASE_DEF_CONFIG
	perl -pi -e 's/CONFIG_DEBUG_OBJECTS_PERCPU_COUNTER=y/# CONFIG_DEBUG_OBJECTS_PERCPU_COUNTER is not set/g' $RELEASE_DEF_CONFIG

        # 20141023 Disable config related to VDFS
        # 20150127 CONFIG_VDFS_POSIX_ACL=n to debug,perf,product mode
	# perl -pi -e 's/CONFIG_VDFS_POSIX_ACL=y/# CONFIG_VDFS_POSIX_ACL is not set/g' $RELEASE_DEF_CONFIG
        perl -pi -e 's/CONFIG_VDFS_DEBUG=y/# CONFIG_VDFS_DEBUG is not set/g' $RELEASE_DEF_CONFIG
        perl -pi -e 's/CONFIG_VDFS_PANIC_ON_ERROR=y/# CONFIG_VDFS_PANIC_ON_ERROR is not set/g' $RELEASE_DEF_CONFIG
        perl -pi -e 's/CONFIG_VDFS_SW_DECOMPRESS_RETRY=y/# CONFIG_VDFS_SW_DECOMPRESS_RETRY is not set/g' $RELEASE_DEF_CONFIG
        perl -pi -e 's/CONFIG_VDFS_CRC_CHECK=y/# CONFIG_VDFS_CRC_CHECK is not set/g' $RELEASE_DEF_CONFIG

        # 20141029 CONFIG_SLUB_DEBUG=n
        perl -pi -e 's/CONFIG_SLUB_DEBUG=y/# CONFIG_SLUB_DEBUG is not set/g' $RELEASE_DEF_CONFIG

        # 20141030 CONFIG_DEBUG_HIGHMEM=n
        perl -pi -e 's/CONFIG_DEBUG_HIGHMEM=y/# CONFIG_DEBUG_HIGHMEM is not set/g' $RELEASE_DEF_CONFIG

        # 20141031 CONFIG_PAGE_OWNER=n
        perl -pi -e 's/CONFIG_PAGE_OWNER=y/# CONFIG_PAGE_OWNER is not set/g' $RELEASE_DEF_CONFIG

        # 20141103 CONFIG_VD_HMP_DEBUG=n
        perl -pi -e 's/CONFIG_VD_HMP_DEBUG=y/# CONFIG_VD_HMP_DEBUG is not set/g' $RELEASE_DEF_CONFIG

        # 20141110
	# 20150116 CONFIG_MUPT_TRACE=n to debug,perf,product mode in HawkP and HAWKA 
	if [ "$MODEL_TARGET" = "HawkP_TV" ] || [ "$MODEL_TARGET" = "HawkM" ]
	then
        	perl -pi -e 's/CONFIG_MUPT_TRACE=y/# CONFIG_MUPT_TRACE is not set/g' $RELEASE_DEF_CONFIG
	fi
        perl -pi -e 's/CONFIG_KDML=y/# CONFIG_KDML is not set/g' $RELEASE_DEF_CONFIG

        if [ "$MODEL_TARGET" = "HawkP" ] || [ "$MODEL_TARGET" = "HawkP_TV" ]
        then
                perl -pi -e 's/CONFIG_CHECK_A15_INSTRUCTION=y/# CONFIG_CHECK_A15_INSTRUCTION is not set/g' $RELEASE_DEF_CONFIG
        fi

	# 20141111 CONFIG_OPEN_FILE_CHECKER=y , 20150102 Add HawkM, HAWKA
	if [ "$MODEL_TARGET" = "HawkP" ] || [ "$MODEL_TARGET" = "HawkM" ] || [ "$MODEL_TARGET" = "HAWKA" ]
	then
		perl -pi -e 's/# CONFIG_OPEN_FILE_CHECKER is not set/CONFIG_OPEN_FILE_CHECKER=y/g' $RELEASE_DEF_CONFIG
	fi

        # 20141202 CONFIG_VDFS_DEBUG_AUTHENTICAION=n
        perl -pi -e 's/CONFIG_VDFS_DEBUG_AUTHENTICAION=y/# CONFIG_VDFS_DEBUG_AUTHENTICAION is not set/g' $RELEASE_DEF_CONFIG

	# 20141208 CONFIG_MMC_DEBUG_IOCTL=n
        perl -pi -e 's/CONFIG_MMC_DEBUG_IOCTL=y/# CONFIG_MMC_DEBUG_IOCTL is not set/g' $RELEASE_DEF_CONFIG

        # 20141215 CONFIG_PID_IN_CONTEXTIDR=n
        perl -pi -e 's/CONFIG_PID_IN_CONTEXTIDR=y/# CONFIG_PID_IN_CONTEXTIDR is not set/g' $RELEASE_DEF_CONFIG

        # 20141224 CONFIG_CMA_DEBUG=n
        perl -pi -e 's/CONFIG_CMA_DEBUG=y/# CONFIG_CMA_DEBUG is not set/g' $RELEASE_DEF_CONFIG

	# 20141229 CONFIG_MODULES_USE_LONG_CALLS=n
	echo -e "\n# CONFIG_MODULES_USE_LONG_CALLS is not set" >> $RELEASE_DEF_CONFIG
}

make_perf_config_file()
{
	PERF_DEF_CONFIG=$1
	
	make_product_config_file $PERF_DEF_CONFIG

	perl -pi -e 's/CONFIG_SHUTDOWN=y/# CONFIG_SHUTDOWN is not set/g' $PERF_DEF_CONFIG
	perl -pi -e 's/CONFIG_SERIAL_INPUT_ENABLE_ONLY_NUMBER=y/# CONFIG_SERIAL_INPUT_ENABLE_ONLY_NUMBER is not set/g' $PERF_DEF_CONFIG
		
	perl -pi -e 's/# CONFIG_PROFILING is not set/CONFIG_PROFILING=y/g' $PERF_DEF_CONFIG
	perl -pi -e 's/# CONFIG_FTRACE is not set/CONFIG_FTRACE=y/g' $PERF_DEF_CONFIG

	# 20150123 CONFIG_HW_PERF_EVENTS=n (debug/perf/product)
	# 20150320 CONFIG_HW_PERF_EVENTS=y (debug/perf)
	if [ "$MODEL_TARGET" = "HAWKA" ]
	then
		perl -pi -e 's/# CONFIG_HW_PERF_EVENTS is not set/CONFIG_HW_PERF_EVENTS=y/g' $PERF_DEF_CONFIG
	fi

	perl -pi -e 's/# CONFIG_PERF_EVENTS is not set/CONFIG_PERF_EVENTS=y/g' $PERF_DEF_CONFIG
	# For CONFIG_ENABLE_DEFAULT_TRACERS=y
	perl -pi -e 's/CONFIG_IRQSOFF_TRACER=y/# CONFIG_IRQSOFF_TRACER is not set/g' $PERF_DEF_CONFIG
	perl -pi -e 's/CONFIG_PREEMPT_TRACER=y/# CONFIG_PREEMPT_TRACER is not set/g' $PERF_DEF_CONFIG
	perl -pi -e 's/CONFIG_SCHED_TRACER=y/# CONFIG_SCHED_TRACER is not set/g' $PERF_DEF_CONFIG
	perl -pi -e 's/CONFIG_BLK_DEV_IO_TRACE=y/# CONFIG_BLK_DEV_IO_TRACE is not set/g' $PERF_DEF_CONFIG
	echo -e "\nCONFIG_ENABLE_DEFAULT_TRACERS=y" >> $PERF_DEF_CONFIG

	# TNTFS setting
	if [ $MODEL_TARGET = "PreHawk" ] || [ $MODEL_TARGET = "PreHawk_AV" ] || [ "$MODEL_TARGET" = "HawkP" ] || [ "$MODEL_TARGET" = "HawkP_TV" ] || [ $MODEL_TARGET == "HawkM" ] || [ $MODEL_TARGET == "HAWKA" ]
	then
		perl -pi -e 's/# CONFIG_TNTFS_FS is not set/CONFIG_TNTFS_FS=m/g' $PERF_DEF_CONFIG
	fi

	# 20141023 Disable config related to VDFS
	# 20150127 CONFIG_VDFS_POSIX_ACL=n to debug,perf,product mode
	# perl -pi -e 's/CONFIG_VDFS_POSIX_ACL=y/# CONFIG_VDFS_POSIX_ACL is not set/g' $PERF_DEF_CONFIG
	perl -pi -e 's/CONFIG_VDFS_DEBUG=y/# CONFIG_VDFS_DEBUG is not set/g' $PERF_DEF_CONFIG
	perl -pi -e 's/CONFIG_VDFS_PANIC_ON_ERROR=y/# CONFIG_VDFS_PANIC_ON_ERROR is not set/g' $PERF_DEF_CONFIG
	perl -pi -e 's/CONFIG_VDFS_SW_DECOMPRESS_RETRY=y/# CONFIG_VDFS_SW_DECOMPRESS_RETRY is not set/g' $PERF_DEF_CONFIG
	perl -pi -e 's/CONFIG_VDFS_CRC_CHECK=y/# CONFIG_VDFS_CRC_CHECK is not set/g' $PERF_DEF_CONFIG

	# 20141029 CONFIG_SLUB_DEBUG=n
	perl -pi -e 's/CONFIG_SLUB_DEBUG=y/# CONFIG_SLUB_DEBUG is not set/g' $PERF_DEF_CONFIG

	# 20141030 CONFIG_DEBUG_HIGHMEM=n
	perl -pi -e 's/CONFIG_DEBUG_HIGHMEM=y/# CONFIG_DEBUG_HIGHMEM is not set/g' $PERF_DEF_CONFIG

	# 20141031 CONFIG_PAGE_OWNER=n
	perl -pi -e 's/CONFIG_PAGE_OWNER=y/# CONFIG_PAGE_OWNER is not set/g' $PERF_DEF_CONFIG

	# 20141103 CONFIG_VD_HMP_DEBUG=n
	perl -pi -e 's/CONFIG_VD_HMP_DEBUG=y/# CONFIG_VD_HMP_DEBUG is not set/g' $PERF_DEF_CONFIG

	# 20141110
	# 20150116 CONFIG_MUPT_TRACE=n to debug,perf,product mode in HawkP and HAWKA
	if [ "$MODEL_TARGET" = "HawkP_TV" ] || [ "$MODEL_TARGET" = "HawkM" ]
        then 
		perl -pi -e 's/CONFIG_MUPT_TRACE=y/# CONFIG_MUPT_TRACE is not set/g' $PERF_DEF_CONFIG
	fi
	perl -pi -e 's/CONFIG_KDML=y/# CONFIG_KDML is not set/g' $PERF_DEF_CONFIG
	
	if [ "$MODEL_TARGET" = "HawkP" ] || [ "$MODEL_TARGET" = "HawkP_TV" ]
	then
		perl -pi -e 's/CONFIG_CHECK_A15_INSTRUCTION=y/# CONFIG_CHECK_A15_INSTRUCTION is not set/g' $PERF_DEF_CONFIG
	fi

        # 20141111 CONFIG_OPEN_FILE_CHECKER=y , 20150102 Add HawkM, HAWKA
        if [ "$MODEL_TARGET" = "HawkP" ] || [ "$MODEL_TARGET" = "HawkM" ] || [ "$MODEL_TARGET" = "HAWKA" ]
        then
                perl -pi -e 's/# CONFIG_OPEN_FILE_CHECKER is not set/CONFIG_OPEN_FILE_CHECKER=y/g' $PERF_DEF_CONFIG
        fi
	
	# 20141202 CONFIG_VDFS_DEBUG_AUTHENTICAION=n
	perl -pi -e 's/CONFIG_VDFS_DEBUG_AUTHENTICAION=y/# CONFIG_VDFS_DEBUG_AUTHENTICAION is not set/g' $PERF_DEF_CONFIG

        # 20141208 CONFIG_MMC_DEBUG_IOCTL=y
        perl -pi -e 's/# CONFIG_MMC_DEBUG_IOCTL is not set/CONFIG_MMC_DEBUG_IOCTL=y/g' $PERF_DEF_CONFIG

	# 20141215 CONFIG_PID_IN_CONTEXTIDR=n
        perl -pi -e 's/CONFIG_PID_IN_CONTEXTIDR=y/# CONFIG_PID_IN_CONTEXTIDR is not set/g' $PERF_DEF_CONFIG

	# 20141224 CONFIG_CMA_DEBUG=n
        perl -pi -e 's/CONFIG_CMA_DEBUG=y/# CONFIG_CMA_DEBUG is not set/g' $PERF_DEF_CONFIG
	
}	

make_mupt_config_file()
{
	MUPT_DEF_CONFIG=$1
	
	perl -pi -e 's/# CONFIG_MUPT_TRACE is not set/CONFIG_MUPT_TRACE=y/g' $MUPT_DEF_CONFIG
}	

make_kasan_config_file()
{
        KASAN_DEF_CONFIG=$1

        perl -pi -e 's/CONFIG_CROSS_COMPILE="arm-v7a15a7v5r1-linux-gnueabi-"/CONFIG_CROSS_COMPILE="arm-v7a15a7v5r2-linux-gnueabi-"/g' $KASAN_DEF_CONFIG
	perl -pi -e 's/# CONFIG_KASAN is not set/CONFIG_KASAN=y/g' $KASAN_DEF_CONFIG
	echo -e "\nCONFIG_KASAN_STACK=y" >> $KASAN_DEF_CONFIG 
}

backup_config_file()
{
	cd $ROOT_DIR
	echo "Do you want to backup .config to $MODEL_TARGET default $RELEASE_DEBUG_CHECK configs? [y/n]"
	read answer

	if [ $answer == "y" ]
	then
		echo "Backup/Copy .config"
		if [ $CORE_TYPE == "ARM" ]
		then
			chmod +rw ./arch/arm/configs/"$MODEL_TARGET"_"$RELEASE_DEBUG_CHECK"_defconfig
			cp -rf .config ./arch/arm/configs/"$MODEL_TARGET"_"$RELEASE_DEBUG_CHECK"_defconfig
		fi
	fi
	
	if [ $RELEASE_MODE == "debug" ] || [ $RELEASE_MODE == "perf" ] || [ $RELEASE_MODE == "product" ]
	then
		CONFIG_PATH=arch/arm/configs

		if [ 1 ]
		then
                        if [ $MODEL_TARGET == "HawkP" ] || [ $MODEL_TARGET == "HawkM" ] || [ $MODEL_TARGET == "HAWKA" ]
                        then
				echo "Do you want to make latest product & perf config of $MODEL_TARGET? [y/n]"
				read answer

				if [ $answer == "y" ]
				then
					sudo cp "$CONFIG_PATH"/"$MODEL_TARGET"_debug_defconfig "$CONFIG_PATH"/"$MODEL_TARGET"_product_defconfig
					chmod +rw "$CONFIG_PATH"/"$MODEL_TARGET"_product_defconfig
					make_product_config_file "$CONFIG_PATH"/"$MODEL_TARGET"_product_defconfig

					sudo cp "$CONFIG_PATH"/"$MODEL_TARGET"_debug_defconfig "$CONFIG_PATH"/"$MODEL_TARGET"_perf_defconfig
					chmod +rw "$CONFIG_PATH"/"$MODEL_TARGET"_perf_defconfig
					make_perf_config_file "$CONFIG_PATH"/"$MODEL_TARGET"_perf_defconfig
				fi
			fi

               	fi

		if [ $MODEL_TARGET == "HawkP_TV" ] || [ $MODEL_TARGET == "HawkM" ]
                then
                        sudo cp "$CONFIG_PATH"/"$MODEL_TARGET"_debug_defconfig "$CONFIG_PATH"/"$MODEL_TARGET"_kasan_defconfig
                        chmod +rw "$CONFIG_PATH"/"$MODEL_TARGET"_kasan_defconfig
                        make_kasan_config_file "$CONFIG_PATH"/"$MODEL_TARGET"_kasan_defconfig
		fi
	fi
}

copy_config_file_perf_product()
{
        cd $ROOT_DIR
        echo "Do you want to backup .config to $MODEL_TARGET default $RELEASE_DEBUG_CHECK configs? [y/n]"
        read answer

        if [ $answer == "y" ]
        then
                echo "Backup/Copy .config"
                if [ $CORE_TYPE == "ARM" ]
                then
                        chmod +rw ./arch/arm/configs/"$MODEL_TARGET"_"$RELEASE_DEBUG_CHECK"_defconfig
                        cp -rf .config ./arch/arm/configs/"$MODEL_TARGET"_"$RELEASE_DEBUG_CHECK"_defconfig
                fi
        fi

        if [ $RELEASE_MODE == "debug" ]
        then
                CONFIG_PATH=arch/arm/configs

                if [ 1 ]
                then
                        if [ $MODEL_TARGET == "HawkP" ] || [ $MODEL_TARGET == "HawkM" ] || [ $MODEL_TARGET == "HAWKA" ]
                        then
                                echo "Do you want to make latest product & perf config of $MODEL_TARGET? [y/n]"
                                read answer

                                if [ $answer == "y" ]
                                then
                                        sudo cp "$CONFIG_PATH"/"$MODEL_TARGET"_debug_defconfig "$CONFIG_PATH"/"$MODEL_TARGET"_product_defconfig
                                        chmod +rw "$CONFIG_PATH"/"$MODEL_TARGET"_product_defconfig
                                        make_product_config_file "$CONFIG_PATH"/"$MODEL_TARGET"_product_defconfig

                                        sudo cp "$CONFIG_PATH"/"$MODEL_TARGET"_debug_defconfig "$CONFIG_PATH"/"$MODEL_TARGET"_perf_defconfig
                                        chmod +rw "$CONFIG_PATH"/"$MODEL_TARGET"_perf_defconfig
                                        make_perf_config_file "$CONFIG_PATH"/"$MODEL_TARGET"_perf_defconfig
                                fi
                        fi

                fi

        fi

        echo "Copy config file Completed ..."
        exit 0
}

make_header_file()
{
	rm ../linux-3.10.28_header -rf

	mkdir ../linux-3.10.28_header
	cp ./drivers ./arch ./scripts ./include ../linux-3.10.28_header -ar
	mkdir -p ../linux-3.10.28_header/init/secureboot
	cp ./init/secureboot/include ../linux-3.10.28_header/init/secureboot -ar
	cp ./Kbuild ./Makefile ./modules.builtin ./modules.order ./Module.symvers ../linux-3.10.28_header
	cp .config ../linux-3.10.28_header
	# 20140227
	# cp ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/vmlinux ../linux-3.10.28_header
	find ../linux-3.10.28_header/drivers -name "*.c" -exec rm {} -rf \;
	find ../linux-3.10.28_header/drivers -name "*.s" -exec rm {} -rf \;
	find ../linux-3.10.28_header/drivers -name "*.S" -exec rm {} -rf \;
	find ../linux-3.10.28_header/arch -name "*.c" -exec rm {} -rf \;
	find ../linux-3.10.28_header/arch -name "*.s" -exec rm {} -rf \;
	find ../linux-3.10.28_header/arch -name "*.S" -exec rm {} -rf \;
	find ../linux-3.10.28_header -name "*.ko" -exec rm {} -rf \;
	# 20131118
	find ../linux-3.10.28_header -name "*.o" -exec rm {} -rf \;
	find ../linux-3.10.28_header -name ".*.cmd" -exec rm {} -rf \;
	find ../linux-3.10.28_header -name "*.cmd" -exec rm {} -rf \;
        # 20140116
	find ../linux-3.10.28_header -name "*.a" -exec rm {} -rf \;
	# 20140623
	rm -f ../linux-3.10.28_header/scripts/basic/fixdep
	rm -f ../linux-3.10.28_header/scripts/recordmcount
	rm -f ../linux-3.10.28_header/scripts/mod/modpost
 
	# Open when gator is ready
	if false
	# if [ $CORE_TYPE == "ARM" ]
	then
		if [ $RELEASE_MODE == "debug" ] || [ $RELEASE_MODE == "perf" ]
		then
			echo "========================================================"
			echo " Compile $RELEASE_MODE GATOR util source"
			echo "========================================================"
	  
			cd ../../Tools/gator
	    
			rm ./gator-driver -rf
			tar xvzf gator-driver.tar.gz
	    
			chmod -Rf 777 *
	    
			KERNEL_HEADER_PATH=$ROOT_DIR/../linux-3.10.28_header

			cd ./gator-driver

			make clean
			export GATOR_WITH_MALI_SUPPORT=y
			make -C $KERNEL_HEADER_PATH M=`pwd` ARCH=arm CROSS_COMPILE=$TOOLCHAIN_PREFIX- modules
			cp ./gator.ko ../../../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/streamline_module

			# gatord copy
			cp ../gatord."$MODEL_TARGET" ../../../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/streamline_module/gatord

			cd $ROOT_DIR
	   
			rm ../../Tools/gator/gator-driver -rf
	   
			sync
		fi
	fi
  
	tar zcf ../linux-3.10.28_header.only.header_"$RELEASE_MODE".tgz ../linux-3.10.28_header
	rm ../linux-3.10.28_header -rf
	sync
}

release_img()
{
	echo "Do you want to progress RELEASE process? [y/n]"
	read answer

	if [ $answer == "y" ]
	then
		echo "Copy Image to $MODEL_TARGET"
	 
		test -d ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"
		if [ $? != 0 ]
		then
			mkdir -p ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"
			sync
		fi

		if [ $ADDITIONAL_FUNCTION == "bootchart" ]
		then
			mv -f uImage  ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/Image_"$ADDITIONAL_FUNCTION"
		elif [ $ADDITIONAL_FUNCTION == $INIT ]
		then
			cp -f System.map vmlinux ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"
			mv -f uImage rootfs.img ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"
			if [ $MODEL_TARGET == "PreHawk" ] || [ $MODEL_TARGET == "PreHawk_AV" ]
			then
				mv -f uImage_US_1MP ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"
			fi

			if [ $MODEL_TARGET == "HawkP" ]
			then
				cp -f arch/arm/boot/dts/sdp1404-av.dtb ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.bin
			fi

			if [ $MODEL_TARGET == "HawkP_TV" ]
			then
				cp -f ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/uImage ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/uImage.tmp
				cp -f arch/arm/boot/dts/sdp1404-dtv.dtb ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.bin
				
				if [ -e ~/sig_gen ]
				then
					sudo ~/sig_gen HawkP ../$MODEL_TARGET/image/$RELEASE_MODE/uImage.tmp ../$MODEL_TARGET/image/$RELEASE_MODE/uImage
					cp -f ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.bin ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.bin.tmp
					sudo ~/sig_gen HawkP ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.bin.tmp ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.bin 504

					sudo rm -f ../$MODEL_TARGET/image/$RELEASE_MODE/uImage.tmp
					sudo rm -f ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.bin.tmp	
				else
					echo " There is no generator "
					mv -f ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.bin.tmp ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.bin
					exit
				fi
			fi

			if [ $MODEL_TARGET == "HawkM" ]
			then
				cd scripts/dtb_merge
				make clean
				make
				if [ $? -ne 0 ]
				then
					echo "build dtb_merge failed. Stopped..."
					exit
				fi

				cd -
				chmod 777 ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.uhd.bin
				./scripts/dtb_merge/dtb_merge ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.uhd.bin arch/arm/boot/dts/sdp1406.uhd.2.5G.dtb arch/arm/boot/dts/sdp1406.uhd.1.5G.dtb arch/arm/boot/dts/sdp1406.uhd.2.0G.dtb
				cp -f ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/uImage ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/uImage.tmp	
				
				if [ -e ~/sig_gen ]
				then
					sudo ~/sig_gen HawkM ../$MODEL_TARGET/image/$RELEASE_MODE/uImage.tmp ../$MODEL_TARGET/image/$RELEASE_MODE/uImage
					cp -f ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.uhd.bin ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.uhd.bin.tmp
					sudo ~/sig_gen HawkM ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.uhd.bin.tmp ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.uhd.bin 504

					sudo rm -f ../$MODEL_TARGET/image/$RELEASE_MODE/uImage.tmp
					sudo rm -f ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.uhd.bin.tmp
				else
					echo " There is no generator "
					mv -f ../$MODEL_TARGET/image/$RELEASE_MODE/uImage.tmp ../$MODEL_TARGET/image/$RELEASE_MODE/uImage
					mv -f ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.uhd.bin.tmp ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.uhd.bin
					exit
				fi

				cd scripts/dtb_merge
				make clean
                                make
                                if [ $? -ne 0 ]
                                then
                                        echo "build dtb_merge failed. Stopped..."
                                        exit
                                fi

				cd -
				chmod 777 ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.fhd.bin
				./scripts/dtb_merge/dtb_merge ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.fhd.bin arch/arm/boot/dts/sdp1406.fhd.dtb arch/arm/boot/dts/sdp1406.fhd.1.5G.dtb			

				if [ -e ~/sig_gen ]
				then
					cp -f ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.fhd.bin ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.fhd.bin.tmp
					sudo ~/sig_gen HawkM ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.fhd.bin.tmp ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.fhd.bin 504

					sudo rm -f ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.fhd.bin.tmp
				else
					echo " There is no generator "
					mv -f ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.fhd.bin.tmp ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.fhd.bin
					exit
				fi
				cd scripts/dtb_merge
                                make clean					
				
				cd -
			fi

			if [ $MODEL_TARGET == "HAWKA" ]
                        then	
				if [ $RELEASE_DEBUG_CHECK == "debug" ]
				then
					cp -f arch/arm/boot/dts/sdp1412.dtb ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.bin
				fi

				if [ $RELEASE_DEBUG_CHECK == "perf" ] || [ $RELEASE_DEBUG_CHECK == "product" ]
				then
					cp -f ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/uImage ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/uImage.tmp	
					cp -f arch/arm/boot/dts/sdp1412.dtb ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.bin
			
					if [ -e ~/sig_gen ]
					then
						sudo ~/sig_gen HawkA ../$MODEL_TARGET/image/$RELEASE_MODE/uImage.tmp ../$MODEL_TARGET/image/$RELEASE_MODE/uImage
						cp -f ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.bin ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.bin.tmp
						sudo ~/sig_gen HawkA ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.bin.tmp ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.bin 504

						sudo rm -f ../$MODEL_TARGET/image/$RELEASE_MODE/uImage.tmp
						sudo rm -f ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.bin.tmp
					else
						echo " There is no generator "
						mv -f ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.bin.tmp ../$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.bin
						exit
					fi
				fi
			fi

		else
			PRINT_MSG=" Not yet implemented this function..."
			print_msg
			exit	
		fi
    
		echo "Making kernel header only"
		make_header_file

		test -d ../$MODEL_TARGET/source/kernel-src
		if [ $? != 0 ]
		then
			mkdir -p ../$MODEL_TARGET/source/kernel-src
			sync
		fi

		mv -f ../linux-3.10.28_header.only.header_"$RELEASE_MODE".tgz ../$MODEL_TARGET/source/kernel-src

		sync
	  
		cd $ROOT_DIR
	fi

	PRINT_MSG=$PRINT_MSG_END
	print_msg
}

release_img_for_P4()
{
	echo "Do you want to move BSP data to P4 directory? [y/n]"
	read answer

	if [ $answer == "y" ]
	then
		cd $ROOT_DIR

		if [ ! -d $BSP_P4_DIR ]
		then
			echo "$BSP_P4_DIR is not found...Stopped..."
			exit
		fi

		echo ""
		echo "Move BSP data to $BSP_P4_DIR/$MODEL_TARGET"
		echo ""
	
		if [ $ADDITIONAL_FUNCTION == $INIT ]
		then
			# copy image to P4 folder
			cp -af ../$MODEL_TARGET/image/* $BSP_P4_DIR/$MODEL_TARGET/image/
			if [ -d ../$MODEL_TARGET/source/kernel-src ]
			then
				cp -af ../$MODEL_TARGET/source/kernel-src/* $BSP_P4_DIR/$MODEL_TARGET/source/kernel-src/
			fi

			# remove image and kernel header in git
			rm -rf ../$MODEL_TARGET/image
			rm -rf ../$MODEL_TARGET/source/kernel-src

			# copy rootfs source to P4 folder
			rm -rf $BSP_P4_DIR/$MODEL_TARGET/source/rootfs-src/*
			cp -af ../$MODEL_TARGET/source/rootfs-src/* $BSP_P4_DIR/$MODEL_TARGET/source/rootfs-src/
		else
			PRINT_MSG=" Not yet implemented this function..."
			print_msg
			exit	
		fi
    
		sync
	  
		cd $ROOT_DIR
	fi
}


release_onboot()
{
	cd $ROOT_DIR 

	echo ""
	echo "Onboot copy : $MODEL_TARGET"
	echo ""

	if [ $MODEL_TARGET == "HawkP" ]
	then
		cp -f ../../sa15_onboot/Hawk.P-Tizen/Hawk.P-Orsay/image/debug/onboot_Orsay_AV.bin $BSP_P4_DIR/$MODEL_TARGET/image/debug/onboot.bin
		cp -f ../../sa15_onboot/Hawk.P-Tizen/Hawk.P-Orsay/image/debug/onboot_Orsay_AV $BSP_P4_DIR/$MODEL_TARGET/image/debug/
		cp -f ../../sa15_onboot/Hawk.P-Tizen/Hawk.P-Orsay/image/debug/onboot_Orsay_AV.ddr.init $BSP_P4_DIR/$MODEL_TARGET/image/debug/ddr.init
	elif [ $MODEL_TARGET == "HawkP_TV" ]
	then
		cp -f ../../sa15_onboot/Hawk.P-Tizen/Hawk.P-Orsay/image/debug/onboot_Orsay_TV.bin $BSP_P4_DIR/$MODEL_TARGET/image/debug/onboot.bin
		cp -f ../../sa15_onboot/Hawk.P-Tizen/Hawk.P-Orsay/image/debug/onboot_Orsay_TV $BSP_P4_DIR/$MODEL_TARGET/image/debug/
		cp -f ../../sa15_onboot/Hawk.P-Tizen/Hawk.P-Orsay/image/debug/onboot_Orsay_TV.ddr.init $BSP_P4_DIR/$MODEL_TARGET/image/debug/ddr.init
	elif [ $MODEL_TARGET == "HawkM" ] || [ $MODEL_TARGET == "HAWKA" ]
	then
		cp -f ../../sa15_onboot/Hawk.P-Tizen/Hawk.M-Orsay/image/debug/onboot_Orsay_TV_UHD.bin $BSP_P4_DIR/$MODEL_TARGET/image/debug/onboot.uhd.bin
		cp -f ../../sa15_onboot/Hawk.P-Tizen/Hawk.M-Orsay/image/debug/onboot_Orsay_TV_UHD $BSP_P4_DIR/$MODEL_TARGET/image/debug/onboot_Orsay_TV.uhd
		cp -f ../../sa15_onboot/Hawk.P-Tizen/Hawk.M-Orsay/image/debug/onboot_Orsay_TV_UHD.ddr.init $BSP_P4_DIR/$MODEL_TARGET/image/debug/ddr.uhd.init
		cp -f ../../sa15_onboot/Hawk.P-Tizen/Hawk.M-Orsay/image/debug/onboot_Orsay_TV_FHD.bin $BSP_P4_DIR/$MODEL_TARGET/image/debug/onboot.fhd.bin
		cp -f ../../sa15_onboot/Hawk.P-Tizen/Hawk.M-Orsay/image/debug/onboot_Orsay_TV_FHD $BSP_P4_DIR/$MODEL_TARGET/image/debug/onboot_Orsay_TV.fhd
		cp -f ../../sa15_onboot/Hawk.P-Tizen/Hawk.M-Orsay/image/debug/onboot_Orsay_TV_FHD.ddr.init $BSP_P4_DIR/$MODEL_TARGET/image/debug/ddr.fhd.init
	else
		echo "Not supported target !!!!!!!!!!!!!!!!!!!!!"
		exit 1;
	fi

	echo "Completed ...."
	exit 0
}

clean_env()
{
	make distclean

	cd $ROOT_DIR 
	
	rm -f .product 
	rm -f .debug
	rm -f .perf
	rm -f .kasan	
	rm -f .PreHawk
	rm -f .PreHawk_AV
	rm -f .HawkP
	rm -f .HawkP_TV
	rm -f .HawkM
	rm -f .HAWKA

	rm -f ./include/linux/vdlp_version.h
    
	rm -f uImage_US_1MP

	rm -f scripts/rsa_tool/rsa_tool

	cd $ROOT_DIR
}

MakeRootFSDataToCheckRelease()
{
	echo ""
	echo "################################################"
	echo "# Make RootFS Data To Check Release"
	echo "################################################"
	echo ""

	DFCR_DIR=DataForCheckRelease
	
	DFCR_ROOTFS_LIST=$DFCR_DIR/rootfs-"$MODEL_TARGET"_"$RELEASE_MODE".list
	DFCR_ROOTFS_DIR=$DFCR_DIR/rootfs-"$MODEL_TARGET"_"$RELEASE_MODE"
		
	cd $ROOT_DIR
	cd ../$MODEL_TARGET/source/rootfs-src/

	if [ ! -e $DFCR_DIR ]
	then
		mkdir $DFCR_DIR
	fi

        if [ -e rootfs-vdlinux ]
        then
                sudo rm -rf rootfs-vdlinux
        fi

	sudo tar xzf rootfs-"$MODEL_TARGET"_"$RELEASE_MODE".tgz
	
	if [ $? != "0" ]
	then
		sudo rm -rf rootfs-vdlinux
		echo "================================================================"
		echo " Make RootFS Data : tar xfz failed !!!!!!!!!!!!!!!!!!!!!!!!!"
		echo "================================================================"
		exit
	fi

	rm -f $DFCR_ROOTFS_LIST
	sudo LANG=enUS ls -anlLR --time-style=long-iso rootfs-vdlinux >& $DFCR_ROOTFS_LIST

	rm -rf $DFCR_ROOTFS_DIR
	sudo cp -af rootfs-vdlinux $DFCR_ROOTFS_DIR
	sudo chmod -R 777 $DFCR_ROOTFS_DIR
	rm -rf $DFCR_ROOTFS_DIR/dev

	for SIM_LINK_FILE in "$( find $DFCR_ROOTFS_DIR -type l )"
	do
		rm -rf $SIM_LINK_FILE
	done
		
	sudo rm -rf rootfs-vdlinux
	cd $ROOT_DIR
}


MakeRootFSImageEW()
{
        
	cd $ROOT_DIR

	echo "=============================================="
        echo "unsquashfs4.2.a test routine"
        echo "=============================================="
        cd ../$MODEL_TARGET/image/"$RELEASE_MODE"/

	sudo rm -rf squashfs-root
        sudo unsquashfs4.2.a rootfs.img

        if [ $? != "0" ]
        then
                echo "================================================================"
                echo "Test ew_rootfs.img : unsquashfs4.2.a failed !!!!!!!!!!!!!!!!!!!!!!!!!"
                echo "================================================================"
		exit
        fi

	rm -f squashfs-root/etc/profile
	cp -af ../../source/rootfs-src/ew_data/profile.ew.debug squashfs-root/etc/profile

	sudo chmod 777 squashfs-root/etc/profile
	sudo chown root:root squashfs-root/etc/profile

	cp -af ../../source/rootfs-src/ew_data/EW_update.sh squashfs-root/etc/Scripts/EW_update.sh

	sudo chmod 777 squashfs-root/etc/Scripts/EW_update.sh
	sudo chown root:root squashfs-root/etc/Scripts/EW_update.sh

	rm -f ew_rootfs.img
        echo "making rootfs image for ew"
        sudo mksquashfs4.2.a squashfs-root ew_rootfs.img
        if [ $? != "0" ]
        then
                echo "================================================================"
                echo "ew_rootfs.img : mksquashfs4.2.a failed !!!!!!!!!!!!!!!!!!!"
                echo "================================================================"
                exit
        fi

	sudo rm -rf squashfs-root
        sudo chmod +rw ew_rootfs.img


	cd $ROOT_DIR

}


check_signature()
{
        cd $ROOT_DIR

        echo ""
        echo "===================================================================="
        echo "Checking signature"
        echo "===================================================================="
        echo ""

        if [ $MODEL_TARGET == "HawkP_TV" ]
        then
                let TOTAL_SIG_LEN=256
        elif [ $MODEL_TARGET == "HawkM" ]
        then
                let TOTAL_SIG_LEN=256
        elif [ $MODEL_TARGET == "HAWKA" ]
        then
		if [ $RELEASE_MODE == "perf" ] || [ $RELEASE_MODE == "product" ]
		then
	                let TOTAL_SIG_LEN=256
		else
                        echo "$MODEL_TARGET - $RELEASE_MODE : Checking signature is skipped"
                        echo ""
                        return
                fi
        fi

        if [ $TOTAL_SIG_LEN == 0 ]
        then
                echo "Error : Estimated length of signature is 0"
                exit 1
        fi

        cp -f $BSP_P4_DIR/$MODEL_TARGET/image/$RELEASE_DEBUG_CHECK/uImage .
        chmod 777 uImage

        UIMAGE_LEN=$(stat -c %s uImage)
        let "UIMAGE_LEN=$UIMAGE_LEN - $TOTAL_SIG_LEN"

        truncate -s $UIMAGE_LEN uImage

        SIG_GEN_FOUND=0

        if [ $MODEL_TARGET == "HawkP_TV" ]
        then
                if [ -x ~/sig_gen ]; then SIG_GEN_FOUND=1; fi
		~/sig_gen HawkP uImage uImage.tmp
		mv -f uImage.tmp uImage

		cp -f $BSP_P4_DIR/$MODEL_TARGET/image/$RELEASE_DEBUG_CHECK/dtb.bin .
		chmod 777 dtb.bin
		~/sig_gen HawkP dtb.bin dtb.bin.tmp 504
		rm -f dtb.bin dtb.bin.tmp
        elif [ $MODEL_TARGET == "HawkM" ]
        then
                if [ -x ~/sig_gen ]; then SIG_GEN_FOUND=1; fi
                ~/sig_gen HawkM uImage uImage.tmp
		mv -f uImage.tmp uImage
                
		cp -f $BSP_P4_DIR/$MODEL_TARGET/image/$RELEASE_DEBUG_CHECK/dtb.uhd.bin .
		chmod 777 dtb.uhd.bin
		~/sig_gen HawkM dtb.uhd.bin dtb.uhd.bin.tmp 504
		rm -f dtb.uhd.bin dtb.uhd.bin.tmp

		cp -f $BSP_P4_DIR/$MODEL_TARGET/image/$RELEASE_DEBUG_CHECK/dtb.fhd.bin .
		chmod 777 dtb.fhd.bin
		~/sig_gen HawkM dtb.fhd.bin dtb.fhd.bin.tmp 504
		rm -f uImage.tmp dtb.fhd.bin dtb.fhd.bin.tmp
        elif [ $MODEL_TARGET == "HAWKA" ]
        then
		if [ $RELEASE_DEBUG_CHECK == "perf" ] || [ $RELEASE_DEBUG_CHECK == "product" ]
		then
	                if [ -x ~/sig_gen ]; then SIG_GEN_FOUND=1; fi
        	        ~/sig_gen HawkA uImage uImage.tmp
			mv -f uImage.tmp uImage
                	
			cp -f $BSP_P4_DIR/$MODEL_TARGET/image/$RELEASE_DEBUG_CHECK/dtb.bin .
			chmod 777 dtb.bin
			~/sig_gen HawkA dtb.bin dtb.bin.tmp 504
			rm -f dtb.bin dtb.bin.tmp
	        fi
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

        DIFF_RESULT=$(diff $BSP_P4_DIR/$MODEL_TARGET/image/$RELEASE_DEBUG_CHECK/uImage uImage)
        if [ "$DIFF_RESULT" != "" ]
        then
                echo "Error : Signature is incorrect"
                exit 1
        fi

        rm -f uImage

        echo ""
        echo "===================================================================="
        echo "Signature check result : OK"
        echo "===================================================================="
        echo ""
}



######################################################################################
# FUNCTION SECTION END
#####################################################################################


######################################################################################
# MAIN SCRIPT ROUTINE
######################################################################################

set_value

until [ "$TARGET_INPUT_ERR" == "$FALSE" ]
do
	TARGET_INPUT_ERR=$FALSE
	main_menu
done

until [ "$SELECT_INPUT_ERR" == "$FALSE" ]
do
	SELECT_INPUT_ERR=$FALSE
  
	if [ $CHANGE_TARGET == $FALSE ] 
	then
		set_value

		until [ "$TARGET_INPUT_ERR" == "$FALSE" ]
		do
			TARGET_INPUT_ERR=$FALSE
			main_menu
			done

	else
		check_compile_env
	fi
done

check_release_mode

until [ "$ACTION_INPUT_ERR" == "$FALSE" ]
do
	ACTION_INPUT_ERR=$FALSE
	action_menu
done

prepare_rootfs

kernel_version_change

rootfs_version_change

kernel_compile

make_rootfs_img

if [ $RELEASE_MODE == "product" ] || [ $RELEASE_MODE == "debug" ]  || [ $RELEASE_MODE == "perf" ] || [ $RELEASE_MODE == "kasan" ]
then
	backup_config_file
fi

echo "=============================================="
echo "unsquashfs4.2.a test routine"
echo "=============================================="
cd $ROOT_DIR

if [ $MODEL_TARGET == "HawkP" ]
then
	cd $ROOT_DIR
        sudo $BSP_P4_DIR/Tools/vdfs-tools/unpack.vdfs ../$MODEL_TARGET/image/"$RELEASE_MODE"/rootfs.img
        cd $BSP_P4_DIR/Tools/vdfs-tools
        make clean
else
	sudo unsquashfs4.2.a rootfs.img
fi

if [ $? != "0" ]
then
	sudo rm -rf squashfs-root
	echo "================================================================"
	echo "Test rootfs.img : unsquashfs4.2.a failed !!!!!!!!!!!!!!!!!!!!!!!!!"
	echo "================================================================"
	exit
fi

if [ $MODEL_TARGET == "HawkP" ]
then
	cd $ROOT_DIR
        sudo rm -rf vdfs_root
else
	sudo rm -rf squashfs-root
fi

release_img

if [ $MODEL_TARGET == "HawkP_TV" ] || [ $MODEL_TARGET == "HawkM" ]
then
	MakeRootFSImageEW
fi
MakeRootFSDataToCheckRelease

release_img_for_P4

check_signature

echo "Do you want to progress CLEAN process? [y/n]"
read answer

if [ $answer == "y" ]
then
	clean_env
fi

######################################################################################
# MAIN SCRIPT ROUTINE END
######################################################################################
