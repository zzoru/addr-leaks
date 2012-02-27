CC=llvm-gcc
CFLAGS=-emit-llvm -I/usr/include/i386-linux-gnu/ -g

SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.bc)

all: $(OBJS)

%.bc: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS)
