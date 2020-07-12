#!/bin/sh

FILE_DIR=$1
FILE_NAME=$2
FILE=$1/$2
CODE_ENTRY_ADDR=$3
HEADER_SIZE=$4
CHIP_NAME=$5
IVT_SIZE=32

echo "File        : $FILE"
echo "Header size : $HEADER_SIZE"
echo "Chip Name   : $CHIP_NAME"

# Security Server Command #######################################
enc_sign_cookie="http://168.219.243.151/secos/secosbin.do"

if [ $# -ne 5 ];then
  echo "#usage: $0 <file_dir> <file_name> <base addr(HEX)> <header size(DEC)> <chip name(ex:iMX8MM)>"
  echo "        header size : code entry - load address. (secos.bin/secos_drv.bin : 64, other cases : 0)"
  exit
fi

echo "extend $FILE_NAME to 4KB aligned size ..."
if [ -f $FILE ]
then
	FILESIZE=$(wc -c "$FILE" | awk '{print $1}')
	echo -n "file size: 0x"
	echo "obase=16; ibase=10; $FILESIZE" | bc
else
	echo "$FILE : No such file or directory!"
	exit
fi

REMAINDER=`expr $FILESIZE % 4096`
if [ $REMAINDER -ne 0 ]; then 
	PADDEDSIZE=`expr $FILESIZE - $REMAINDER + 4096`
	objcopy -I binary -O binary --pad-to $PADDEDSIZE --gap-fill=0x5A $FILE $FILE.pad
	echo -n "extend $FILE to 0x"
	echo "obase=16; ibase=10; $PADDEDSIZE" | bc
	
else
	PADDEDSIZE=$FILESIZE
	cp -f $FILE $FILE.pad
	echo "$FILE is already 4KB aligned."
fi

echo "generate IVT ..."
CODE_ENTRY_ADDR_TEMP=`echo $CODE_ENTRY_ADDR | sed 's/..//'`
CODE_ENTRY_ADDR_DEC=`echo "obase=10; ibase=16; $CODE_ENTRY_ADDR_TEMP" | bc`

SELF_POINTER=0x`echo "obase=16; ibase=10; ($CODE_ENTRY_ADDR_DEC + $PADDEDSIZE - $HEADER_SIZE)" | bc`
CSF_POINTER=0x`echo "obase=16; ibase=10; ($CODE_ENTRY_ADDR_DEC + $PADDEDSIZE - $HEADER_SIZE + $IVT_SIZE)" | bc`

echo "Code entry addr : $CODE_ENTRY_ADDR"
echo "Self Pointer : $SELF_POINTER"
echo "CSF Pointer : $CSF_POINTER"

./genIVT $CODE_ENTRY_ADDR $SELF_POINTER $CSF_POINTER

echo "attach IVT ..."
cat $FILE.pad ivt.bin > $FILE.pad.ivt

echo "generate CSF data ..."
BIN_LOAD_ADDR=0x`echo "obase=16; ibase=10; ($CODE_ENTRY_ADDR_DEC - $HEADER_SIZE)" | bc`
SIGN_DATA_SIZE=0x`echo "obase=16; ibase=10; ($PADDEDSIZE + $IVT_SIZE)" | bc`
sed -e "s/CHIP_NAME/$CHIP_NAME/g" -e "s/LOAD_ADDR/$BIN_LOAD_ADDR/g" -e "s/DATA_SIZE/$SIGN_DATA_SIZE/g" -e "s/FILE_PAD_IVT/$FILE_NAME.pad.ivt/g" raw.csf.template > $FILE.csf

# This tool execution will be replaced with curl communiction to security server
#../linux64/bin/cst --o $FILE.csf.bin --i $FILE.csf
hash=$(openssl sha1 $FILE.pad.ivt | cut -d " " -f 2)
sign_hash=$(curl -b ~/cookies.txt -F "file=@$FILE.pad.ivt" -F "file_csf=@$FILE.csf" -F "hash=$hash" -F "target=$CHIP_NAME" -F "mode=sign"  -o $FILE.csf.bin $enc_sign_cookie -D - | grep hash: | sed 's/hash://' | tr -d " \r")
echo "curl -b ~/cookies.txt -F "file=@$FILE.pad.ivt" -F "file_csf=@$FILE.csf" -F "hash=$hash" -F "target=$CHIP_NAME" -F "mode=sign"  -o $FILE.csf.bin $enc_sign_cookie -D - | grep hash: | sed 's/hash://' | tr -d " \r""
sign_hashgenerate=$(openssl sha1 $FILE.csf.bin | cut -d " " -f 2)
echo "Sign_hashgen: [$sign_hashgenerate]"
echo "Sign_hash : [$sign_hash]"

if [ -s $FILE.csf.bin ];then
	echo Sign file exists.
	if [ "$sign_hashgenerate" = "$sign_hash" ]; then
		echo Hash same
	else
		echo "========================================================"
		echo " Security Server error Report"
		echo "========================================================"
		cat $FILE.csf.bin
		echo ""
		echo "========================================================"
		exit
	fi
else
	echo Sign file does not exist.
	exit
fi


echo "extend csf data to 0x200 ..."
objcopy -I binary -O binary --pad-to 0x2000 --gap-fill=0x5A $FILE.csf.bin $FILE.csf.bin.pad

echo "attach csf data ..."
cat $FILE.pad.ivt $FILE.csf.bin.pad > $FILE_DIR/signed_$FILE_NAME

echo "rename $FILE_NAME to raw_$FILE_NAME and signed_$FILE_NAME to $FILE_NAME"
mv $FILE_DIR/$FILE_NAME $FILE_DIR/raw_$FILE_NAME
mv $FILE_DIR/signed_$FILE_NAME $FILE_DIR/$FILE_NAME

rm -f $FILE.pad $FILE.pad.ivt $FILE.csf $FILE.csf.bin $FILE.csf.bin.pad ivt.bin

echo "signed $FILE_NAME is ready"
