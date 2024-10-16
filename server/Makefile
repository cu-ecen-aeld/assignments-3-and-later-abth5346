#export PATH="/home/blcthms/arm-cross-compiler/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-linux-gnu/bin:$PATH"

CC ?= gcc
CFLAGS ?= -Wall -Werror -g

all: aesdsocket

aesdsocket: aesdsocket.o  
	$(CROSS_COMPILE)$(CC) $(CFLAGS) $^ -o $@

%.o: %.c
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -c -o $@ $*.c

clean:
	rm -rf aesdsocket
	rm -rf *.o
