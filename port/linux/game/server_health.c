/*
SERVER_HEALTH.C

HTTP health check endpoint for dedicated servers (GET /health).
Provides an unauthenticated JSON health check on a dedicated TCP port.
*/

#include "server_health.h"

#if !defined(_WIN32)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <netinet/in.h>

#include <SDL3/SDL.h>

#include "cseries.h"
#include "cseries/errors.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "networking/network_game_manager.h"
#include "networking/network_game_globals.h"
#include "networking/network_server_manager.h"
#include "tag_files/tag_files.h"
#include "game/game_engine_list.h"

#define HALO_LINUX_WINSOCK_NAMES_UNDEFINE
#include "halo_linux_winsock_names.h"
#undef HALO_LINUX_WINSOCK_NAMES_UNDEFINE

int p2p_hosting_listed(void);
unsigned long system_milliseconds(void);

struct server_health_snapshot
{
	int state; /* 0: pregame, 1: in_game, 2: postgame, -1: no_server */
	char map[64];
	char variant[64];
	int players;
	int max_players;
	int minutes_left;
	boolean listed;
	unsigned long uptime_seconds;
};

static struct
{
	SDL_Thread *thread;
	SDL_Mutex *mutex;
	int listen_socket;
	SDL_AtomicInt stop_requested;
	unsigned long start_time_ms;
	struct server_health_snapshot snapshot;
} health_globals = {
	NULL,
	NULL,
	-1,
	0,
	0,
	{ -1, "", "", 0, 0, 0, FALSE, 0 }
};

static void server_health_handle_connection(int client_fd)
{
	char buffer[1024];
	ssize_t received;
	struct pollfd pfd;
	int poll_res;
	char method[16];
	char path[256];
	char response[1024];
	int response_length;
	struct server_health_snapshot snap;
	boolean head_only;

	pfd.fd = client_fd;
	pfd.events = POLLIN;
	pfd.revents = 0;

	/* 2-second receive timeout via poll */
	poll_res = poll(&pfd, 1, 2000);
	if (poll_res <= 0 || !(pfd.revents & POLLIN))
	{
		close(client_fd);
		return;
	}

	received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
	if (received <= 0)
	{
		close(client_fd);
		return;
	}
	buffer[received] = '\0';

	method[0] = '\0';
	path[0] = '\0';
	if (sscanf(buffer, "%15s %255s", method, path) != 2)
	{
		close(client_fd);
		return;
	}
	/* (HEAD sends the headers alone, its body left out, as a monitor asks) */
	head_only = strcmp(method, "HEAD") == 0;

	if ((strcmp(method, "GET") == 0 || strcmp(method, "HEAD") == 0) && strcmp(path, "/health") == 0)
	{
		char const *state_str = "no_server";
		char json_body[512];
		int body_length;

		if (health_globals.mutex)
			SDL_LockMutex(health_globals.mutex);
		snap = health_globals.snapshot;
		if (health_globals.mutex)
			SDL_UnlockMutex(health_globals.mutex);

		switch (snap.state)
		{
		case 0:
			state_str = "pregame";
			break;
		case 1:
			state_str = "in_game";
			break;
		case 2:
			state_str = "postgame";
			break;
		default:
			state_str = "no_server";
			break;
		}

		body_length = snprintf(
			json_body,
			sizeof(json_body),
			"{\"status\":\"ok\",\"state\":\"%s\",\"map\":\"%s\",\"variant\":\"%s\","
			"\"players\":%d,\"max_players\":%d,\"minutes_left\":%d,\"listed\":%s,"
			"\"uptime_seconds\":%lu}\n",
			state_str,
			snap.map,
			snap.variant,
			snap.players,
			snap.max_players,
			snap.minutes_left,
			snap.listed ? "true" : "false",
			snap.uptime_seconds);

		response_length = snprintf(
			response,
			sizeof(response),
			"HTTP/1.1 200 OK\r\n"
			"Content-Type: application/json\r\n"
			"Content-Length: %d\r\n"
			"Connection: close\r\n"
			"\r\n"
			"%s",
			body_length,
			head_only ? "" : json_body);
	}
	else
	{
		char const *error_body = "{\"error\":\"not found\"}\n";
		int body_length = (int)strlen(error_body);

		response_length = snprintf(
			response,
			sizeof(response),
			"HTTP/1.1 404 Not Found\r\n"
			"Content-Type: application/json\r\n"
			"Content-Length: %d\r\n"
			"Connection: close\r\n"
			"\r\n"
			"%s",
			body_length,
			head_only ? "" : error_body);
	}

	if (response_length > 0)
		(void)send(client_fd, response, (size_t)response_length, 0);

	close(client_fd);
}

static int server_health_thread_func(void *data)
{
	(void)data;

	while (!SDL_GetAtomicInt(&health_globals.stop_requested))
	{
		struct pollfd pfd;
		int listen_fd = health_globals.listen_socket;
		int result;

		if (listen_fd < 0)
			break;

		pfd.fd = listen_fd;
		pfd.events = POLLIN;
		pfd.revents = 0;

		/* 1-second timeout so stop flag is noticed promptly */
		result = poll(&pfd, 1, 1000);
		if (result > 0 && (pfd.revents & POLLIN))
		{
			struct sockaddr_in client_addr;
			socklen_t client_len = sizeof(client_addr);
			int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);

			if (client_fd >= 0)
				server_health_handle_connection(client_fd);
		}
		else if (result < 0 && errno != EINTR)
		{
			break;
		}
	}

	return 0;
}

