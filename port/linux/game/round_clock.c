/*
ROUND_CLOCK.C

The clock of the game running, told to the players where the game's own is
not: its time limit is behind the scores' button, and a player watching the
game has no other way to know how much of it is left.

The notice goes in the chat area of every machine's own players, every
minute of the last five and each of the last seconds. Each machine computes
it from the game's options and clock, which it already has, so nothing new
goes between them and a machine of another build shows it too. Displayed by
display.time_notices (off: the stock game's screens alone). Called every
frame from main.c, before the frame is drawn.
*/

#include "cseries.h"
#include <stdio.h>
#include "round_clock.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "network_chat.h"

/* the platform layer's (port/linux/src/port_config.c) */
boolean config_boolean(char const *name);

/* the seconds left the last notice named, -1 outside a game */
static long round_clock_left = -1;

void round_clock_update(
	boolean main_menu_loaded,
	real seconds)
{
	long limit;
	long left;

	(void)seconds;

	if (main_menu_loaded || !game_in_progress())
	{
		round_clock_left = -1;
		return;
	}
	if (!config_boolean("display.time_notices"))
		return;

	limit = (long)game_variant_options_get()->time_limit;
	if (limit <= 0)
	{
		round_clock_left = -1;
		return;
	}

	/* (the game's own clock: the same on every machine, the ticks each of
	them has played) */
	left = limit * 60L - game_time_get() / TICKS_PER_SECOND;
	if (left < 0)
		left = 0;
	if (left == round_clock_left)
		return;
	round_clock_left = left;

	{
		boolean say = FALSE;

		if (left == 300 || left == 600)
			say = TRUE;
		else if (left < 300 && left > 0 && (left % 60) == 0)
			say = TRUE;
		else if (left == 30 || (left <= 10 && left > 0))
			say = TRUE;

		if (say)
		{
			char text[64];

			if (left >= 60)
				snprintf(text, sizeof(text), "%ld %s left",
					left / 60, left / 60 == 1 ? "minute" : "minutes");
			else
				snprintf(text, sizeof(text), "%ld %s left",
					left, left == 1 ? "second" : "seconds");
			network_chat_add_notice(text);
		}
	}
}
