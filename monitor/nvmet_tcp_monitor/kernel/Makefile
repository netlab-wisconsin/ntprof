KERNEL_SRC := /usr/src/linux-source-5.15.0

obj-m += k_nttm.o

k_nttm-y := k_blk_layer.o k_nvmet_tcp_layer.o k_tcp_layer.o k_nttm_main.o

all: k_nttm

k_nttm:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) \
		EXTRA_CFLAGS="-I$(KERNEL_SRC)/block -I$(PWD)/../../include -I$(PWD)/../common" modules

clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f *.o *.ko *.mod.c *.symvers *.order
