/*
 * Multiverse server.
 * Lost count of the reincarnation count.
 *
 * Made by Folkert van Verseveld
 *
 * Copyright Folkert van Verseveld. All rights reserved.
 */
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

// sdl stuff
#include <GL/gl.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_keyboard.h>
#include <SDL2/SDL_keycode.h>

#include "dbg.h"

#define ARRAY_SIZE(a) (sizeof(a)/sizeof(a[0]))

// TODO reuse hlist.c
// TODO reuse daemon.c

typedef uint16_t block_t;
// must match sizeof(block_t)
typedef uint16_t meta_t;

#define ID_AIR 0
#define ID_STONE 1
#define ID_GRASS 2

#define ONT_SIDE_MASK 0x000f
#define ONT_TYPE_MASK 0x00f0

// TODO use for (un)marking blocks
// TODO use for unsplit
#define ONT_CELL_MASK 0x0f00

#define ONT_CELL 0x10
#define ONT_SPLIT 0x20

#define OT_CAP 1024
#define OT_RCAP 32
#define OT_SIZE 32

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

// TODO add cap, flags for resize
struct ot_pool {
	struct ot_node *nodes;
	// index to first node.
	size_t root;
	// number of nodes and total capacity.
	size_t count, cap;
	// list that keeps track of first free slots
	size_t *rpop;
	size_t rcount, rcap;
	// block count
	size_t blocks;
	// Size of root node in blocks, must be power of 2 and at least 2.
	unsigned root_size;
} ot_pool;

SDL_Window *win;
SDL_GLContext gl;

#define INIT_OT 1
#define INIT_IMG 2
#define INIT_SDL 4

unsigned init_mask = 0;

#define TITLE "Multiverse"
#define WIDTH 640
#define HEIGHT 480

#define FONT_WIDTH 144
#define FONT_HEIGHT 256

#define TERRAIN_WIDTH 256
#define TERRAIN_HEIGHT 256

#define TEX_FONT 0
#define TEX_TERRAIN 1

struct texture {
	GLuint id;
	unsigned w, h;
	const char *path;
} textures[] = {
	{(GLuint)-1, FONT_WIDTH, FONT_HEIGHT, "font.png"},
	{(GLuint)-1, TERRAIN_WIDTH, TERRAIN_HEIGHT, "terrain.png"},
};

struct player {
	// fractional position
	float pos[3];
	float rot[3];
} you;

int mouse_ignore = 1, mouse_pos[2], mouse_d[2];

#define KEY_Z_UP   1
#define KEY_Z_DOWN 2
#define KEY_Y_UP   4
#define KEY_Y_DOWN 8
#define KEY_X_UP   16
#define KEY_X_DOWN 32

static unsigned keys = 0;

int ot_init(struct ot_pool *o, size_t cap, size_t rcap, unsigned size)
{
	struct ot_node *nodes;
	size_t *rpop;

	if (cap < 8 || size < 2 || (size & (size - 1)))
		return EINVAL;

	if (!(nodes = malloc(cap * sizeof *nodes)))
		return ENOMEM;
	if (!(rpop = malloc(rcap * sizeof *rpop))) {
		free(nodes);
		return ENOMEM;
	}

	o->nodes = nodes;
	o->count = 0;
	o->cap = cap;
	o->root_size = size;

	o->rpop = rpop;
	o->rcount = 0;
	o->rcap = rcap;

	return 0;
}

void ot_free(struct ot_pool *o)
{
	free(o->rpop);
	free(o->nodes);
}

int ot_split(struct ot_pool *o, struct ot_node *n)
{
	struct ot_node *children;

	assert((n->type & ONT_TYPE_MASK) == ONT_CELL);

	// check for resize
	if (o->count >= o->cap - 8) {
		size_t maxcap, newcap;

		maxcap = SIZE_MAX / sizeof(struct ot_node);

		if (o->cap >= maxcap)
			return EOVERFLOW;

		newcap = o->cap > maxcap >> 1 ? maxcap : o->cap << 1;
		children = realloc(o->nodes, newcap * sizeof(struct ot_node));

		if (!children)
			return ENOMEM;

		o->nodes = children;
		o->cap = newcap;
	}

	children = &o->nodes[o->rcount ? o->rpop[--o->rcount] : o->count];
	o->count += 8;

	for (unsigned i = 0; i < 8; ++i) {
		children[i].parent = n;
		children[i].type = ONT_CELL | i;

		for (unsigned j = 0; j < 8; ++j)
			children[i].data.cells[j] = 0;
	}

	n->data.children = children;
	n->type = (n->type & ONT_SIDE_MASK) | ONT_SPLIT;

	return 0;
}

