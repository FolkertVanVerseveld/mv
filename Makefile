.PHONY: default clean

CC?=gcc
CFLAGS=-g -DDEBUG -Wall -Wextra -pedantic -std=gnu99 $(shell pkg-config --cflags xtcommon)
LDLIBS=$(shell pkg-config --libs xtcommon gl sdl2) -lSDL2_image

default: server

server: server.c

clean:
	rm -f server *.o
