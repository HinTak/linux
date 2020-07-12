#!/bin/bash

# 2015_ConfigDiff.sh
# 
# Diff debug vs product and product vs perf config files of targets listed in TARGET_LIST
# Modify TARGET_LIST below to add target
#    
# Usage : ./2015_ConfigDiff.sh
# Result:
#   $TARGET_NAME.DvsP.diff : Diff result of Debug vs Product config for $TARGET_NAME
#   $TARGET_NAME.PvsP.diff : Diff result of Product vs Perf config for $TARGET_NAME
#   DebugVsProduct.diff : Merged file of all $TARGET_NAME.DvsP.diff files
#   ProductVsPerf.diff : Merged file of all $TARGET_NAME.PvsP.diff files

TARGET_LIST="HawkP Hawk.P-Tizen Hawk.M-Tizen"

MERGE_FIND ()
{
	CONFIG_LINE=$1
	CONFIG_NAME=""
	# echo "==========================================================================="
	# echo "@@@CONFIG_LINE=$CONFIG_LINE"

	if [ "${CONFIG_LINE:0:1}" == "#" ]
	then
		return
	fi

	CONFIG_NAME=$(echo "$CONFIG_LINE" | cut -d'	' -f1)

	if [ "$CONFIG_NAME" = "" ]
	then
		return
	fi
	# echo "CONFIG_NAME=$CONFIG_NAME"

	if [ -e $MERGE_FILE ]
	then
		if [ "$(grep -m 1 "^$CONFIG_NAME" $MERGE_FILE)" != "" ]
		then
			# echo "$CONFIG_NAME is in $MERGE_FILE"
			return
		fi
	fi

	echo -n "$CONFIG_NAME" >> $MERGE_FILE

	for TARGET in $TARGET_LIST
	do
		if [ "$DIFF_MODE" == "DP" ]
		then
			DIFF_FILE=$TARGET.DvsP.diff
			DEFCONFIG_FILE=${TARGET}_debug_defconfig
		else
			DIFF_FILE=$TARGET.PvsP.diff
			DEFCONFIG_FILE=${TARGET}_release_defconfig
		fi

		# echo $DIFF_FILE
		CONFIG_LINE=$(grep -m 1 "$CONFIG_NAME" $DIFF_FILE)
		if [ "$CONFIG_LINE" == "" ]
		then
			CONFIG_LINE=$(grep -m 1 "$CONFIG_NAME=" $DEFCONFIG_FILE)
			if [ "$CONFIG_LINE" == "" ]
			then
				CONFIG_LINE=$(grep -m 1 "# $CONFIG_NAME is not set" $DEFCONFIG_FILE)
				if [ "$CONFIG_LINE" == "" ]
				then
					CONFIG_VAL_1="-"
					CONFIG_VAL_2="-"
				else
					CONFIG_VAL_1="N"
					CONFIG_VAL_2="N"
				fi
			else
				CONFIG_VAL_1=$(echo "$CONFIG_LINE" | cut -d'=' -f2)
				if [ "$CONFIG_VAL_1" == "y" ]
				then
					CONFIG_VAL_1="Y"
				fi
				if [ "$CONFIG_VAL_1" == "" ]
				then
					CONFIG_VAL_1="-"
				fi
				CONFIG_VAL_2=$CONFIG_VAL_1
			fi
		else
			CONFIG_VAL_1=$(echo $CONFIG_LINE | awk '{print $2}')
			CONFIG_VAL_2=$(echo $CONFIG_LINE | awk '{print $3}')
		fi
		# echo "CONFIG_VAL_1=$CONFIG_VAL_1"
		# echo "CONFIG_VAL_2=$CONFIG_VAL_2"
		if [ "$CONFIG_VAL_1" = "" ]
		then
			CONFIG_VAL_1="-"
		fi
		if [ "$CONFIG_VAL_2" = "" ]
		then
			CONFIG_VAL_2="-"
		fi
		echo -n "	$CONFIG_VAL_1	$CONFIG_VAL_2" >> $MERGE_FILE
	done
	echo "" >> $MERGE_FILE
}

MERGE()
{
	if [ "$DIFF_MODE" == "DP" ]
	then
		DIFF_FILE=$TARGET.DvsP.diff
	else
		DIFF_FILE=$TARGET.PvsP.diff
	fi

	while read CONFIG_LINE
	do
		if [ "$CONFIG_LINE" != "" ]
		then
			MERGE_FIND "$CONFIG_LINE"
		fi
	done < $DIFF_FILE
}