int ot_unsplit(struct ot_pool *o, struct ot_node *n)
{
#if 0
	struct ot_node *root = &o->nodes[o->root];

	// check for resize
	if (o->rcount == o->rcap) {
		size_t maxcap, newcap;
		size_t *data;

		maxcap = SIZE_MAX / sizeof(size_t);

		if (o->rcap >= maxcap)
			// can't resize anymore, should never happen
			return EOVERFLOW;

		newcap = o->rcap > maxcap >> 1 ? maxcap : o->rcap << 1;
		data = realloc(o->rpop, newcap * sizeof(size_t));

		if (!data)
			return 1;

		o->rpop = data;
		o->rcap = newcap;
	}

	if (root == n) {
		o->count = 0;
		o->rcount = 0;
		return 0;
	}

	assert(o->count > 8);

	for (; n->parent; n = n->parent) {
		unsigned side = n->type & ONT_SIDE_MASK;

		// ignore if the parent has more children
		if (side != (n->parent->type & ONT_SIDE_MASK))
			continue;

		dbgf("unsplit %u\n", side);
	}

	return 0;
#else
	(void)o;
	(void)n;

	dbgs("todo unsplit");
	return 0;
#endif
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
	assert(o->count);

	// ignore if position out of boundaries
	int size = (int)(o->root_size >> 1);

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

int ot_set_cell(struct ot_pool *o, int x, int y, int z, block_t id)
{
	// TODO merge blocks where are cells are set to ID_AIR
	int error;

	// ignore if position out of boundaries
	int size = (int)(o->root_size >> 1);

	if (z < -size || y < -size || x < -size || z >= size || y >= size || x >= size)
		// TODO resize
		return ERANGE;

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
		if (size == 1) {
			pos = ((z + 1) << 2) + ((y + 1) << 1) + (x + 1);
			goto put_pos;
		} else {
			while (size > 1) {
				error = ot_split(o, node);
				if (error)
					return error;

				pos = cell_get_pos(cx, cy, cz, x, y, z);
				node = &node->data.children[pos];

				cell_pos_update(&size, &cx, &cy, &cz, x, y, z);
			}

			// node has been split into size 2, now put block there
			goto put;
		}
		return 0;
	}

	// strategy: find closest node, split if bigger than 2, put block

	while (size > 1) {
		if ((node->type & ONT_TYPE_MASK) == ONT_CELL) {
			while (size > 1) {
				error = ot_split(o, node);
				if (error)
					return error;

				pos = cell_get_pos(cx, cy, cz, x, y, z);
				node = &node->data.children[pos];

				cell_pos_update(&size, &cx, &cy, &cz, x, y, z);
			}
			break;
		}

		pos = cell_get_pos(cx, cy, cz, x, y, z);
		node = &node->data.children[pos];

		cell_pos_update(&size, &cx, &cy, &cz, x, y, z);
	}

put:
	pos = cell_get_pos(cx, cy, cz, x, y, z);
put_pos:
	if (id && !node->data.cells[pos])
		++o->blocks;

	node->data.cells[pos] = id;

	if (id)
		node->type |= 0x100 << pos;
	else {
		--o->blocks;
		node->type &= ~(0x100 << pos);

		if (!(node->type & ONT_CELL_MASK))
			return ot_unsplit(o, node);
	}

	return 0;
}

static int tex_map(GLuint tex, SDL_Surface *surf)
{
	GLint internal;
	GLenum format;

	glPixelTransferi(GL_MAP_COLOR, GL_FALSE);
	glBindTexture(GL_TEXTURE_2D, tex);

	if (surf->format->palette) {
		fputs("tex_map: no color palette support\n", stderr);
		return 1;
	}

	internal = (!surf->format->Amask || surf->format->BitsPerPixel == 24) ? GL_RGB : GL_RGBA;
	format = internal;

	glTexImage2D(
		GL_TEXTURE_2D, 0,
		internal, surf->w, surf->h,
		0, format, GL_UNSIGNED_BYTE, surf->pixels
	);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	return 0;
}

