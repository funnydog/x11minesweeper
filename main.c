#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <xcb/xcb.h>

#include <png.h>

struct image
{
	uint16_t width;
	uint16_t height;
	uint16_t stride;
	uint8_t depth;
	uint8_t data[];
};

static void my_png_warning(png_structp png, png_const_charp msg)
{
	(void)png;
	(void)msg;
}

static void my_png_error(png_structp png, png_const_charp msg)
{
	(void)msg;
	longjmp(png_jmpbuf(png), 1);
}

static struct image *loadpng(const char *path)
{
	FILE *in = fopen(path, "rb");
	if (!in)
	{
		goto err;
	}

	png_structp img = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!img)
	{
		goto err_close;
	}

	png_infop end = NULL;
	png_infop info = png_create_info_struct(img);
	if (!info)
	{
		goto err_destroy;
	}

	end = png_create_info_struct(img);
	if (!end)
	{
		goto err_destroy;
	}

	/* error handling is here */
	png_set_error_fn(img, png_jmpbuf(img), my_png_error, my_png_warning);
	if (setjmp(png_jmpbuf(img)))
	{
		goto err_destroy;
	}

	/* associate the file to the image and read the info */
	png_init_io(img, in);
	png_read_info(img, info);

	uint32_t width, height;
	int bdepth, ctype, imethod, cmethod, fmethod;
	if (!png_get_IHDR(img, info, &width, &height, &bdepth, &ctype, &imethod, &cmethod, &fmethod))
	{
		goto err_destroy;
	}

	/* colour transformations */
	switch (ctype)
	{
	case PNG_COLOR_TYPE_PALETTE:
		png_set_palette_to_rgb(img);
		goto update_alpha;

	case PNG_COLOR_TYPE_GRAY_ALPHA:
		png_set_gray_to_rgb(img);
		break;

	case PNG_COLOR_TYPE_GRAY:
		if (bdepth < 8)
		{
			png_set_expand_gray_1_2_4_to_8(img);
		}
		png_set_gray_to_rgb(img);

	update_alpha:
		if (png_get_valid(img, info, PNG_INFO_tRNS))
		{
			png_set_tRNS_to_alpha(img);
			break;
		}
		/* fall-through */
	case PNG_COLOR_TYPE_RGB:
		png_set_add_alpha(img, 0xFF, PNG_FILLER_AFTER);
		break;
	}

	/* if bdepth is 16 reduce to 8 */
	if (bdepth == 16)
	{
		png_set_strip_16(img);
	}

	/* flip the order of rgb bytes */
	png_set_bgr(img);

	/* apply the transformations */
	png_read_update_info(img, info);
	if (!png_get_IHDR(img, info, &width, &height, &bdepth, &ctype, &imethod, &cmethod, &fmethod))
	{
		goto err_destroy;
	}

	/* load the data */
	size_t rowbytes = png_get_rowbytes(img, info);
	png_bytepp rows = malloc(height * sizeof(png_bytep));
	if (!rows)
	{
		goto err_destroy;
	}

	/* create the DIB section */
	struct image *rv = calloc(1, sizeof(img) + rowbytes * height);
	if (!rv)
	{
		goto err_freemem;
	}
	rv->width = width;
	rv->height = height;
	rv->stride = rowbytes;
	rv->depth = bdepth * 3;

	/* prepare the rows addresses */
	rows[0] = rv->data;
	for (unsigned i = 1; i < height; i++)
	{
		rows[i] = rows[i-1] + rowbytes;
	}

	/* read the image */
	png_read_image(img, rows);
	png_read_end(img, end);
	png_destroy_read_struct(&img, &info, &end);
	fclose(in);

	/* premultiply by the alpha channel */
	for (unsigned y = 0; y < height; y++)
	{
		for (unsigned x = 0; x < rowbytes; x += 4)
		{
			float alpha = rows[y][x + 3] / 255.0f;
			rows[y][x + 0] = (uint8_t)(alpha * rows[y][x + 0] + 0.5f);
			rows[y][x + 1] = (uint8_t)(alpha * rows[y][x + 1] + 0.5f);
			rows[y][x + 2] = (uint8_t)(alpha * rows[y][x + 2] + 0.5f);
		}
	}

	/* cleanup */
	free(rows);
	return rv;

err_freemem:
	free(rows);
err_destroy:
	png_destroy_read_struct(&img, &info, &end);
err_close:
	fclose(in);
err:
	return NULL;
}

#define ENTITY_WIDTH  16
#define ENTITY_HEIGHT 16
#define MAP_COLUMNS   16
#define MAP_ROWS      16

