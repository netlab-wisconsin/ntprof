obj-m += ntprof_host.o
ntprof_host-y := config.o trace_blk.o trace_nvme_tcp.o host.o analyzer.o host_main.o

all: ntprof_host

ntprof_host:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(CURDIR) \
		EXTRA_CFLAGS="-I$(KERNEL_SRC)/block -I$(KERNEL_SRC)/include -I$(CURDIR)/../include" modules

clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(CURDIR) clean
	rm -f *.o *.ko *.mod.c *.symvers *.order
