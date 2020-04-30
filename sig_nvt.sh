#!/bin/bash

SIG_TOOL=~/secure/sig_nvt
SIG_KEYS=~/secure/NT14U

if [ $# -eq 0 ] || [ $# -eq 1 ]; then
    IMGPATH=${1:-"."}

    if [ -f $IMGPATH/ddr.init ]; then
        $SIG_TOOL -i $IMGPATH/ddr.init -u $SIG_KEYS/pukey_2048.txt -r $SIG_KEYS/prkey_2048.txt -o $IMGPATH/ddr.init.sec -s 20
    fi
    if [ -f $IMGPATH/dtb.bin ]; then
        $SIG_TOOL -i $IMGPATH/dtb.bin -u $SIG_KEYS/pukey_2048.txt -r $SIG_KEYS/prkey_2048.txt -o $IMGPATH/dtb.bin.sec -s 504
    fi
    if [ -f $IMGPATH/secos.bin ]; then
        $SIG_TOOL -i $IMGPATH/secos.bin -u $SIG_KEYS/pukey_2048.txt -r $SIG_KEYS/prkey_2048.txt -o $IMGPATH/secos.bin.sec -s 1016
    fi
    if [ -f $IMGPATH/seret.bin ]; then
        $SIG_TOOL -i $IMGPATH/seret.bin -u $SIG_KEYS/pukey_2048.txt -r $SIG_KEYS/prkey_2048.txt -o $IMGPATH/seret.bin.sec -s 1528
    fi
    if [ -f $IMGPATH/uImage ]; then
        $SIG_TOOL -i $IMGPATH/uImage -u $SIG_KEYS/pukey_2048.txt -r $SIG_KEYS/prkey_2048.txt -o $IMGPATH/uImage.sec -b uImage
    fi
elif [ $# -eq 2 ] || [ $# -eq 3 ]; then
    if [ -z $3 ]; then
        if [ $(echo $1 | grep Image -c) -ne 0 ]; then
            $SIG_TOOL -i $1 -u $SIG_KEYS/pukey_2048.txt -r $SIG_KEYS/prkey_2048.txt -o $2 -b uImage
        else
            $SIG_TOOL -i $1 -u $SIG_KEYS/pukey_2048.txt -r $SIG_KEYS/prkey_2048.txt -o $2
        fi
    else
        $SIG_TOOL -i $1 -u $SIG_KEYS/pukey_2048.txt -r $SIG_KEYS/prkey_2048.txt -o $2 -s $3
    fi
else
    echo "Usage:"
    echo "  $0 [image set path] - sign whole image set"
    echo "  $0 <input file> <output file> [signed size] - sign one image"
fi

