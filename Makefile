# FIXME: Need a real build system

all: squashfuse

clean:
	rm -rf squashfuse *.o *.inc *.dSYM

.PHONY: all clean

ARCH = 

FUSE_FLAGS = -D__FreeBSD__=10 -D_FILE_OFFSET_BITS=64 -D__DARWIN_64_BIT_INO_T=1
CFLAGS = $(ARCH) -g -O0 -Wall
LDFLAGS = $(ARCH)

LIBFILES = dir.o file.o fs.o swap.o table.o ll.o
LIBADD = -lz -lfuse_ino64

squashfuse: squashfuse.o $(LIBFILES)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBADD)

%.o: %.c $(wildcard *.h) swap.h.inc
	$(CC) $(CFLAGS) $(FUSE_FLAGS) -c -o $@ $<

swap.h.inc: squashfs_fs.h gen_swap.rb
	./gen_swap.rb