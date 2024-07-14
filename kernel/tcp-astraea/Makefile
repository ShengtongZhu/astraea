bypass ?= n
ifeq ($(bypass), y)
	name = tcp_astraea_bypass
else
	name = tcp_astraea
endif


ifneq ($(KERNELRELEASE),)

# kbuild part of makefile
obj-m := $(name).o

else
# normal makefile

KDIR ?= /lib/modules/`uname -r`/build

default:
	$(MAKE) -C $(KDIR) M=$$PWD

install:
	sudo insmod $(name).ko

uninstall:
	sudo rmmod $(name).ko

reinstall: default
	-sudo rmmod $(name).ko
	sudo insmod $(name).ko
	
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
endif