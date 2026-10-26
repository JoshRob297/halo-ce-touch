/*
SERVER_MODE.C

A dedicated server (server_mode.h; docs/dedicated_server.md): the game run
with no window, sound or player of its own (port_config.c forces those
settings for the run), hosting one game after another
(port/linux/game/server_director.c). This file is its start, before the
game's: the command line's other answers (--version, --keygen...), its
signals, its game data and settings checked before anything is bound (a
panel sees a server that cannot run stop at once, with its exit code,
rather than one that looks healthy and hosts nothing), its frames' pace
without a display to wait for, and its exits.
*/

#include "platform.h"
#include "port_config.h"
#include "posix.h"
#include "server_mode.h"

#if HALO_SERVER_BUILD

#include "posix_server.h"
#include "server_master.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef HALO_BUILD_NUMBER
#define HALO_BUILD_NUMBER 0
#endif

enum
{
	/* frames a second while no other machine is connected */
	SERVER_IDLE_FRAME_RATE = 10,
	/* a frame this late resynchronises the pace (a map loaded) */
	SERVER_RESYNCHRONISE_NANOSECONDS = 250 * 1000 * 1000,
	SERVER_MAXIMUM_EXIT_HOOKS = 8,
};

static struct
{
	int checked;
	int active;
	Uint64 started;
	/* (and in seconds since 1970) */
	unsigned long started_unix;
	/* the pace: the next frame's deadline, and whether the server is idle */
	Uint64 deadline;
	int idle;
	/* the frames' work this minute and the last: the longest and the sum */
	Uint64 frame_began;
	Uint64 minute_began;
	double minute_longest, minute_sum;
	long minute_frames;
	double last_longest, last_mean;
	void (*exit_hooks[SERVER_MAXIMUM_EXIT_HOOKS])(void);
	int exit_hook_count;
	int exiting;
} server_mode;

int halo_server_mode(void)
{
	if (!server_mode.checked)
	{
		server_mode.checked = 1;
		server_mode.active = config_server_mode_enabled();
	}
	return server_mode.active;
}

int halo_server_debug_echo(void)
{
	static int echo = -1;

	if (echo < 0)
		echo = config_boolean("server.log_debug_echo") != 0;
	return echo;
}

const char *server_mode_build(void)
{
	static char build[32];

	if (!build[0])
		snprintf(build, sizeof(build), "build-%d", (int)HALO_BUILD_NUMBER);
	return build;
}

double server_mode_uptime(void)
{
	return server_mode.started ? (double)(SDL_GetTicksNS() - server_mode.started) / 1e9 : 0.0;
}

unsigned long server_mode_started(void)
{
	return server_mode.started_unix;
}

unsigned long server_mode_process_id(void)
{
	return (unsigned long)posix_process_id();
}

/* ---------- exits */

void server_mode_at_exit(void (*hook)(void))
{
	if (server_mode.exit_hook_count < SERVER_MAXIMUM_EXIT_HOOKS)
		server_mode.exit_hooks[server_mode.exit_hook_count++] = hook;
}

void server_mode_exit(int code)
{
	if (!server_mode.exiting)
	{
		server_mode.exiting = 1;
		while (server_mode.exit_hook_count > 0)
			server_mode.exit_hooks[--server_mode.exit_hook_count]();
	}
	fflush(NULL);
	exit(code);
}

void server_mode_fatal(const char *text)
{
	static int fatal;
	const char *line = text ? text : "";

	if (fatal++)
		return;
	/* (the listings' withdrawal and the other exit hooks given a few
	seconds: a stuck one ends the process with the code all the same) */
	posix_server_quit_deadline(5, SERVER_EXIT_FATAL);
	/* (each line of it: the last ones are the error's) */
	while (*line)
	{
		const char *end = strchr(line, '\n');
		int length = end ? (int)(end - line) : (int)strlen(line);

		if (length > 0)
			platform_log("server fatal: %.*s", length, line);
		line += length + (end != NULL);
	}
	platform_log("server stopping after a fatal error (exit code %d)", SERVER_EXIT_FATAL);
	server_mode_exit(SERVER_EXIT_FATAL);
}

/* ---------- the command line */

static void print_help(void)
{
	fprintf(stdout,
		"halo [--server] [--config <file>] [--keygen] [--sign <text>] [--probe [<invite>]] [--version] [--help]\n"
		"  --server         a dedicated server (docs/dedicated_server.md)\n"
		"  --probe [<invite>]  prints the invite's game (or the LAN's first) as one line of JSON\n"
		"  --config <file>  the settings file (else config.toml beside the executable)\n"
		"  --keygen         writes a publisher key for the master servers and prints its public key\n"
		"  --sign <text>    prints the publisher key's signature of the text (an application's proof)\n"
		"  --version        prints the build and its network version\n");
}

