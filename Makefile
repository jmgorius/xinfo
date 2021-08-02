.POSIX:
.SUFFIXES:

CC = clang
CFLAGS = -Wall -Wextra -std=gnu99

xinfo: xinfo.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)
