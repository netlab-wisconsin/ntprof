KERNEL_SRC := /usr/src/linux-source-5.15.0

obj-m += k_ntm.o

all: k_ntm

k_ntm:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) \
		EXTRA_CFLAGS="-I$(KERNEL_SRC)/block -I$(PWD)/../../include -I$(PWD)/../common" modules

clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f *.o *.ko *.mod.c *.symvers *.order
