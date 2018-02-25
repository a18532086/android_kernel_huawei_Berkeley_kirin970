#!/bin/bash
export CONFIG_FILE="merge_kirin970_defconfig"
export TOOL_CHAIN_PATH="/home/a18532086/aarch64-linux-android-4.9/bin/aarch64-linux-android-"
export objdir="../out"
compile() {
  mkdir ../out
  make O=$objdir ARCH=arm64 CROSS_COMPILE=$TOOL_CHAIN_PATH $CONFIG_FILE -j4 
  make O=$objdir ARCH=arm64 CROSS_COMPILE=$TOOL_CHAIN_PATH -j8
}
module(){
  cd ../out
  mkdir modules
  find . -name '*.ko' -exec cp -av {} modules/ \;
  # strip modules 
  # ${TOOL_CHAIN_PATH}strip --strip-unneeded modules/*
  #mkdir modules/qca_cld
  #mv modules/wlan.ko modules/qca_cld/qca_cld_wlan.ko
}
dtbuild(){
  ./tools/dtbToolCM -2 -o $objdir/arch/arm64/boot/dt.img -s 2048 -p $objdir/scripts/dtc/ $objdir/arch/arm64/boot/dts/
}
compile
module
#dtbuild
#cp $objdir/arch/arm64/boot/zImage $sourcedir/zImage
#cp $objdir/arch/arm64/boot/dt.img.lz4 $sourcedir/dt.img
