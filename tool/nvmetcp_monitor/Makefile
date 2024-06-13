KERNEL_SRC := /usr/src/linux-source-5.15.0

obj-m += k_ntm.o

all: k_ntm u_ntm

k_ntm:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) \
		EXTRA_CFLAGS=-I$(KERNEL_SRC)/block modules

u_ntm: u_ntm.c  u_ntm.h ntm_com.h u_blk_layer.h 
	gcc -std=c11 -o u_ntm u_ntm.c u_blk_layer.c -I. -lrt

clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f u_ntm