enum entity_kind
{
	COVERED,
	FLAGGED,
	UNCOVERED_0,
	UNCOVERED_1,
	UNCOVERED_2,
	UNCOVERED_3,
	UNCOVERED_4,
	UNCOVERED_5,
	UNCOVERED_6,
	UNCOVERED_7,
	UNCOVERED_8,
	MINE_EXPLODED,
	MINE_CROSSED,
	MINE_IDLE,
};

struct position
{
	uint16_t x;
	uint16_t y;
};

static const struct position entity_positions[] = {
	[COVERED] = { 0, 39 },
	[FLAGGED] = { 16, 39 },
	[UNCOVERED_0] = { 0 * 16, 23 },
	[UNCOVERED_1] = { 1 * 16, 23 },
	[UNCOVERED_2] = { 2 * 16, 23 },
	[UNCOVERED_3] = { 3 * 16, 23 },
	[UNCOVERED_4] = { 4 * 16, 23 },
	[UNCOVERED_5] = { 5 * 16, 23 },
	[UNCOVERED_6] = { 6 * 16, 23 },
	[UNCOVERED_7] = { 7 * 16, 23 },
	[UNCOVERED_8] = { 8 * 16, 23 },
	[MINE_EXPLODED] = { 32, 39 },
	[MINE_CROSSED] = { 48, 39 },
	[MINE_IDLE] = { 64, 39 },
};

struct scene
{
	xcb_window_t window_id;
	xcb_gcontext_t gc_id;
	xcb_pixmap_t sprite_pixmap_id;
	enum entity_kind entities[MAP_COLUMNS * MAP_ROWS];
	int mines[MAP_COLUMNS * MAP_ROWS];
};

static void reset(struct scene *scene)
{
	for (int i=0; i < MAP_COLUMNS * MAP_ROWS; i++)
	{
		scene->entities[i] = COVERED;
	}
	for (int i=0; i < MAP_COLUMNS * MAP_ROWS; i++)
	{
		scene->mines[i] = rand() < RAND_MAX/4;
	}
}

static void render(xcb_connection_t *connection, const struct scene *scene)
{
	for (int i = 0; i < MAP_COLUMNS * MAP_ROWS; i++)
	{
		int x  = (i % MAP_COLUMNS) * ENTITY_WIDTH;
		int y = (i / MAP_COLUMNS) * ENTITY_HEIGHT;
		xcb_copy_area(
			connection,
			scene->sprite_pixmap_id,
			scene->window_id,
			scene->gc_id,
			entity_positions[scene->entities[i]].x,
			entity_positions[scene->entities[i]].y,
			x, y,
			ENTITY_WIDTH, ENTITY_HEIGHT);
	}
	xcb_flush(connection);
}

static void uncover_all(struct scene *scene)
{
	for (int i = 0; i < MAP_COLUMNS * MAP_ROWS; i++)
	{
		if (scene->mines[i])
		{
			scene->entities[i] = MINE_IDLE;
		}
		else if (scene->entities[i] == FLAGGED)
		{
			scene->entities[i] = MINE_CROSSED;
		}
	}
}

static void uncover_cells(struct scene *scene, int x, int y)
{
	int idx = y * MAP_COLUMNS + x;
	if (scene->entities[idx] != COVERED || scene->mines[idx])
	{
		return;
	}

	int minecount = 0;
	static const int dx[] = { -1, 0, 1, 1, 1, 0, -1, -1 };
	static const int dy[] = { -1, -1, -1, 0, 1, 1, 1, 0 };
	for (int i = 0; i < 8; i++)
	{
		int x1 = x + dx[i];
		int y1 = y + dy[i];
		if (0 <= x1 && x1 < MAP_COLUMNS
		    && 0 <= y1 && y1 < MAP_ROWS
		    && scene->mines[y1 * MAP_COLUMNS + x1])
		{
			minecount++;
		}
	}
	scene->entities[idx] = UNCOVERED_0 + minecount;

	/* recurse to neighbors */
	static const int nx[] = { -1, 0, 1, 0 };
	static const int ny[] = { 0, -1, 0, 1 };
	for (int i = 0; i < 4; i++)
	{
		int x1 = x + nx[i];
		int y1 = y + ny[i];
		if (0 <= x1 && x1 < MAP_COLUMNS
		    && 0 <= y1 && y1 < MAP_ROWS)
		{
			uncover_cells(scene, x1, y1);
		}
	}
}