static int tex_init(GLuint tex, const char *path, unsigned w, unsigned h)
{
	SDL_Surface *surf;
	int error = 1;

	surf = IMG_Load(path);
	if (!surf) {
		fprintf(stderr, "tex_init: failed to load tex %u from %s: %s\n", tex, path, IMG_GetError());
		goto fail;
	}
	if (surf->w < 0 || surf->h < 0) {
		fprintf(stderr, "tex_init: corrupt image %s\n", path);
		goto fail;
	}
	if ((unsigned)surf->w != w || (unsigned)surf->h != h) {
		fprintf(stderr,
			"tex_init: bad dimensions for %s: (%u,%u) but expected (%u,%u)\n",
			path, (unsigned)surf->w, (unsigned)surf->h, w, h
		);
		goto fail;
	}
	if ((error = tex_map(tex, surf)))
		goto fail;
	error = 0;
fail:
	if (surf)
		SDL_FreeSurface(surf);
	return error;
}

int game_init(void)
{
	int error = 1;

	GLuint tex[ARRAY_SIZE(textures)];

	glGenTextures(ARRAY_SIZE(tex), tex);

	for (size_t i = 0; i < ARRAY_SIZE(textures); ++i) {
		struct texture *t = &textures[i];

		error = tex_init(tex[i], t->path, t->w, t->h);
		if (error)
			goto fail;

		t->id = tex[i];
	}

	error = 0;
fail:
	if (error)
		glDeleteTextures(ARRAY_SIZE(tex), tex);

	return error;
}

static void gl_init(void)
{
	int w, h;

	SDL_GetWindowSize(win, &w, &h);
	glViewport(0, 0, w, h);

	glEnable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
}

static unsigned shift_qwerty(unsigned key, unsigned mod)
{
	const unsigned char numup[] = {")!@#$%^&*("};

	if (!(mod & KMOD_SHIFT) || key > 0xff)
		return key;

	if (key >= 'a' && key <= 'z')
		return key - 'a' + 'A';
	else if (key >= '0' && key <= '9')
		return numup[key - '0'];

	switch (key) {
	case '`' : return '~';
	case '-' : return '_';
	case '=' : return '+';
	case '[' : return '{';
	case ']' : return '}';
	case '\\': return '|';
	case ';' : return ':';
	case '\'': return '"';
	case ',' : return '<';
	case '.' : return '>';
	case '/' : return '?';
	}
	return key;
}

static void keydown(const SDL_Event *ev)
{
	unsigned mod, virt;

	mod = ev->key.keysym.mod;
	virt = shift_qwerty(ev->key.keysym.sym, mod);

	if (virt > 0xff) {
		switch (virt) {
		case SDLK_HOME:
			player_reset(&you);
			break;
		}
		return;
	}
	// TODO add keys
	switch (virt) {
	case 'q': keys |= KEY_Z_UP;   break;
	case 'e': keys |= KEY_Z_DOWN; break;
	case 'w': keys |= KEY_Y_UP;   break;
	case 's': keys |= KEY_Y_DOWN; break;
	case 'd': keys |= KEY_X_UP;   break;
	case 'a': keys |= KEY_X_DOWN; break;
	}
}

static void keyup(const SDL_Event *ev)
{
	unsigned mod, virt;

	mod = ev->key.keysym.mod;
	virt = shift_qwerty(ev->key.keysym.sym, mod);

	if (virt > 0xff)
		return;
	// TODO add keys
	switch (virt) {
	case 'q': keys &= ~KEY_Z_UP;   break;
	case 'e': keys &= ~KEY_Z_DOWN; break;
	case 'w': keys &= ~KEY_Y_UP;   break;
	case 's': keys &= ~KEY_Y_DOWN; break;
	case 'd': keys &= ~KEY_X_UP;   break;
	case 'a': keys &= ~KEY_X_DOWN; break;
	}
}

#define MOUSESPEED 0.01
#define MOVESPEED 0.01

void player_reset(struct player *p)
{
	p->pos[0] = p->pos[1] = p->pos[2] = 0.0f;
	p->rot[0] = p->rot[1] = p->rot[2] = 0.0f;
}

