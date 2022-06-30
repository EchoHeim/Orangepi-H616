#!/bin/bash

cd ~

sudo sed -i "s/verbosity=1/verbosity=7/" /boot/orangepiEnv.txt

if [ -e "./linux-u-boot-current-orangepizero2_3.0.4_arm64.deb" ];then
    echo -e "\n **** remove uboot ****\n"
    sudo apt purge -y linux-u-boot-orangepizero2-current
    echo -e "\n **** install uboot ****\n"
    sudo dpkg -i linux-u-boot-current-orangepizero2_3.0.4_arm64.deb

    sudo nand-sata-install
fi

sudo apt install udhcpc -y

sudo mv regulatory.db* /lib/firmware/

sudo rm /lib/modules/5.13.0-sun50iw9 -fr
sudo mv ./5.13.0-sun50iw9 /lib/modules/ -f

if [ -e "./linux-dtb-current-sun50iw9_3.0.4_arm64.deb" ];then
    echo -e "\n **** remove dtb ****\n"
    sudo apt purge -y linux-dtb-current-sun50iw9
    echo -e "\n **** install dtb ****\n"
    sudo dpkg -i linux-dtb-current-sun50iw9_3.0.4_arm64.deb
fi

if [ -e "./linux-image-current-sun50iw9_3.0.4_arm64.deb" ];then
    echo -e "\n **** remove kernel ****\n"
    sudo apt purge -y linux-image-current-sun50iw9
    echo -e "\n **** install kernel ****\n"
    sudo dpkg -i linux-image-current-sun50iw9_3.0.4_arm64.deb
fi

sudo rm ./linux-*.deb

sudo reboot