static void on_cell_clicked(struct scene *scene, int16_t x, int16_t y)
{
	int column = x / ENTITY_WIDTH;
	int row = y / ENTITY_HEIGHT;
	int idx = row * MAP_COLUMNS + column;

	if (scene->entities[idx] == FLAGGED)
	{
		return;
	}
	else if (scene->mines[idx])
	{
                /* Lose */
		uncover_all(scene);
		scene->entities[idx] = MINE_EXPLODED;
	}
	else
	{
		uncover_cells(scene, column, row);

		/* Win. */
		int covered = 0;
		int mines = 0;
		for (int i = 0; i < MAP_COLUMNS * MAP_ROWS; i++)
		{
			covered += (scene->entities[i] == COVERED
				|| scene->entities[i] == FLAGGED);
			mines += scene->mines[i];
		}
		if (covered == mines)
		{
			uncover_all(scene);
		}
	}
}

static void on_cell_marked(struct scene *scene, int16_t x, int16_t y)
{
	int column = x / ENTITY_WIDTH;
	int row = y / ENTITY_HEIGHT;
	int idx = row * MAP_COLUMNS + column;

	if (scene->entities[idx] == FLAGGED)
	{
		scene->entities[idx] = COVERED;
	}
	else if (scene->entities[idx] == COVERED)
	{
		scene->entities[idx] = FLAGGED;
	}
}

int main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;

	xcb_connection_t *connection = xcb_connect(NULL, NULL);
	if (!connection)
	{
		fprintf(stderr, "Cannot connect to the X server\n");
		return 1;
	}

	/* get the first screen */
	const xcb_setup_t *setup = xcb_get_setup(connection);
	xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
	xcb_screen_t *screen = iter.data;

	/* create a graphic context for drawing in the foreground */
	xcb_drawable_t window_id = screen->root;
	xcb_gcontext_t gc_id = xcb_generate_id(connection);
	uint32_t mask = XCB_GC_BACKGROUND | XCB_GC_GRAPHICS_EXPOSURES;
	uint32_t values[2] = { 0x0000FF00, 0 };
	xcb_create_gc(connection, gc_id, window_id, mask, values);

	/* create the window */
	window_id = xcb_generate_id(connection);

	mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
	values[0] = 0x00ffff00; /* yellow */
	values[1] = XCB_EVENT_MASK_EXPOSURE
		| XCB_EVENT_MASK_BUTTON_PRESS
		| XCB_EVENT_MASK_BUTTON_RELEASE
		| XCB_EVENT_MASK_KEY_PRESS
		| XCB_EVENT_MASK_KEY_RELEASE;

	xcb_create_window(
		connection,           /* connection */
		XCB_COPY_FROM_PARENT, /* depth */
		window_id,            /* window id */
		screen->root,         /* parent window */
		0, 0,                 /* x, y */
		256, 256,             /* width, height */
		10,                   /* border_width */
		XCB_WINDOW_CLASS_INPUT_OUTPUT, /* class */
		screen->root_visual,           /* visual */
		mask,                          /* value mask */
		values);                       /* values */

	/* map the window on the screen */
	xcb_map_window(connection, window_id);
	xcb_flush(connection);

	/* load the image */
	struct image *img = loadpng("game-x11-sprite.png");
	if (!img)
	{
		fprintf(stderr, "Cannot load the assets\n");
		xcb_disconnect(connection);
		return 0;
	}

	/* create the pixmap on the server */
	xcb_pixmap_t pixmap_id = xcb_generate_id(connection);
	xcb_create_pixmap(
		connection,
		img->depth,
		pixmap_id,
		window_id,
		img->width,
		img->height);

	/* upload the data from the client */
	xcb_put_image(
		connection,
		XCB_IMAGE_FORMAT_Z_PIXMAP,
		pixmap_id,
		gc_id,
		img->width, img->height,
		0, 0,
		0,
		img->depth,
		img->height * img->stride,
		img->data);
	free(img);

	struct scene scene = {
		.window_id = window_id,
		.gc_id = gc_id,
		.sprite_pixmap_id = pixmap_id,
	};
	reset(&scene);

	xcb_generic_event_t *event;
	while ((event = xcb_wait_for_event(connection)))
	{
		switch (event->response_type & ~0x80)
		{
		case XCB_EXPOSE:
			render(connection, &scene);
			break;
		case XCB_KEY_RELEASE: {
			const xcb_key_release_event_t *release = (void*)event;
			if (release->detail == 36)
			{
				reset(&scene);
				render(connection, &scene);
			}
			break;
		}
		case XCB_BUTTON_RELEASE: {
			const xcb_button_release_event_t *release = (void *)event;
			switch (release->detail)
			{
			case 1:
				on_cell_clicked(&scene, release->event_x, release->event_y);
				break;
			case 3:
				on_cell_marked(&scene, release->event_x, release->event_y);
				break;
			}
			render(connection, &scene);
			break;
		}
		}
	}
	xcb_disconnect(connection);
	return 0;
}
