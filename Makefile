TESTS=$(basename $(wildcard *.c))

CFLAGS=-Werror -Wall -g
INCLUDE=-I bpf/usr/include
LIBS=-lpthread bpf/usr/lib64/libbpf.a -lelf -lz

ARCH ?= arm64

%: %.bpf.c
	clang $(CFLAGS) -target bpf -D__TARGET_ARCH_$(ARCH) $(INCLUDE) -c $< -o $@.bpf.o
	llvm-strip -g $@.bpf.o
	bpftool gen skeleton $@.bpf.o > $@.skel.h

%: %.c
	$(CC) $(CFLAGS) $(INCLUDE) -c $@.c -o $@.o
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ $@.o $(LIBS)

all:
	git submodule init
	git submodule update

	make -C libbpf/src BUILD_STATIC_ONLY=1 DESTDIR=$(abspath bpf) install

	bpftool btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h

	make $(TESTS)

clean:
	rm -f *.o $(TESTS)
	make -C libbpf/src clean
	rm -rf bpf vmlinux.h *.skel.h
