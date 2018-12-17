#ifndef DBG_H
#define DBG_H

#include <assert.h>

#ifdef DEBUG

#include <stdio.h>

#define dbgs(s) puts(s)
#define dbgf(f, ...) printf(f, ## __VA_ARGS__)

#else

#define dbgs(s) ((void)0)
#define dbgf(f, ...) ((void)0)

#endif

#endif
