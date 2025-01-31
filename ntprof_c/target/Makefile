obj-m += ntprof_target.o
ntprof_target-y := trace_nvmet_tcp.o target_main.o

all: ntprof_target

ntprof_target:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(CURDIR) \
		EXTRA_CFLAGS="-I$(KERNEL_SRC)/block -I$(KERNEL_SRC)/include -I$(CURDIR)/../include" modules

clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(CURDIR) clean
	rm -f *.o *.ko *.mod.c *.symvers *.order