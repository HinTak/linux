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
	
	find ./include/linux/ -name "vdlp_version.h" | xargs perl -pi -e 's/product/'$RELEASE_MODE'/g'
	find ./include/linux/ -name "vdlp_version.h" | xargs perl -pi -e 's/debug/'$RELEASE_MODE'/g'
	find ./include/linux/ -name "vdlp_version.h" | xargs perl -pi -e 's/perf/'$RELEASE_MODE'/g'

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
	if false
	then
	echo " 2. Perf mode"
	echo " 3. Product mode"
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
	else
		echo "Wrong input"
		SELECT_RELEASE_MODE_ERR=$TRUE
	fi
}

select_menuconfig_mode()
{
	echo "============================================================================"
	echo " Select menuconfig Mode..."
	echo "============================================================================"
	echo " 1. Keep going build"
	echo " 2. Change kernel .config (not recommended)"
	echo "    [CAUTION! If changing affect RFS modules, we can't guarantee working!]"
	echo "    [Because, no permission to build RFS modules.                        ]"
	echo "============================================================================"
	read mode

	if [ "$mode" == "2" ]
	then
		make menuconfig
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
			chmod +rw arch/arm/boot/dts/sdp1304-eval-uhd.dtb
			chmod +rw arch/arm/boot/dts/sdp1304-eval-fhd.dtb
			chmod +rw arch/arm/boot/dts/sdp1304-eval-us-1mp.dtb

			chmod +rw arch/arm/boot/dts/sdp1304-dtv-uhd.dtb
			chmod +rw arch/arm/boot/dts/sdp1304-dtv-fhd.dtb
			chmod +rw arch/arm/boot/dts/sdp1304-dtv-us-1mp.dtb
			chmod +rw arch/arm/boot/dts/sdp1304-dtv-fhd.prehawk.orsay.dtb

			make sdp1304-eval-uhd.dtb
			if [ $? != "0" ]
			then
				exit
			fi
			make sdp1304-eval-fhd.dtb
			if [ $? != "0" ]
			then
				exit
			fi
			make sdp1304-eval-us-1mp.dtb
			if [ $? != "0" ]
			then
				exit
			fi			

			make sdp1304-dtv-uhd.dtb
			if [ $? != "0" ]
			then
				exit
			fi
			make sdp1304-dtv-fhd.dtb
			if [ $? != "0" ]
			then
				exit
			fi
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

		elif [ $MODEL_TARGET == "HawkP" ]
                then
			cd $ROOT_DIR
			cp arch/arm/boot/uImage uImage
			cp -f arch/arm/boot/dts/sdp1404-av.dtb dtb.bin

		elif [ $MODEL_TARGET == "HawkP_TV" ]
		then
			cd $ROOT_DIR
			cp arch/arm/boot/uImage uImage.tmp
			cp -f arch/arm/boot/dts/sdp1404-dtv.dtb dtb.bin

			if [ -e ~/sig_gen ]
                        then
                                sudo ~/sig_gen HawkP uImage.tmp uImage
                                cp -f dtb.bin dtb.bin.tmp
                                sudo ~/sig_gen HawkP dtb.bin.tmp dtb.bin 504

                                sudo rm -f uImage.tmp
                                sudo rm -f dtb.bin.tmp
                        else
                                echo " There is no generator "
                                mv -f dtb.bin.tmp dtb.bin
                                exit
                        fi

		elif [ $MODEL_TARGET == "HawkM" ]
		then
			cd $ROOT_DIR
			cp arch/arm/boot/uImage uImage.tmp
			cd scripts/dtb_merge
			make clean
			make
			if [ $? -ne 0 ]
                        then
                        	echo "build dtb_merge failed. Stopped..."
                                exit
                        fi
			
			cd $ROOT_DIR
			chmod 777 $BSP_P4_DIR/$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.uhd.bin
			./scripts/dtb_merge/dtb_merge $BSP_P4_DIR/$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.uhd.bin arch/arm/boot/dts/sdp1406.uhd.2.5G.dtb arch/arm/boot/dts/sdp1406.uhd.1.5G.dtb arch/arm/boot/dts/sdp1406.uhd.2.0G.dtb
			cp -f $BSP_P4_DIR/$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.uhd.bin .

                        if [ -e ~/sig_gen ]
                        then
                                sudo ~/sig_gen HawkM uImage.tmp uImage
                                cp -f dtb.uhd.bin dtb.uhd.bin.tmp
                                sudo ~/sig_gen HawkM dtb.uhd.bin.tmp dtb.uhd.bin 504

                                sudo rm -f uImage.tmp
                                sudo rm -f dtb.uhd.bin.tmp
                        else
                                echo " There is no generator "
                                mv -f uImage.tmp uImage
                                mv -f dtb.uhd.bin.tmp dtb.uhd.bin
                                exit
                        fi

			chmod 777 $BSP_P4_DIR/$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.fhd.bin
			./scripts/dtb_merge/dtb_merge dtb.fhd.bin arch/arm/boot/dts/sdp1406.fhd.dtb arch/arm/boot/dts/sdp1406.fhd.1.5G.dtb
			cp -f $BSP_P4_DIR/$MODEL_TARGET/image/"$RELEASE_DEBUG_CHECK"/dtb.fhd.bin .

                        if [ -e ~/sig_gen ]
                        then
                                cp -f dtb.fhd.bin dtb.fhd.bin.tmp
                                sudo ~/sig_gen HawkM dtb.fhd.bin.tmp dtb.fhd.bin 504

                                sudo rm -f dtb.fhd.bin.tmp
                        else
                                echo " There is no generator "
                                mv -f dtb.fhd.bin.tmp dtb.fhd.bin
                                exit
                        fi

			cd scripts/dtb_merge
			make clean
			
			cd $ROOT_DIR

		elif [ $MODEL_TARGET == "HAWKA" ]
		then
			if [ $RELEASE_DEBUG_CHECK == "debug" ]
			then
                                cd $ROOT_DIR
                                cp arch/arm/boot/uImage uImage
                                cp -f arch/arm/boot/dts/sdp1412.dtb dtb.bin
			fi

			if [ $RELEASE_DEBUG_CHECK == "perf" ] || [ $RELEASE_DEBUG_CHECK == "product" ]
			then
				cd $ROOT_DIR
				cp arch/arm/boot/uImage uImage.tmp
				cp -f arch/arm/boot/dts/sdp1412.dtb dtb.bin

				if [ -e ~/sig_gen ]
	                        then
        	                        sudo ~/sig_gen HawkA uImage.tmp uImage
                	                cp -f dtb.bin dtb.bin.tmp
                        	        sudo ~/sig_gen HawkA dtb.bin.tmp dtb.bin 504

                                	sudo rm -f uImage.tmp
	                                sudo rm -f dtb.bin.tmp
        	                else
                	                echo " There is no generator "
                        	        mv -f dtb.bin.tmp dtb.bin
                                	exit
	                        fi
			fi
		
		else
			cd $ROOT_DIR
			cp arch/arm/boot/uImage uImage
		fi
	fi
}

prepare_kernel_modules()
{
	echo "copying modules for $PRODUCT_TARGET, $MODEL_TARGET to ../KO"
  
	rm -rf ../KO
	mkdir ../KO

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
	$TOOLCHAIN_PREFIX-strip -g  ../KO/*

}

######################################################################################
# FUNCTION SECTION END
#####################################################################################


######################################################################################
# MAIN SCRIPT ROUTINE
######################################################################################

set_value

main_menu

select_release_mode

set_compile_env

copy_config_file

select_menuconfig_mode

kernel_compile

prepare_kernel_modules
######################################################################################
# MAIN SCRIPT ROUTINE END
######################################################################################