void player_move(struct player *p, unsigned ms)
{
	p->rot[0] = fmod(p->rot[0] + mouse_d[1] * MOUSESPEED * ms, 360.0);
	if (mouse_d[1] > 0) {
		if (p->rot[0] > 90.0)
			p->rot[0] = 90.0;
	} else if (mouse_d[1] < 0) {
		if (p->rot[0] < -90.0)
			p->rot[0] = -90.0;
	}
	p->rot[2] = fmod(p->rot[2] + mouse_d[0] * MOUSESPEED * ms, 360.0);
	mouse_d[1] = mouse_d[0] = 0;

	// Independent directional vectors
	int dz, dy, dx;
	dz = dy = dx = 0;

	if (keys & KEY_Z_UP) ++dz;
	if (keys & KEY_Z_DOWN) --dz;
	if (keys & KEY_Y_UP) ++dy;
	if (keys & KEY_Y_DOWN) --dy;
	if (keys & KEY_X_UP) ++dx;
	if (keys & KEY_X_DOWN) --dx;

	//dbgf("%d %d %d\n", dx, dy, dz);

	double angle = atan2(dy, dx);
	// Apply camera orientation.
	angle += p->rot[2] / 180.0 * M_PI;

	p->pos[2] += (double)dz * MOVESPEED * ms;

	if (dy || dx) {
		p->pos[0] += cos(angle) * MOVESPEED * ms;
		p->pos[1] += sin(angle) * MOVESPEED * ms;
	}
}

/* Perform game tick. */
static void tick(unsigned ms)
{
	// TODO game tick
	player_move(&you, ms);
}

static void mouse_move(SDL_Event *ev)
{
	int mx, my;

	mx = ev->motion.x;
	my = ev->motion.y;

	if (mouse_ignore) {
		mouse_ignore = 0;
		goto update;
	}
	mouse_d[0] = mx - mouse_pos[0];
	mouse_d[1] = my - mouse_pos[1];

	SDL_WarpMouseInWindow(win, WIDTH / 2, HEIGHT / 2);

update:
	mouse_pos[0] = mx;
	mouse_pos[1] = my;
}

#define CAM_FOVY 45.0
#define CAM_ZNEAR 0.05
#define CAM_ZFAR 1000.0

static void cam_perspective(GLdouble fovy, GLdouble aspect, GLdouble znear, GLdouble zfar)
{
	GLdouble fw, fh;

	fh = tan(fovy / 360.0 * M_PI) * znear;
	fw = fh * aspect;

	glFrustum(-fw, fw, -fh, fh, znear, zfar);
}

static void draw_block(unsigned size, int x, int y, int z, block_t id)
{
	if (!id)
		return;

	GLfloat x0 = (GLfloat)x, x1 = (GLfloat)x + size;
	GLfloat y0 = (GLfloat)y, y1 = (GLfloat)y + size;
	GLfloat z0 = (GLfloat)z, z1 = (GLfloat)z + size;
	unsigned idx, idy;
	idx = id % 16;
	idy = id / 16;
	GLfloat tx0 = idx / 16.0f, tx1 = (idx + 1) / 16.0f;
	GLfloat ty0 = idy / 16.0f, ty1 = (idy + 1) / 16.0f;

	glBegin(GL_QUADS);
#define pp(x,y,z,s,t) glTexCoord2f(s,t);glVertex3f(x,y,z)
// z1
	pp(x0, y0, z1, tx0, ty1);
	pp(x1, y0, z1, tx1, ty1);
	pp(x1, y1, z1, tx1, ty0);
	pp(x0, y1, z1, tx0, ty0);
// z0
	pp(x0, y1, z0, tx0, ty1);
	pp(x1, y1, z0, tx1, ty1);
	pp(x1, y0, z0, tx1, ty0);
	pp(x0, y0, z0, tx0, ty0);
// y1
	pp(x1, y1, z0, tx0, ty1);
	pp(x0, y1, z0, tx1, ty1);
	pp(x0, y1, z1, tx1, ty0);
	pp(x1, y1, z1, tx0, ty0);
// y0
	pp(x0, y0, z0, tx0, ty1);
	pp(x1, y0, z0, tx1, ty1);
	pp(x1, y0, z1, tx1, ty0);
	pp(x0, y0, z1, tx0, ty0);
// x1
	pp(x1, y0, z1, tx0, ty0);
	pp(x1, y0, z0, tx0, ty1);
	pp(x1, y1, z0, tx1, ty1);
	pp(x1, y1, z1, tx1, ty0);
// x0
	pp(x0, y1, z1, tx0, ty0);
	pp(x0, y1, z0, tx0, ty1);
	pp(x0, y0, z0, tx1, ty1);
	pp(x0, y0, z1, tx1, ty0);

	glEnd();
}

