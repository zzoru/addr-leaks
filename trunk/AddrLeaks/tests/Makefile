CC=clang
CFLAGS=-emit-llvm -g

SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.bc)

all: $(OBJS)

%.bc: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS)
