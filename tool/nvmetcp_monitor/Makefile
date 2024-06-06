KERNEL_SRC := /usr/src/linux-source-5.15.0

obj-m += nvmetcp_monitor.o

all: nvmetcp_monitor nvmetcp_monitor_user

nvmetcp_monitor:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) \
		EXTRA_CFLAGS=-I$(KERNEL_SRC)/block modules

nvmetcp_monitor_user: nvmetcp_monitor_user.c nvmetcp_monitor.h
	gcc -std=c11 -o nvmetcp_monitor_user nvmetcp_monitor_user.c -I. -lrt

clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f nvmetcp_monitor_user