// TODO optimize
void draw_node(const struct ot_node *n, unsigned size, int x, int y, int z)
{
	unsigned hsize = size >> 1;

#ifdef DEBUG
	GLfloat f = (GLfloat)size / OT_SIZE;
	glColor3f(f, f, f);
	glBegin(GL_LINES);
	glVertex3f(x - size * .5f, y - size * .5f, z - size * .5f);
	glVertex3f(x + size * .5f, y - size * .5f, z - size * .5f);
	glVertex3f(x + size * .5f, y - size * .5f, z - size * .5f);
	glVertex3f(x + size * .5f, y + size * .5f, z - size * .5f);
	glVertex3f(x + size * .5f, y + size * .5f, z - size * .5f);
	glVertex3f(x - size * .5f, y + size * .5f, z - size * .5f);
	glVertex3f(x - size * .5f, y + size * .5f, z - size * .5f);
	glVertex3f(x - size * .5f, y - size * .5f, z - size * .5f);

	glVertex3f(x - size * .5f, y - size * .5f, z + size * .5f);
	glVertex3f(x + size * .5f, y - size * .5f, z + size * .5f);
	glVertex3f(x + size * .5f, y - size * .5f, z + size * .5f);
	glVertex3f(x + size * .5f, y + size * .5f, z + size * .5f);
	glVertex3f(x + size * .5f, y + size * .5f, z + size * .5f);
	glVertex3f(x - size * .5f, y + size * .5f, z + size * .5f);
	glVertex3f(x - size * .5f, y + size * .5f, z + size * .5f);
	glVertex3f(x - size * .5f, y - size * .5f, z + size * .5f);

	glVertex3f(x - size * .5f, y - size * .5f, z - size * .5f);
	glVertex3f(x - size * .5f, y - size * .5f, z + size * .5f);
	glVertex3f(x + size * .5f, y - size * .5f, z - size * .5f);
	glVertex3f(x + size * .5f, y - size * .5f, z + size * .5f);
	glVertex3f(x - size * .5f, y + size * .5f, z - size * .5f);
	glVertex3f(x - size * .5f, y + size * .5f, z + size * .5f);
	glVertex3f(x + size * .5f, y + size * .5f, z - size * .5f);
	glVertex3f(x + size * .5f, y + size * .5f, z + size * .5f);
	glEnd();
#endif

	switch (n->type & ONT_TYPE_MASK) {
	case ONT_CELL:
		//dbgf("cell (%d,%d,%d): size=%u\n", x, y, z, size);
		glColor3f(1, 1, 1);
		draw_block(hsize, x - hsize, y - hsize, z - hsize, n->data.cells[0]);
		draw_block(hsize, x, y - hsize, z - hsize, n->data.cells[1]);
		draw_block(hsize, x - hsize, y, z - hsize, n->data.cells[2]);
		draw_block(hsize, x, y, z - hsize, n->data.cells[3]);
		draw_block(hsize, x - hsize, y - hsize, z, n->data.cells[4]);
		draw_block(hsize, x, y - hsize, z, n->data.cells[5]);
		draw_block(hsize, x - hsize, y, z, n->data.cells[6]);
		draw_block(hsize, x, y, z, n->data.cells[7]);
		break;
	case ONT_SPLIT:
		draw_node(&n->data.children[0], size / 2, x - size / 4, y - size / 4, z - size / 4);
		draw_node(&n->data.children[1], size / 2, x + size / 4, y - size / 4, z - size / 4);
		draw_node(&n->data.children[2], size / 2, x - size / 4, y + size / 4, z - size / 4);
		draw_node(&n->data.children[3], size / 2, x + size / 4, y + size / 4, z - size / 4);
		draw_node(&n->data.children[4], size / 2, x - size / 4, y - size / 4, z + size / 4);
		draw_node(&n->data.children[5], size / 2, x + size / 4, y - size / 4, z + size / 4);
		draw_node(&n->data.children[6], size / 2, x - size / 4, y + size / 4, z + size / 4);
		draw_node(&n->data.children[7], size / 2, x + size / 4, y + size / 4, z + size / 4);
		break;
	}
}

void draw_ot(const struct ot_pool *o)
{
	if (!o->count)
		return;

	const struct ot_node *root = &o->nodes[o->root];
	draw_node(root, o->root_size, 0, 0, 0);
}

void draw_world(void)
{
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();

	cam_perspective(CAM_FOVY, (GLdouble)WIDTH / HEIGHT, CAM_ZNEAR, CAM_ZFAR);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	const struct player *p = &you;

	glRotatef(-p->rot[0] - 90, 1, 0, 0);
	glRotatef(-p->rot[2], 0, 0, 1);
	glTranslatef(-p->pos[0], -p->pos[1], -p->pos[2] - 0.5 - 1.7);

	glColor3f(1, 1, 1);

	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, textures[TEX_TERRAIN].id);

