/*
 * Multiverse server.
 * Lost count of the reincarnation count.
 *
 * Made by Folkert van Verseveld
 *
 * Copyright Folkert van Verseveld. All rights reserved.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "dbg.h"

// TODO reuse hlist.c
// TODO reuse daemon.c

typedef uint16_t block_t;

#define ID_AIR 0
#define ID_STONE 1

#define ONT_SIDE_MASK 0x000f
#define ONT_TYPE_MASK 0x00f0

// TODO use for (un)marking blocks
// TODO use for unsplit
#define ONT_CELL_MASK 0x0f00

#define ONT_CELL 0x10
#define ONT_SPLIT 0x20

struct ot_node {
	struct ot_node *parent;
	// lower nibble indicates which child this is
	// upper nibble indicates node type
	unsigned type;
	union {
		struct ot_node *children;
		block_t cells[8];
	} data;
};

#define OT_NODES_COUNT 256

struct ot_node ot_nodes[OT_NODES_COUNT];

// TODO add cap, flags for resize
struct ot_pool {
	struct ot_node *nodes;
	size_t root, count;
	// Size of root node in blocks, must be power of 2 and at least 2.
	unsigned root_size;
} ot_pool = {
	ot_nodes, 0, 0, 256
};

void ot_split(struct ot_pool *o, struct ot_node *n)
{
	if (o->count + 8 >= OT_NODES_COUNT) {
		fputs("node overflow\n", stderr);
		exit(1);
	}

	assert((n->type & ONT_TYPE_MASK) == ONT_CELL);

	struct ot_node *children = &o->nodes[o->count];

	for (unsigned i = 0; i < 8; ++i) {
		children[i].parent = n;
		children[i].type = ONT_CELL | i;
	}

	n->data.children = children;
	n->type = (n->type & ONT_SIDE_MASK) | ONT_SPLIT;
	o->count += 8;
}

static inline unsigned cell_get_pos(int cx, int cy, int cz, int x, int y, int z)
{
	int dx, dy, dz;

	dx = x >= cx;
	dy = y >= cy;
	dz = z >= cz;

	return (dz << 2) + (dy << 1) + dx;
}

static inline void cell_pos_update(int *size, int *cx, int *cy, int *cz, int x, int y, int z)
{
	*size >>= 1;

	*cx = x >= *cx ? *cx + *size : *cx - *size;
	*cy = y >= *cy ? *cy + *size : *cy - *size;
	*cz = z >= *cz ? *cz + *size : *cz - *size;
}

block_t ot_get_cell(const struct ot_pool *o, int x, int y, int z)
{
	// ignore if position out of boundaries
	int size = (int)(o->root_size >> 1);

	assert(o->count);

	if (z < -size || y < -size || x < -size || z >= size || y >= size || x >= size)
		return ID_AIR;

	int cx = 0, cy = 0, cz = 0;
	unsigned pos;

	struct ot_node *node = &o->nodes[o->root];

	while (size > 1) {
		pos = cell_get_pos(cx, cy, cz, x, y, z);

		if ((node->type & ONT_TYPE_MASK) == ONT_CELL)
			return node->data.cells[pos];

		node = &node->data.children[pos];
		cell_pos_update(&size, &cx, &cy, &cz, x, y, z);
	}

	pos = cell_get_pos(cx, cy, cz, x, y, z);
	return node->data.cells[pos];
}

void ot_set_cell(struct ot_pool *o, int x, int y, int z, block_t id)
{
	// ignore if position out of boundaries
	int size = (int)(o->root_size >> 1);

	if (z < -size || y < -size || x < -size || z >= size || y >= size || x >= size)
		return;

	int cx = 0, cy = 0, cz = 0;
	unsigned pos;

	// ensure there's an initial node
	if (!o->count) {
		o->count = 1;
		o->nodes[o->root = 0].type = ONT_CELL;
	}

	struct ot_node *node = &o->nodes[o->root];

	// if root node is a cell, keep splitting if size > 2
	if ((node->type & ONT_TYPE_MASK) == ONT_CELL) {
		if (size == 1)
			node->data.cells[
				((z + 1) << 2) + ((y + 1) << 1) + (x + 1)
			] = id;
		else {
			while (size > 1) {
				dbgf("split: %u, (%d, %d, %d)\n", pos, cx, cy, cz);
				ot_split(o, node);

				pos = cell_get_pos(cx, cy, cz, x, y, z);
				node = &node->data.children[pos];

				cell_pos_update(&size, &cx, &cy, &cz, x, y, z);
			}

			// node has been split into size 2, now put block there
			goto put;
		}
		return;
	}

	// strategy: find closest node, split if bigger than 2, put block

	while (size > 1) {
		if ((node->type & ONT_TYPE_MASK) == ONT_CELL) {
			while (size > 1) {
				dbgf("split: %u, (%d, %d, %d)\n", pos, cx, cy, cz);
				ot_split(o, node);

				pos = cell_get_pos(cx, cy, cz, x, y, z);
				node = &node->data.children[pos];

				cell_pos_update(&size, &cx, &cy, &cz, x, y, z);
			}
			break;
		}

		pos = cell_get_pos(cx, cy, cz, x, y, z);
		node = &node->data.children[pos];

		dbgf("search: %u, (%d, %d, %d)\n", pos, cx, cy, cz);

		cell_pos_update(&size, &cx, &cy, &cz, x, y, z);
	}

put:
	pos = cell_get_pos(cx, cy, cz, x, y, z);
	dbgf("put  : %u, (%d, %d, %d)\n", pos, cx, cy, cz);
	node->data.cells[pos] = id;
}

int main(void)
{
	ot_set_cell(&ot_pool, -4, 2, -2, ID_STONE);
	ot_set_cell(&ot_pool, 3, 3, -3, ID_STONE);

	printf("cell 0: %u\n", ot_get_cell(&ot_pool, -4, 2, -2));
	printf("cell 1: %u\n", ot_get_cell(&ot_pool, 3, 3, -3));
	return 0;
}
