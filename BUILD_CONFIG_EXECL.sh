#!/bin/bash
OUTPUT_EXECL=Kernel_Config_EXCEL.csv
CONFIG_FILE_PATH=arch/arm/configs
CONFIG_TEMP_FILE_PATH=temp_config
OWNER_FILE_PATH=Config_Owner

MAX_TEAM=4
OWNER_LIST[1]=SOC
OWNER_LIST[2]=USB
OWNER_LIST[3]=WIFI
OWNER_LIST[4]=SOC_MM

MAX_PROJECT=18
PROJECT[1]=Echo.P-Tizen_debug_defconfig
PROJECT[2]=Echo.P-Tizen_perf_defconfig
PROJECT[3]=Echo.P-Tizen_release_defconfig
PROJECT[4]=Fox.P-Tizen_debug_defconfig
PROJECT[5]=Fox.P-Tizen_perf_defconfig
PROJECT[6]=Fox.P-Tizen_release_defconfig
PROJECT[7]=Golf.P-Tizen_debug_defconfig
PROJECT[8]=Golf.P-Tizen_perf_defconfig
PROJECT[9]=Golf.P-Tizen_release_defconfig
PROJECT[10]=Hawk.P-Tizen_debug_defconfig
PROJECT[11]=Hawk.P-Tizen_perf_defconfig
PROJECT[12]=Hawk.P-Tizen_release_defconfig
PROJECT[13]=Hawk.M-Tizen_debug_defconfig
PROJECT[14]=Hawk.M-Tizen_perf_defconfig
PROJECT[15]=Hawk.M-Tizen_release_defconfig
PROJECT[16]=HawkP_debug_defconfig
PROJECT[17]=HawkP_perf_defconfig
PROJECT[18]=HawkP_product_defconfig

find . -name "Kconfig*" -exec grep -H '^config' {} \; -print | grep ":" > Kconfig.txt
find . -name "Kconfig.txt" -exec perl -pi -e 's/config /CONFIG_/g' {} \;
find . -name "Kconfig.txt" -exec perl -pi -e 's/config\t/CONFIG_/g' {} \;
find . -name "Kconfig.txt" -exec perl -pi -e 's/CONFIG_ /CONFIG_/g' {} \;
find . -name "Kconfig.txt" -exec perl -pi -e 's/CONFIG_\t/CONFIG_/g' {} \;

find . -name "Kconfig.txt" -exec perl -pi -e 's/:/ /g' {} \;
cat Kconfig.txt | awk '{print $2}' | uniq  > Kconfig_list.txt
rm Kconfig.txt

rm -rf $CONFIG_TEMP_FILE_PATH
mkdir $CONFIG_TEMP_FILE_PATH

rm -f $OUTPUT_EXECL
echo "==========================================================="
echo " Run make execl file"
echo "==========================================================="

echo -n "Config, Owner, Confirm, 검토 내용 기입," > $OUTPUT_EXECL
count=1
while [ $count -le $MAX_PROJECT ]
do
	echo -n ${PROJECT[$count]}, >> $OUTPUT_EXECL
	cp $CONFIG_FILE_PATH/${PROJECT[$count]} $CONFIG_TEMP_FILE_PATH
	count=$(($count + 1))
done
echo " " >> $OUTPUT_EXECL


while read temp_config
do
	CHECK_CONFIG=$(grep $temp_config -rn $CONFIG_TEMP_FILE_PATH)
	if [ "$CHECK_CONFIG" == "" ]
	then
		continue
	fi

	#echo $temp_config
	echo -n $temp_config, >> $OUTPUT_EXECL

	count=1
        while [ $count -le $MAX_TEAM ]
        do
                LINE=$(cat $OWNER_FILE_PATH/${OWNER_LIST[$count]} | grep $temp_config)
		if [ "$LINE" != "" ]
		then
                	echo -n ${OWNER_LIST[$count]} >> $OUTPUT_EXECL
			echo -n ' ' >> $OUTPUT_EXECL
		fi
                count=$(($count + 1))
        done	
	echo -n ',' >> $OUTPUT_EXECL
	echo -n ',' >> $OUTPUT_EXECL
	echo -n ',' >> $OUTPUT_EXECL
	count=1
	while [ $count -le $MAX_PROJECT ]
	do
	        LINE=$(cat $CONFIG_FILE_PATH/${PROJECT[$count]} | grep $temp_config)
		echo -n $LINE, >> $OUTPUT_EXECL
	        count=$(($count + 1))
	done
	echo " " >> $OUTPUT_EXECL
done < Kconfig_list.txt



rm Kconfig_list.txt
rm -rf $CONFIG_TEMP_FILE_PATH

echo "==========================================================="
echo  $OUTPUT_EXECL Done
echo "==========================================================="
cd excel_convert/src
make clean
make
cd ..
chmod 777 run
./run
cd ..
rm $OUTPUT_EXECL
mv config_output.xls Kernel_Config_List.xls
