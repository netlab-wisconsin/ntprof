obj-m += ntprof_target.o

ntprof_target-y := target_main.o

target_main:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(CURDIR) \
		EXTRA_CFLAGS="$(EXTRA_CFLAGS)" modules

clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(CURDIR) clean
	rm -f *.o *.ko *.mod.c *.symvers *.order
