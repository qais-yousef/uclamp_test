TESTS=$(basename $(filter-out $(wildcard *.bpf.c), $(wildcard *.c)))
BPF_SKEL=$(subst .bpf.c,.skel.h, $(wildcard *.bpf.c))

CFLAGS=-O2 -Wall -g
INCLUDE=-I bpf/usr/include
LIBS=-lpthread bpf/usr/lib64/libbpf.a -lelf -lz

ARCH ?= arm64

%.bpf.o: %.bpf.c
	clang $(CFLAGS) -target bpf -D__TARGET_ARCH_$(ARCH) $(INCLUDE) -c $< -o $@
	llvm-strip -g $@

%.skel.h: %.bpf.o
	bpftool gen skeleton $< > $@

%: %.c
	$(CC) $(CFLAGS) $(INCLUDE) -c $@.c -o $@.o
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ $@.o $(LIBS)

all:
	git submodule init
	git submodule update

	make -C libbpf/src BUILD_STATIC_ONLY=1 DESTDIR=$(abspath bpf) install

	bpftool btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h

	make $(BPF_SKEL)
	make $(TESTS)

clean:
	rm -f *.o $(TESTS)
	make -C libbpf/src clean
	rm -rf bpf vmlinux.h *.skel.h
