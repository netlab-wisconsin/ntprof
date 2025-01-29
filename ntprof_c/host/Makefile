obj-m += ntprof_host.o

ntprof_host-y := config.o host_main.o

host_main:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(CURDIR) \
		EXTRA_CFLAGS="$(EXTRA_CFLAGS)" modules

clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(CURDIR) clean
	rm -f *.o *.ko *.mod.c *.symvers *.order
