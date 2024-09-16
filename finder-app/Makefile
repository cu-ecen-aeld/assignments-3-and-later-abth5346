CC = gcc

ifeq ($(CROSS_COMPILE), aarch64-none-linux-gnu-)
	CC = aarch64-none-linux-gnu-gcc
endif

all: writer.c
	$(CC) writer.c -o writer

clean:
	rm -rf writer