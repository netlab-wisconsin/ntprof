.PHONY: all clean host target cli

HOST_DIR := host
TARGET_DIR := target
CLI_DIR := cli


KERNEL_SRC := /usr/src/linux-source-5.15.0
EXTRA_CFLAGS := -I$(CURDIR)/include

all: host target cli


host:
	$(MAKE) -C $(HOST_DIR) KERNEL_SRC=$(KERNEL_SRC) EXTRA_CFLAGS="$(EXTRA_CFLAGS)"

target:
	$(MAKE) -C $(TARGET_DIR) KERNEL_SRC=$(KERNEL_SRC) EXTRA_CFLAGS="$(EXTRA_CFLAGS)"

cli:
	$(MAKE) -C $(CLI_DIR) EXTRA_CFLAGS="$(EXTRA_CFLAGS)"

clean:
	$(MAKE) -C $(HOST_DIR) clean
	$(MAKE) -C $(TARGET_DIR) clean
	$(MAKE) -C $(CLI_DIR) clean