DIFF_FIND ()
{
	CONFIG_LINE=$1
	HEADER_STR=${CONFIG_LINE:0:1}
	IS_NOT_SET=$(echo $CONFIG_LINE | grep -m 1 "is not set")
	CONFIG_NAME=""
	# echo "==========================================================================="
	# echo "@@@CONFIG_LINE=$CONFIG_LINE"

	if [ "$HEADER_STR" == "#" ]
	then
		if [ "$IS_NOT_SET" != "" ]
		then
			CONFIG_NAME=$(echo $CONFIG_LINE | awk '{print $2}')
		fi
	else
		CONFIG_NAME=$(echo "$CONFIG_LINE" | cut -d'=' -f1)
	fi

	if [ "$CONFIG_NAME" = "" ]
	then
		return
	fi

	# echo "CONFIG_NAME=$CONFIG_NAME"

	if [ -e $CONFIG_FILE_OUT ]
	then
		if [ "$(grep -m 1 "^$CONFIG_NAME" $CONFIG_FILE_OUT)" != "" ]
		then
			# echo "$CONFIG_NAME is in $CONFIG_FILE_OUT"
			return
		fi
	fi

	CONFIG_1=$(grep -m 1 "$CONFIG_NAME=" $CONFIG_FILE_1)
	if [ "$CONFIG_1" == "" ]
	then
		CONFIG_1=$(grep -m 1 "# $CONFIG_NAME" $CONFIG_FILE_1)
	fi

	CONFIG_2=$(grep -m 1 "$CONFIG_NAME=" $CONFIG_FILE_2)
	if [ "$CONFIG_2" == "" ]
	then
		CONFIG_2=$(grep -m 1 "# $CONFIG_NAME" $CONFIG_FILE_2)
	fi

	# echo "CONFIG_1=$CONFIG_1"
	# echo "CONFIG_2=$CONFIG_2"

	if [ "$CONFIG_1" = "$CONFIG_2" ]
	then
		# echo "CONFIG_1 == CONFIG_2"
		return
	fi

	if [ "${CONFIG_1:0:1}" = "#" ]
	then
		CONFIG_VAL_1="N"
	else
		CONFIG_VAL_1=$(echo "$CONFIG_1" | cut -d'=' -f2)
		if [ "$CONFIG_VAL_1" == "y" ]
		then
			CONFIG_VAL_1="Y"
		fi

		if [ "$CONFIG_VAL_1" == "" ]
		then
			CONFIG_VAL_1="-"
		fi
	fi

	if [ "${CONFIG_2:0:1}" = "#" ]
	then
		CONFIG_VAL_2="N"
	else
		CONFIG_VAL_2=$(echo "$CONFIG_2" | cut -d'=' -f2)
		if [ "$CONFIG_VAL_2" == "y" ]
		then
			CONFIG_VAL_2="Y"
		fi

		if [ "$CONFIG_VAL_2" == "" ]
		then
			CONFIG_VAL_2="-"
		fi
	fi

	# echo "$CONFIG_NAME	$CONFIG_VAL_1	$CONFIG_VAL_2"
	echo "$CONFIG_NAME	$CONFIG_VAL_1	$CONFIG_VAL_2" >> $CONFIG_FILE_OUT
}

DIFF()
{
	if [ "$DIFF_MODE" == "DP" ]
	then
		CONFIG_FILE_1=$1_debug_defconfig
		CONFIG_FILE_2=$1_release_defconfig
		CONFIG_FILE_OUT=$1.DvsP.diff
	else
		CONFIG_FILE_1=$1_release_defconfig
		CONFIG_FILE_2=$1_perf_defconfig
		CONFIG_FILE_OUT=$1.PvsP.diff
	fi

	rm -f $CONFIG_FILE_OUT

	if [ "$DIFF_MODE" == "DP" ]
	then
		echo "# Config	Debug	Product" >> $CONFIG_FILE_OUT
	else
		echo "# Config	Product	Perf" >> $CONFIG_FILE_OUT
	fi

	while read CONFIG_LINE
	do
		if [ "$CONFIG_LINE" != "" ]
		then
			DIFF_FIND "$CONFIG_LINE"
		fi
	done < $CONFIG_FILE_1

	while read CONFIG_LINE
	do
		if [ "$CONFIG_LINE" != "" ]
		then
			DIFF_FIND "$CONFIG_LINE"
		fi
	done < $CONFIG_FILE_2
}

DIFF_RUN ()
{
	for TARGET in $TARGET_LIST
	do
		if [ -e ${TARGET}_product_defconfig ]
		then
			cp -f ${TARGET}_product_defconfig ${TARGET}_release_defconfig | exit
		fi

		echo "Diff $TARGET"
		DIFF $TARGET
	done

	rm -f $MERGE_FILE

	echo -n "Config Name	" > $MERGE_FILE
	for TARGET in $TARGET_LIST
	do
		echo -n "$TARGET		" >> $MERGE_FILE
	done
	echo "" >> $MERGE_FILE

	echo -n "	" >> $MERGE_FILE
	for TARGET in $TARGET_LIST
	do
		if [ "$DIFF_MODE" == "DP" ]
		then
			echo -n "Debug	Product	" >> $MERGE_FILE
		else
			echo -n "Product	Perf	" >> $MERGE_FILE
		fi
	done
	echo "" >> $MERGE_FILE

	echo "Merge"
	for TARGET in $TARGET_LIST
	do
		MERGE $TARGET
	done
}

echo "DebugVsProduct Start"
MERGE_FILE=DebugVsProduct.diff
DIFF_MODE=DP
DIFF_RUN

echo "ProductVsPerf Start"
MERGE_FILE=ProductVsPerf.diff
DIFF_MODE=PP
DIFF_RUN
