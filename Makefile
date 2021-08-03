.POSIX:
.SUFFIXES:

CC = clang
CFLAGS = -Wall -Wextra -ansi -pedantic -std=c99 -O3

xinfo: xinfo.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -f xinfo
