#!/bin/bash

export KERNEL=kernel7

function build () {
    make -j8 ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- zImage modules dtbs
}

function update () {
    sudo mount /dev/$11 mnt/fat32
    sudo mount /dev/$12 mnt/ext4
    sudo env PATH=$PATH make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- INSTALL_MOD_PATH=mnt/ext4 modules_install
    sudo cp arch/arm/boot/zImage mnt/fat32/$KERNEL.img
    sudo cp arch/arm/boot/dts/*.dtb mnt/fat32/
    sudo cp arch/arm/boot/dts/overlays/*.dtb* mnt/fat32/overlays/
    sudo cp arch/arm/boot/dts/overlays/README mnt/fat32/overlays/
    sudo umount mnt/fat32
    sudo umount mnt/ext4
}

function install () {
    sudo mount /dev/$11 mnt/fat32
    sudo mount /dev/$12 mnt/ext4
    sudo env PATH=$PATH make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- INSTALL_MOD_PATH=mnt/ext4 modules_install
    sudo cp mnt/fat32/$KERNEL.img mnt/fat32/$KERNEL-backup.img
	sudo cp arch/arm/boot/zImage mnt/fat32/$KERNEL.img
    sudo cp arch/arm/boot/dts/*.dtb mnt/fat32/
    sudo cp arch/arm/boot/dts/overlays/*.dtb* mnt/fat32/overlays/
    sudo cp arch/arm/boot/dts/overlays/README mnt/fat32/overlays/
    sudo umount mnt/fat32
    sudo umount mnt/ext4
}