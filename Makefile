.PHONY: default clean

CC?=gcc
CFLAGS=-g -DDEBUG -Wall -Wextra -pedantic -std=gnu99 $(shell pkg-config --cflags xtcommon)
LDFLAGS=$(shell pkg-config --libs xtcommon)

default: server

server: server.c

clean:
	rm -f server *.o