void server_health_start(short port)
{
	int listen_fd;
	int opt = 1;
	struct sockaddr_in addr;

	if (port <= 0)
		return;

	if (health_globals.thread != NULL || health_globals.listen_socket >= 0)
		server_health_stop();

	SDL_SetAtomicInt(&health_globals.stop_requested, 0);
	health_globals.start_time_ms = system_milliseconds();

	if (!health_globals.mutex)
		health_globals.mutex = SDL_CreateMutex();

	listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_fd < 0)
	{
		platform_log("server_health: failed to create socket: %s", strerror(errno));
		return;
	}

	setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (const void *)&opt, sizeof(opt));

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons((unsigned short)port);

	if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
	{
		platform_log("server_health: failed to bind port %d: %s", (int)port, strerror(errno));
		close(listen_fd);
		return;
	}

	if (listen(listen_fd, 8) < 0)
	{
		platform_log("server_health: failed to listen on port %d: %s", (int)port, strerror(errno));
		close(listen_fd);
		return;
	}

	health_globals.listen_socket = listen_fd;
	health_globals.thread = SDL_CreateThread(server_health_thread_func, "server_health", NULL);
	if (!health_globals.thread)
	{
		platform_log("server_health: failed to create thread");
		close(listen_fd);
		health_globals.listen_socket = -1;
		return;
	}

	platform_log("server_health: listening on port %d", (int)port);
}

void server_health_publish(void)
{
	struct network_game_server *server;
	struct server_health_snapshot snap;
	unsigned long now;

	now = system_milliseconds();
	memset(&snap, 0, sizeof(snap));

	server = global_network_game_server_get();
	if (server)
	{
		struct network_game *game;
		long time_limit;
		long played;
		char const *map_name;
		size_t v_idx;

		snap.state = (int)network_game_server_get_state(server, NULL);
		game = network_game_server_get_game(server);

		if (game)
		{
			map_name = tag_name_strip_path(game->map.name);
			snprintf(snap.map, sizeof(snap.map), "%s", map_name ? map_name : "");

			for (v_idx = 0;
				 v_idx < sizeof(snap.variant) - 1 &&
				 v_idx < sizeof(game->variant.human_readable_game_description) / sizeof(wchar_t) &&
				 game->variant.human_readable_game_description[v_idx];
				 v_idx++)
			{
				snap.variant[v_idx] = (char)(unsigned char)game->variant.human_readable_game_description[v_idx];
			}
			snap.variant[v_idx] = '\0';

			if (!snap.variant[0] &&
				game->variant.game_engine_index >= first_usable_game_engine_index &&
				game->variant.game_engine_index <= last_usable_game_engine_index &&
				game_engines[game->variant.game_engine_index] &&
				game_engines[game->variant.game_engine_index]->name)
			{
				snprintf(snap.variant, sizeof(snap.variant), "%s", game_engines[game->variant.game_engine_index]->name);
			}

			snap.players = (int)game->player_count;
			snap.max_players = (int)game->maximum_players;

			time_limit = (long)game->variant_options.time_limit;
			played = game_time_get() / TICKS_PER_SECOND;
			snap.minutes_left = (int)(time_limit > 0 ? (time_limit * 60L - played + 59) / 60 : 0);
			if (snap.minutes_left < 0)
				snap.minutes_left = 0;
		}

		snap.listed = p2p_hosting_listed() != 0;
	}
	else
	{
		snap.state = -1;
		snap.map[0] = '\0';
		snap.variant[0] = '\0';
		snap.players = 0;
		snap.max_players = 0;
		snap.minutes_left = 0;
		snap.listed = FALSE;
	}

	if (health_globals.start_time_ms > 0 && now >= health_globals.start_time_ms)
		snap.uptime_seconds = (now - health_globals.start_time_ms) / 1000;
	else
		snap.uptime_seconds = 0;

	if (health_globals.mutex)
		SDL_LockMutex(health_globals.mutex);
	health_globals.snapshot = snap;
	if (health_globals.mutex)
		SDL_UnlockMutex(health_globals.mutex);
}

void server_health_stop(void)
{
	/* (the flag first, then a shutdown to wake the thread's poll: the fd is
	closed only once the thread has left, or a new socket of the game's could
	take its number while the thread still polls it) */
	SDL_SetAtomicInt(&health_globals.stop_requested, 1);

	if (health_globals.listen_socket >= 0)
		shutdown(health_globals.listen_socket, SHUT_RDWR);

	if (health_globals.thread)
	{
		SDL_WaitThread(health_globals.thread, NULL);
		health_globals.thread = NULL;
	}

	if (health_globals.listen_socket >= 0)
	{
		close(health_globals.listen_socket);
		health_globals.listen_socket = -1;
	}

	if (health_globals.mutex)
	{
		SDL_DestroyMutex(health_globals.mutex);
		health_globals.mutex = NULL;
	}

	health_globals.start_time_ms = 0;
	SDL_SetAtomicInt(&health_globals.stop_requested, 0);
}

#else /* _WIN32 */

void server_health_start(short port)
{
	(void)port;
}

void server_health_publish(void)
{
}

void server_health_stop(void)
{
}

#endif /* !_WIN32 */
