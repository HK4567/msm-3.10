#!/bin/bash
 
now=`date +'%Y-%m-%d %H:%M:%S'`
start_time=$(date --date="$now" +%s);
 

file=out

if [  -f "$file" ]; then
rm -r $file
fi

mkdir out

GCC=/usr/bin/arm-linux-gnueabi-gcc-4.9
LDD=/usr/bin/arm-linux-gnueabi-ld
DECONFIG=pd1510-perf_defconfig

echo ======================================================================================================
CROSS_COMPILE=/usr/bin/arm-linux-gnueabi-
echo======================================================================================================
make O=out ARCH=arm CC=${GCC} LD=${LDD}CROSS_COMPILE=${CROSS_COMPILE} ${DECONFIG}
echo ======================================================================================================
make O=out ARCH=arm CC=${GCC} CROSS_COMPILE=${CROSS_COMPILE} -j4
echo ======================================================================================================
mv out/arch/arm/boot/zImage AnyKernel3/
echo ======================================================================================================


now=`date +'%Y-%m-%d %H:%M:%S'`
end_time=$(date --date="$now" +%s);
echo "used time:"$((end_time-start_time))"s"