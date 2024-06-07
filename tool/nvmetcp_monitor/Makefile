KERNEL_SRC := /usr/src/linux-source-5.15.0

obj-m += ntm_kernel.o

all: ntm_kernel ntm_user

ntm_kernel:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) \
		EXTRA_CFLAGS=-I$(KERNEL_SRC)/block modules

ntm_user: ntm_user.c  ntm_user.h ntm_com.h blk_layer_user.h 
	gcc -std=c11 -o ntm_user ntm_user.c blk_layer_user.c -I. -lrt

clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f ntm_user
