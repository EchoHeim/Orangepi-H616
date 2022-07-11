#!/bin/bash    

source /etc/system.cfg     # 加载配置文件

function startWifi_ap() {
    sudo kill -9 $(pidof hostapd)
    sudo kill -9 $(pidof udhcpd)
    sudo kill -9 $(pidof udhcpc)

    # hostapd启动中常用到的参数说明
        # -h   显示帮助信息
        # -d   显示更多的debug信息 (-dd 获取更多)
        # -B   将hostapd程序运行在后台
        # -g   全局控制接口路径，这个工hostapd_cli使用，一般为/var/run/hostapd
        # -G   控制接口组
        # -P   PID 文件
        # -K   调试信息中包含关键数据
        # -t   调试信息中包含时间戳
        # -v   显示hostapd的版本
    
    sudo hostapd /etc/soft_ap/hostapd.conf -B   # 指定配置文件，-B将程序放到后台运行
    sudo ifconfig wlan0 192.168.2.1
    sudo udhcpd /etc/soft_ap/udhcpd.conf &
}

function startWifi_sta() {
    sudo kill -9 $(pidof hostapd)
    sudo kill -9 $(pidof udhcpd)
    sudo kill -9 $(pidof udhcpc)
    
    sudo nmcli dev wifi connect $WIFI_SSID password $WIFI_PASSWD ifname $wlan
    sudo udhcpc -i $wlan
}