/* the command line's arguments that are answered and end the process */
static void answer_command_line(void)
{
	char argument[1024];
	int index;

	for (index = 1; posix_command_line_argument(index, argument, sizeof(argument)); index++)
	{
		if (!strcmp(argument, "--version") || !strcmp(argument, "-v"))
		{
			fprintf(stdout, "OpenCE %s (network version %d)\n", server_mode_build(), HALO_PORT_NETWORK_VERSION);
			exit(EXIT_SUCCESS);
		}
		if (!strcmp(argument, "--help") || !strcmp(argument, "-h"))
		{
			print_help();
			exit(EXIT_SUCCESS);
		}
		if (!strcmp(argument, "--keygen"))
			exit(server_master_keygen() ? EXIT_SUCCESS : SERVER_EXIT_CONFIGURATION);
		if (!strcmp(argument, "--sign"))
		{
			char text[1024];

			if (!posix_command_line_argument(index + 1, text, sizeof(text)))
			{
				fprintf(stderr, "--sign: give the text to sign\n");
				exit(SERVER_EXIT_CONFIGURATION);
			}
			exit(server_master_sign_text(text) ? EXIT_SUCCESS : SERVER_EXIT_CONFIGURATION);
		}
	}
}

void server_mode_start(void)
{
	answer_command_line();
	if (!halo_server_mode())
		return;
	server_mode.started = SDL_GetTicksNS();
	server_mode.started_unix = posix_server_unix_seconds();
	posix_server_stdout_line_buffered();
	posix_server_signals_install();
	/* (SDL's own would turn SIGINT and SIGTERM into a quit event, which the
	server's signals do better) */
	SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
}

int server_mode_signals(int *quit_signal)
{
	return posix_server_signals_take(quit_signal);
}

/* ---------- the frames' pace */

void server_mode_set_idle(int idle)
{
	server_mode.idle = idle;
}

void server_mode_pace_frame(void)
{
	long rate = server_mode.idle ? SERVER_IDLE_FRAME_RATE : config_integer("server.frame_rate");
	Uint64 now = SDL_GetTicksNS();
	Uint64 period;
	double work;

	if (rate < 30)
		rate = 30;
	if (rate > 120)
		rate = 120;
	if (server_mode.idle)
		rate = SERVER_IDLE_FRAME_RATE;
	period = 1000000000ULL / (Uint64)rate;
	/* (the frame's work: from the end of the last wait to now) */
	if (server_mode.frame_began)
	{
		work = (double)(now - server_mode.frame_began) / 1e6;
		if (work > server_mode.minute_longest)
			server_mode.minute_longest = work;
		server_mode.minute_sum += work;
		server_mode.minute_frames++;
	}
	if (!server_mode.minute_began)
		server_mode.minute_began = now;
	if (now - server_mode.minute_began >= 60ULL * 1000000000ULL)
	{
		server_mode.last_longest = server_mode.minute_longest;
		server_mode.last_mean = server_mode.minute_frames ? server_mode.minute_sum / server_mode.minute_frames : 0.0;
		server_mode.minute_longest = server_mode.minute_sum = 0.0;
		server_mode.minute_frames = 0;
		server_mode.minute_began = now;
	}
	if (!server_mode.deadline)
		server_mode.deadline = now;
	server_mode.deadline += period;
	if (now > server_mode.deadline + SERVER_RESYNCHRONISE_NANOSECONDS)
		server_mode.deadline = now;
	else if (server_mode.deadline > now)
		SDL_DelayPrecise(server_mode.deadline - now);
	server_mode.frame_began = SDL_GetTicksNS();
}

void server_mode_frame_times(double *longest, double *mean)
{
	/* (this minute's until a whole one has gone by) */
	if (server_mode.last_longest > 0.0 || server_mode.last_mean > 0.0)
	{
		*longest = server_mode.last_longest > server_mode.minute_longest ? server_mode.last_longest :
			server_mode.minute_longest;
		*mean = server_mode.last_mean;
	}
	else
	{
		*longest = server_mode.minute_longest;
		*mean = server_mode.minute_frames ? server_mode.minute_sum / server_mode.minute_frames : 0.0;
	}
}

#else

int halo_server_mode(void)
{
	return 0;
}

int halo_server_debug_echo(void)
{
	return 0;
}

void server_mode_start(void)
{
}

int server_mode_signals(int *quit_signal)
{
	*quit_signal = 0;
	return 0;
}

void server_mode_pace_frame(void)
{
}

void server_mode_set_idle(int idle)
{
	(void)idle;
}

void server_mode_frame_times(double *longest, double *mean)
{
	*longest = *mean = 0.0;
}

void server_mode_fatal(const char *text)
{
	(void)text;
}

void server_mode_exit(int code)
{
	exit(code);
}

void server_mode_at_exit(void (*hook)(void))
{
	(void)hook;
}

const char *server_mode_build(void)
{
	return "build-0";
}

double server_mode_uptime(void)
{
	return 0.0;
}

unsigned long server_mode_started(void)
{
	return 0;
}

unsigned long server_mode_process_id(void)
{
	return 0;
}

#endif
