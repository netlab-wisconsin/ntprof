# ntprof: eBPF profiler for NVMe-over-Fabrics on unmodified kernels and
# SPDK.
#
#   make                 build ./ntprof (libbpf is built from the submodule)
#   make install         copy it to $(DESTDIR)$(PREFIX)/sbin (PREFIX=/usr/local)
#   make test            run the user-space unit tests
#   make BPFTOOL=/path   use a specific bpftool for skeleton generation
#   make V=1             show the commands
#
# Needs clang (>= 12), libelf, zlib and bpftool at build time; the binary
# needs libelf and zlib at run time.

VERSION := 2.0.0
# the commit, or the REVISION file of a copy without .git
REVISION := $(shell git describe --always --dirty 2>/dev/null || cat REVISION 2>/dev/null || echo unknown)
PREFIX ?= /usr/local

OUTPUT ?= build
CLANG ?= clang
CC ?= cc
BPFTOOL ?= $(shell command -v bpftool 2>/dev/null)
ARCH := $(shell uname -m | sed -e 's/x86_64/x86/' -e 's/aarch64/arm64/')

LIBBPF_SRC := $(abspath libbpf/src)
LIBBPF_OBJ := $(abspath $(OUTPUT)/libbpf.a)
LIBBPF_INC := $(abspath $(OUTPUT))

CFLAGS ?= -O2 -g
CFLAGS += -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter \
          -Wmissing-prototypes -Wstrict-prototypes
INCLUDES := -I$(OUTPUT) -Ilibbpf/include/uapi -Ibpf -Isrc
LDLIBS := $(LIBBPF_OBJ) -lelf -lz -lm -lpthread

# The system include directories clang -target bpf would not search.
CLANG_BPF_SYS_INCLUDES := $(shell $(CLANG) -v -E - </dev/null 2>&1 \
	| sed -n '/<...> search starts here:/,/End of search list./{ s| \(/.*\)|-idirafter \1|p }')
BPF_CFLAGS := -g -O2 -target bpf -D__TARGET_ARCH_$(ARCH) -Wall -Werror \
              -I$(LIBBPF_INC) -Ilibbpf/include/uapi -Ibpf $(CLANG_BPF_SYS_INCLUDES)

BPF_PROGS := nvme_tcp_host nvme_tcp_target nvme_rdma_host nvme_rdma_target \
             spdk_tcp_host spdk_tcp_target spdk_rdma_host spdk_rdma_target
SKELS := $(patsubst %,$(OUTPUT)/%.skel.h,$(BPF_PROGS))

APP_SRCS := src/main.c src/config.c src/record.c src/offline.c src/loader.c \
            src/trace_file.c src/model.c src/join.c src/stats.c src/report.c \
            src/spdk.c src/util.c
APP_OBJS := $(patsubst src/%.c,$(OUTPUT)/%.o,$(APP_SRCS))
# what the unit tests link against: no BPF, no libbpf
LIB_OBJS := $(addprefix $(OUTPUT)/,model.o join.o stats.o trace_file.o util.o)

TESTS := tests/test_model tests/test_join tests/test_stats tests/test_trace_file
TEST_BINS := $(patsubst tests/%,$(OUTPUT)/%,$(TESTS))

ifeq ($(V),1)
Q =
msg =
else
Q = @
msg = @printf '  %-8s %s\n' "$(1)" "$(2)";
endif

.PHONY: all clean test install check-bpftool FORCE
.DELETE_ON_ERROR:

all: ntprof

check-bpftool:
	@test -n "$(BPFTOOL)" || { echo "bpftool not found: set BPFTOOL=/path/to/bpftool" >&2; exit 1; }

$(OUTPUT) $(OUTPUT)/libbpf:
	$(Q)mkdir -p $@

$(LIBBPF_OBJ): $(wildcard $(LIBBPF_SRC)/*.[ch] $(LIBBPF_SRC)/Makefile) | $(OUTPUT)/libbpf
	$(call msg,LIB,$@)
	$(Q)$(MAKE) -C $(LIBBPF_SRC) BUILD_STATIC_ONLY=1 OBJDIR=$(dir $@)libbpf \
		DESTDIR=$(dir $@) INCLUDEDIR= LIBDIR= UAPIDIR= prefix= \
		$(if $(V),,-s) install

$(OUTPUT)/%.bpf.o: bpf/%.bpf.c $(wildcard bpf/*.h) $(LIBBPF_OBJ) | $(OUTPUT)
	$(call msg,BPF,$@)
	$(Q)$(CLANG) $(BPF_CFLAGS) -c $< -o $@
	$(Q)llvm-strip -g $@ 2>/dev/null || true

$(OUTPUT)/%.skel.h: $(OUTPUT)/%.bpf.o | check-bpftool
	$(call msg,GEN-SKEL,$@)
	$(Q)$(BPFTOOL) gen skeleton $< > $@

$(OUTPUT)/%.o: src/%.c $(wildcard src/*.h) bpf/ntprof.h $(LIBBPF_OBJ) | $(OUTPUT)
	$(call msg,CC,$@)
	$(Q)$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# The RDMA target needs atomic fetch-or (BPF ISA v3, Linux 5.12 and later).
$(OUTPUT)/nvme_rdma_target.bpf.o: BPF_CFLAGS += -mcpu=v3

# The loader includes the generated skeletons.
$(OUTPUT)/loader.o: $(SKELS)

# Rewritten only when the version or the revision changes.
$(OUTPUT)/version.h: FORCE | $(OUTPUT)
	$(Q)printf '#define NTP_VERSION "%s"\n#define NTP_REVISION "%s"\n' \
		'$(VERSION)' '$(REVISION)' > $@.tmp
	$(Q)if cmp -s $@.tmp $@; then rm -f $@.tmp; else mv $@.tmp $@; fi

$(OUTPUT)/main.o: $(OUTPUT)/version.h

ntprof: $(APP_OBJS) $(LIBBPF_OBJ)
	$(call msg,LINK,$@)
	$(Q)$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(OUTPUT)/test_%: tests/test_%.c tests/testlib.h $(LIB_OBJS)
	$(call msg,CC,$@)
	$(Q)$(CC) $(CFLAGS) $(INCLUDES) -Itests $< $(LIB_OBJS) -lm -o $@

test: $(TEST_BINS)
	$(Q)for t in $(TEST_BINS); do echo "== $$t"; $$t || exit 1; done

install: ntprof
	$(call msg,INSTALL,$(DESTDIR)$(PREFIX)/sbin/ntprof)
	$(Q)install -D -m 0755 ntprof $(DESTDIR)$(PREFIX)/sbin/ntprof

clean:
	$(call msg,CLEAN)
	$(Q)rm -rf $(OUTPUT) ntprof
