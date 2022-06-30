#!/bin/sh

IFNAME=$1
CMD=$2

echo `basename "$0"` $@

if [ $CMD = "SSH-KEYGEN" ]; then
    KEY_FILE_PATH=$3
    ssh-keygen -N "" -t rsa -f $KEY_FILE_PATH
fi

if [ $CMD = "SSH-COPY-ID" ]; then
    USER=$3
    IPSTR=$4
    PUBKEY=$5
    echo ssh-copy-id -i $PUBKEY $USER@$IPSTR
    ssh-copy-id -i $PUBKEY $USER@$IPSTR
    echo done
fi

if [ $CMD = "SCP-FETCH" ]; then
    USER=$3
    PRIV_KEY_FILE=$4
    IPSTR=$5
    RFILE=$6
    LFILE=$7
    FTYPE=$8
    if [ $FTYPE = "folder" ]; then
	echo scp -i $PRIV_KEY_FILE -r $USER@$IPSTR:$RFILE $LFILE
	scp -i $PRIV_KEY_FILE -r $USER@$IPSTR:$RFILE $LFILE
    fi
    if [ $FTYPE = "file" ]; then
	echo scp -i $PRIV_KEY_FILE $USER@$IPSTR:$RFILE $LFILE
	scp -i $PRIV_KEY_FILE $USER@$IPSTR:$RFILE $LFILE
    fi

    echo done
fi

if [ $CMD = "disabled-START-WPAS" ]; then
    sudo rfkill unblock wifi
    sudo ifconfig $IFNAME up
    sudo ./wpa_supplicant -Dnl80211 -i$IFNAME -c/etc/wpa_supplicant.conf -dd -f/tmp/wpa_supplicant-$IFNAME &
    echo wpa_s started
fi

if [ $CMD = "disabled-STOP-WPAS" ]; then
    sudo rm -rf /var/run/wpa_supplicant
    PID=`pgrep -f wpa_supplicant.*-i$IFNAME`
    if [ ! -z "$PID" ]; then
	sudo kill $PID
	echo kill wpa_s with pid $PID
	#sudo kill $(pgrep -f wpa_supplicant.*-i$IFNAME)
    fi
fi

if [ $CMD = "disabled-RESTART-IF" ]; then
    sudo rfkill unblock wifi
    sudo ifconfig $IFNAME down
    sudo ifconfig $IFNAME up
    echo $IFNAME restarted

fi
