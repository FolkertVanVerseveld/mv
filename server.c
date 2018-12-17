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

// TODO reuse daemon.c

typedef uint16_t block_t;

#define ID_AIR 0
#define ID_STONE 1

#define ONT_SIDE_MASK 0x0f
#define ONT_TYPE_MASK 0xf0

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
	ot_nodes, 0, 0, 8
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

block_t ot_get_cell(const struct ot_pool *o, int x, int y, int z)
{
	// ignore if position out of boundaries
	int size = (int)(o->root_size >> 1);

	if (!o->count)
		return ID_AIR;

	if (z < -size || y < -size || x < -size)
		return ID_AIR;
	if (z >= size || y >= size || x >= size)
		return ID_AIR;

	int cx, cy, cz;
	unsigned dx, dy, dz, pos;

	cx = cy = cz = 0;

	struct ot_node *node = &o->nodes[o->root];

	while (size > 1) {
		dx = x >= cx ? 1 : 0;
		dy = y >= cy ? 1 : 0;
		dz = z >= cz ? 1 : 0;

		pos = (dz << 2) + (dy << 1) + dx;

		if ((node->type & ONT_TYPE_MASK) == ONT_CELL)
			return node->data.cells[pos];

		node = &node->data.children[pos];

		size >>= 1;

		cx = x >= cx ? cx + size : cx - size;
		cy = y >= cy ? cy + size : cy - size;
		cz = z >= cz ? cz + size : cz - size;
	}

	dx = x >= cx ? 1 : 0;
	dy = y >= cy ? 1 : 0;
	dz = z >= cz ? 1 : 0;

	pos = (dz << 2) + (dy << 1) + dx;

	return node->data.cells[pos];
}

void ot_set_cell(struct ot_pool *o, int x, int y, int z, block_t id)
{
	// ignore if position out of boundaries
	int size = (int)(o->root_size >> 1);

	if (z < -size || y < -size || x < -size)
		return;
	if (z >= size || y >= size || x >= size)
		return;

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
			int cx, cy, cz;
			unsigned dx, dy, dz, pos;

			cx = cy = cz = 0;

			while (size > 1) {
				dx = x >= cx ? 1 : 0;
				dy = y >= cy ? 1 : 0;
				dz = z >= cz ? 1 : 0;

				pos = (dz << 2) + (dy << 1) + dx;

				dbgf("split: %u, %d, %d, %d (%d, %d, %d)\n", pos, dx, dy, dz, cx, cy, cz);
				ot_split(o, node);

				node = &node->data.children[pos];

				size >>= 1;

				cx = x >= cx ? cx + size : cx - size;
				cy = y >= cy ? cy + size : cy - size;
				cz = z >= cz ? cz + size : cz - size;
			}

			// node has been split into size 2, now put block there

			dx = x >= cx ? 1 : 0;
			dy = y >= cy ? 1 : 0;
			dz = z >= cz ? 1 : 0;

			pos = (dz << 2) + (dy << 1) + dx;

			dbgf("put  : %u, %d, %d, %d (%d, %d, %d)\n", pos, dx, dy, dz, cx, cy, cz);

			node->data.cells[pos] = id;
		}
		return;
	}

	// strategy: find closest node, split if bigger than 2, put block
	int cx, cy, cz;
	unsigned dx, dy, dz, pos;

	cx = cy = cz = 0;

	while (size > 1) {
		if ((node->type & ONT_TYPE_MASK) == ONT_CELL) {
			while (size > 1) {
				dx = x >= cx ? 1 : 0;
				dy = y >= cy ? 1 : 0;
				dz = z >= cz ? 1 : 0;

				pos = (dz << 2) + (dy << 1) + dx;

				dbgf("split: %u, %d, %d, %d (%d, %d, %d)\n", pos, dx, dy, dz, cx, cy, cz);
				ot_split(o, node);

				node = &node->data.children[pos];

				size >>= 1;

				cx = x >= cx ? cx + size : cx - size;
				cy = y >= cy ? cy + size : cy - size;
				cz = z >= cz ? cz + size : cz - size;
			}
			break;
		}

		dx = x >= cx ? 1 : 0;
		dy = y >= cy ? 1 : 0;
		dz = z >= cz ? 1 : 0;

		pos = (dz << 2) + (dy << 1) + dx;

		node = &node->data.children[pos];

		dbgf("search: %u, %d, %d, %d (%d, %d, %d)\n", pos, dx, dy, dz, cx, cy, cz);

		size >>= 1;

		cx = x >= cx ? cx + size : cx - size;
		cy = y >= cy ? cy + size : cy - size;
		cz = z >= cz ? cz + size : cz - size;
	}

	dx = x >= cx ? 1 : 0;
	dy = y >= cy ? 1 : 0;
	dz = z >= cz ? 1 : 0;

	pos = (dz << 2) + (dy << 1) + dx;

	dbgf("put  : %u, %d, %d, %d (%d, %d, %d)\n", pos, dx, dy, dz, cx, cy, cz);

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
