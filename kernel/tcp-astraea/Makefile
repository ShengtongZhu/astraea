bypass ?= n
rtcp ?= n
ifeq ($(rtcp), y)
	name = rtcp_astraea
else ifeq ($(bypass), y)
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

# Convenience target to build rtcp_astraea
.PHONY: rtcp
rtcp:
	$(MAKE) rtcp=y default

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