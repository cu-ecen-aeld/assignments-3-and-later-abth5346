CC ?= gcc
CFLAGS ?= -Wall -Werror -g

all: aesdsocket

aesdsocket: aesdsocket.o
	$(CROSS_COMPILE)$(CC) $(CFLAGS) $^ -o $@

%.o: %.c
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -c -o $@ $*.c

clean:
	rm -rf aesdsocket
