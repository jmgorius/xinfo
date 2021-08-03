.POSIX:
.SUFFIXES:

CC = clang
CFLAGS = -Wall -Wextra -Weverything -std=gnu99 -g

xinfo: xinfo.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -f xinfo
