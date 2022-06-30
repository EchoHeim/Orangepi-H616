filename = yt_phy_module

obj-m = $(filename).o

KSRC = /lib/modules/`uname -r`/build

KDST = /lib/modules/`uname -r`/kernel/drivers/net/phy/

KFILE = $(KDST)$(filename).ko

all:
	make -C $(KSRC) M=$(PWD) modules

clean:
	make -C $(KSRC) M=$(PWD) clean
	
install:
	sudo install $(filename).ko $(KDST)
	cd /lib/modules/`uname -r`/ && sudo depmod -a
uninstall:
	sudo rm $(KFILE)
