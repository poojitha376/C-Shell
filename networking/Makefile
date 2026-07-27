CC = gcc
CFLAGS = -std=c99 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -Wall -Wextra -Werror -Wno-unused-parameter -fno-asm -DOPENSSL_API_COMPAT=0x10100000L
LDFLAGS = -lcrypto

all: client server

client: client.c sham.c sham.h
	$(CC) $(CFLAGS) -DSHAM_ROLE=\"client\" client.c sham.c -o client $(LDFLAGS)

server: server.c sham.c sham.h
	$(CC) $(CFLAGS) -DSHAM_ROLE=\"server\" server.c sham.c -o server $(LDFLAGS)

clean:
	rm -f client server *.o

.PHONY: all clean
