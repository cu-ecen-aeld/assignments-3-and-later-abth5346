#export PATH="/home/blcthms/arm-cross-compiler/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-linux-gnu/bin:$PATH"

CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -Wall -Werror
LDFLAGS ?= -lpthread -lrt

all: aesdsocket

aesdsocket: aesdsocket.o  
	$(CC) $(CFLAGS) $(LDFLAGS) -pthread -o $@ $<

clean:
	rm -rf aesdsocket

