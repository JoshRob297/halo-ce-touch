/*
STATS_OVERLAY.C

The frame rate counter and the game's mode, in a corner of the screen
(display.stats, off by default): "Slayer | adreno" and, drawn by the game's
own counter, the frames per second.

The counter is the game's (main_framerate_render, display_framerate): this
turns it on from the settings and adds the line above it, which names the
engine and the GPU profile and driver the renderer chose
(gpu_profile.c), so a screenshot of a bug says which of them was running.
*/

#include "cseries.h"
#include "game/game_engine.h"
#include "interface/interface.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "text/draw_string.h"
#include "text/font_group.h"
/* port/linux/src/gpu_profile.c */
char const *gpu_profile_name(void);
char const *gpu_profile_driver(void);
#include "stats_overlay.h"

#include <stdio.h>
#include <string.h>

/* port_config.c's */
int config_boolean(const char *name);
unsigned long config_changes(void);

/* the game's own counter (main.c) */
extern boolean display_framerate;

static boolean stats_overlay_enabled(void)
{
	static unsigned long changes = (unsigned long)-1;
	static boolean enabled = FALSE;

	if (changes != config_changes())
	{
		changes = config_changes();
		enabled = config_boolean("display.stats") != 0;
	}
	return enabled;
}

static char const *stats_overlay_mode(void)
{
	struct game_variant const *variant = game_engine_get_variant();

	if (!variant)
		return "menus";
	switch (variant->game_engine_index)
	{
	case game_engine_ctf: return "CTF";
	case game_engine_slayer: return "Slayer";
	case game_engine_oddball: return "Oddball";
	case game_engine_king: return "King";
	case game_engine_race: return "Race";
	case game_engine_terminator: return "Juggernaut";
	default: return "Game";
	}
}

void stats_overlay_draw(void)
{
	long font_index;
	struct font_header *font;
	short line_height;
	short top, left;
	char text[96];
	char const *driver;
	real_argb_color color;
	rectangle2d bounds;

	if (!stats_overlay_enabled())
		return;
	/* the game's counter, which draws the number at the bottom right */
	display_framerate = TRUE;
	if (!game_in_progress() || !game_engine_in_play())
		return;
	font_index = interface_get_tag_index(_interface_font_terminal);
	if (font_index == NONE)
		return;
	font = font_definition_get(font_index);
	line_height = (short)(font->ascending_height + font->descending_height + font->leading_height);
	if (line_height <= 0)
		return;
	driver = gpu_profile_driver();
	if (!driver || !strcmp(driver, "native"))
		snprintf(text, sizeof(text), "%s | %s", stats_overlay_mode(), gpu_profile_name());
	else
		snprintf(text, sizeof(text), "%s | %s %s", stats_overlay_mode(), gpu_profile_name(), driver);
	bounds = render.camera.window_bounds;
	offset_rectangle2d(&bounds, (short)-render.camera.viewport_bounds.x0, (short)-render.camera.viewport_bounds.y0);
	left = (short)(bounds.x0 + 12);
	top = (short)(bounds.y0 + 12);
	bounds.x0 = left;
	bounds.x1 = (short)(bounds.x1 - 12);
	bounds.y0 = top;
	bounds.y1 = (short)(top + line_height);
	color.alpha = 0.75f;
	color.red = color.green = color.blue = 0.92f;
	draw_string_set_draw_mode(font_index, NONE, 0, 0, &color);
	rasterizer_draw_string(&bounds, NULL, NULL, 0, text);
}
