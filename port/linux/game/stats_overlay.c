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
#include "cache/cache_files.h"
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
char const *config_string(const char *name);
unsigned long config_changes(void);
/* the port's log, logcat on Android (platform.h) */
void platform_log(const char *format, ...);
/* the engine's clock (source/cseries/cseries_windows.c) */
unsigned long system_milliseconds(void);

/* the log's window: a line a second, with the frames it held */
#define STATS_OVERLAY_LOG_MILLISECONDS 1000

/* the game's own counter (main.c) */
extern boolean display_framerate;

static boolean stats_overlay_setting(const char *name, unsigned long *changes, boolean *enabled)
{
	if (*changes != config_changes())
	{
		*changes = config_changes();
		*enabled = config_boolean(name) != 0;
	}
	return *enabled;
}

static boolean stats_overlay_enabled(void)
{
	static unsigned long changes = (unsigned long)-1;
	static boolean enabled = FALSE;

	return stats_overlay_setting("display.stats", &changes, &enabled);
}

static boolean stats_overlay_log_enabled(void)
{
	static unsigned long changes = (unsigned long)-1;
	static boolean enabled = FALSE;

	return stats_overlay_setting("display.stats_log", &changes, &enabled);
}

static char const *stats_overlay_map(void);
static char const *stats_overlay_mode(void);

static char const *stats_overlay_map(void)
{
	char const *map = cache_file_loaded_map_name();

	return (map && map[0]) ? map : "-";
}

/* the frames in a window of milliseconds, a second's by the log's */
static float stats_overlay_fps(unsigned long elapsed_milliseconds, unsigned long frames)
{
	return elapsed_milliseconds ? (float)frames * 1000.0f / (float)elapsed_milliseconds : 0.0f;
}

static void stats_overlay_log_frame(void)
{
	static unsigned long window_start;
	static unsigned long frames;
	unsigned long now = system_milliseconds();
	unsigned long elapsed;
	char const *driver = gpu_profile_driver();
	char const *cache = config_string("display.geometry_cache");
	rectangle2d bounds = render.camera.window_bounds;

	frames++;
	if (!window_start)
	{
		window_start = now;
		return;
	}
	elapsed = now - window_start;
	if (elapsed < STATS_OVERLAY_LOG_MILLISECONDS)
		return;
	platform_log("STATS fps=%.1f mode=%s map=%s gpu=%s drv=%s cache=%s res=%dx%d",
		stats_overlay_fps(elapsed, frames), stats_overlay_mode(), stats_overlay_map(),
		gpu_profile_name(), (driver && driver[0]) ? driver : "native",
		(cache && cache[0]) ? cache : "auto", (int)(bounds.x1 - bounds.x0), (int)(bounds.y1 - bounds.y0));
	window_start = now;
	frames = 0;
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

	if (stats_overlay_enabled())
	{
		/* the game's counter, which draws the number at the bottom right */
		display_framerate = TRUE;
	}
	if (stats_overlay_log_enabled())
		stats_overlay_log_frame();
	if (!stats_overlay_enabled())
		return;
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