#if 0
	// TODO draw terrain
	glBegin(GL_QUADS);
	glVertex3f(2, -2, -2); glTexCoord2f(1, 1);
	glVertex3f(2, 2, -2); glTexCoord2f(1, 0);
	glVertex3f(-2, 2, -2); glTexCoord2f(0, 0);
	glVertex3f(-2, -2, -2); glTexCoord2f(0, 1);
	glEnd();
#endif

	draw_ot(&ot_pool);

	glDisable(GL_TEXTURE_2D);
}

/* Render all graphics on screen. */
static void display(void)
{
	glClearColor(0, 0.5, 0.5, 0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	draw_world();

	// TODO draw UI
}

#define DT_MAX 500

/* Main thread SDL loop. */
static int sdl_loop(void)
{
	Uint32 ticks, next;
	unsigned dt;

	gl_init();
	//SDL_ShowCursor(SDL_DISABLE);
	ticks = SDL_GetTicks();

	while (1) {
		SDL_Event ev;
		while (SDL_PollEvent(&ev)) {
			switch (ev.type) {
			case SDL_QUIT:
				goto end;
			case SDL_KEYDOWN:
				keydown(&ev);
				break;
			case SDL_KEYUP:
				keyup(&ev);
				break;
			case SDL_MOUSEMOTION:
				mouse_move(&ev);
				break;
			}
		}

		next = SDL_GetTicks();
		if (next < ticks)
			next = ticks;
		dt = next - ticks;
		if (dt > DT_MAX)
			dt = DT_MAX;
		if (dt)
			tick(dt);

		display();
		SDL_GL_SwapWindow(win);

		ticks = next;
	}
end:
	return 0;
}

int main(void)
{
	int error = 1;

	if (ot_init(&ot_pool, OT_CAP, OT_RCAP, OT_SIZE)) {
		fputs("ot_init failed\n", stderr);
		goto fail;
	}

	init_mask |= INIT_OT;

#if 0
	ot_set_cell(&ot_pool, -4, 2, -2, ID_STONE);
	ot_set_cell(&ot_pool, 3, 3, -3, ID_STONE);
	ot_set_cell(&ot_pool, 2, 3, -3, ID_GRASS);

	printf("cell 0: %u\n", ot_get_cell(&ot_pool, -4, 2, -2));
	printf("cell 1: %u\n", ot_get_cell(&ot_pool, 3, 3, -3));
	printf("cell 2: %u\n", ot_get_cell(&ot_pool, 2, 3, -3));

	printf("block count: %zu\n", ot_pool.blocks);

	ot_set_cell(&ot_pool, -4, 2, -2, ID_AIR);
#else

	for (int x = -4; x < 4; ++x) {
		ot_set_cell(&ot_pool, x, 0, -1, ID_STONE + ((x + 4) & 1));
		printf("cell %d: %u\n", x, ot_get_cell(&ot_pool, x, 0, -1));
	}

	printf("block count: %zu\n", ot_pool.blocks);

#endif

	if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)) {
		fprintf(stderr, "IMG_Init: %s\n", IMG_GetError());
		goto fail;
	}

	init_mask |= INIT_IMG;

	if (SDL_Init(SDL_INIT_VIDEO)) {
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		goto fail;
	}

	init_mask |= INIT_SDL;

	if (SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, SDL_TRUE)) {
		fprintf(stderr, "SDL: no double buffering: %s\n", SDL_GetError());
		goto fail;
	}

	if (!(win = SDL_CreateWindow(
		TITLE, SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED, WIDTH, HEIGHT,
		SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN
	)))
	{
		fprintf(stderr, "SDL: could not create window: %s\n", SDL_GetError());
		goto fail;
	}
	if (!(gl = SDL_GL_CreateContext(win))) {
		fprintf(stderr, "SDL: could not create gl context: %s\n", SDL_GetError());
		goto fail;
	}

	error = game_init();
	if (error)
		goto fail;

	error = sdl_loop();
fail:
	if (init_mask & INIT_SDL) {
		if (gl)
			SDL_GL_DeleteContext(gl);
		if (win)
			SDL_DestroyWindow(win);
		SDL_Quit();
	}

	if (init_mask & INIT_IMG)
		IMG_Quit();

	if (init_mask & INIT_OT)
		ot_free(&ot_pool);

	return error;
}
