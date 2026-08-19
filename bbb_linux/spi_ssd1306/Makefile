KERNEL_SRC := /home/viet/work/bbb/kernelbuildscripts/KERNEL
CROSS_COMPILE := /home/viet/work/bbb/kernelbuildscripts/dl/gcc-14.4.0-nolibc/arm-linux-gnueabi/bin/arm-linux-gnueabi-
ARCH := arm

obj-m := ssd1306.o

all:
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KERNEL_SRC) M=$(PWD) modules

clean:
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KERNEL_SRC) M=$(PWD) clean