CFLAGS=-I/usr/include/libxml2 -Wall

all: bind9stats

bind9stats: bind9stats.c
	$(CC) $(CFLAGS) -o bind9stats bind9stats.c -lxml2
