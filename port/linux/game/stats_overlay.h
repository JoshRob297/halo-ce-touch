/*
STATS_OVERLAY.H

display.stats and display.stats_log: the game's frame rate counter and the
mode, map and renderer line (port/linux/game/stats_overlay.c).
*/

#ifndef STATS_OVERLAY_H
#define STATS_OVERLAY_H

/* the line on screen while display.stats is on, and one line a second in the
log while display.stats_log is on (called once a frame) */
void stats_overlay_draw(void);

/* the renderer's own text for the line and the log, read once the GL context
exists (d3d8_gl.c's gl_initialize) */
void stats_overlay_set_renderer(
	const char *vendor,
	const char *renderer);

#endif
