#!/bin/bash

##############################################################################
# set envs
ROOT_DIR=$PWD
##############################################################################

rm -f user_headers_install.tgz
export VD_KERNEL_HEADERS_PATH=$ROOT_DIR/vd_kernel_header/


rm -rf $ROOT_DIR/vd_kernel_header/
mkdir -p $ROOT_DIR/vd_kernel_header/

make ARCH=arm INSTALL_HDR_PATH=$VD_KERNEL_HEADERS_PATH headers_install
rm -f `find $VD_KERNEL_HEADERS_PATH -name "*.cmd"`
rm -f `find $VD_KERNEL_HEADERS_PATH -name "*.install"`
cd $ROOT_DIR/vd_kernel_header/
tar zcvf user_headers_install.tgz include
mv user_headers_install.tgz $ROOT_DIR/
rm -rf $ROOT_DIR/vd_kernel_header/
cd -

##############################################################################
# Image
echo "##################################################################"
ls -alh user_headers_install.tgz
echo "Done"
echo "##################################################################"
