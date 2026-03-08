################################################################################
#
#  build_kernel_config.sh
#
#  Copyright (c) 2016-2022 Amazon.com, Inc. or its affiliates. All Rights Reserved.
#
################################################################################

KERNEL_SUBPATH="kernel/brcm/bcm-97xxx/linux-4.1"
DEFCONFIG_NAME="brcmstb_defconfig"
TARGET_ARCH="arm"
TOOLCHAIN_REPO="https://android.googlesource.com/platform/prebuilts/gcc/linux-x86/arm/arm-eabi-4.8"
TOOLCHAIN_PREFIX="arm-eabi-"

# Expected image files are seperated with ":"
KERNEL_IMAGES="arch/arm/boot/Image:arch/arm/boot/zImage"
