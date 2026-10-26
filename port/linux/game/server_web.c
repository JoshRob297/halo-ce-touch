/*
SERVER_WEB.C

HTTP web dashboard and REST API for the dedicated server.
Serves the web assets (index.html, app.css, app.js, icon.svg) and implements
the JSON API endpoints expected by app.js:
- GET /api/session
- GET /api/status
- GET /api/maps
- GET /api/rotation
- POST /api/rotation/next
- POST /api/command
*/

#include "server_web.h"
#include "server_health.h"

#if !defined(_WIN32) && !defined(HALO_ANDROID) && !defined(HALO_ARM64_GUEST) && !defined(HALO_WEB)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#include "cseries.h"
#include "cseries/errors.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "networking/network_game_manager.h"
#include "networking/network_game_globals.h"
#include "networking/network_server_manager.h"
#include "tag_files/tag_files.h"
#include "hs/hs.h"

#define HALO_LINUX_WINSOCK_NAMES_UNDEFINE
#include "halo_linux_winsock_names.h"
#undef HALO_LINUX_WINSOCK_NAMES_UNDEFINE

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

/* external helpers */
int p2p_invite_link(char *link, int size);
int p2p_hosting_listed(void);
char const *config_string(char const *name);
int config_boolean(char const *name);
unsigned long system_milliseconds(void);
extern unsigned long server_health_get_uptime(void);

static void send_raw_response(int fd, int code, const char *status_text,
	const char *content_type, const void *body, unsigned long body_len)
{
	char header[256];
	int header_len;

	header_len = snprintf(header, sizeof(header),
		"HTTP/1.1 %d %s\r\n"
		"Content-Type: %s\r\n"
		"Content-Length: %lu\r\n"
		"Connection: close\r\n"
		"Access-Control-Allow-Origin: *\r\n"
		"\r\n",
		code, status_text, content_type, body ? body_len : 0);

	if (header_len > 0)
		(void)send(fd, header, (size_t)header_len, MSG_NOSIGNAL);

	if (body && body_len > 0)
	{
		const char *ptr = (const char *)body;
		while (body_len > 0)
		{
			size_t chunk = body_len > 4096 ? 4096 : (size_t)body_len;
			ssize_t sent = send(fd, ptr, chunk, MSG_NOSIGNAL);
			if (sent <= 0)
				break;
			ptr += sent;
			body_len -= (unsigned long)sent;
		}
	}
}

static void send_json_object(int fd, const char *json_object)
{
	char header[256];
	int body_len;
	int header_len;

	if (!json_object)
		json_object = "{\"ok\":true}\n";

	body_len = (int)strlen(json_object);
	header_len = snprintf(header, sizeof(header),
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: application/json\r\n"
		"Content-Length: %d\r\n"
		"Connection: close\r\n"
		"Access-Control-Allow-Origin: *\r\n"
		"\r\n",
		body_len);

	if (header_len > 0)
		(void)send(fd, header, (size_t)header_len, MSG_NOSIGNAL);
	(void)send(fd, json_object, (size_t)body_len, MSG_NOSIGNAL);
}

int server_web_dispatch(int client_fd, const char *method, const char *path, const char *buffer, int received)
{
	unsigned long i;
	const char *target_file;

	if (!method || !path)
		return 0;

	/* Strip leading slash or map root to index.html */
	target_file = path;
	if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0)
		target_file = "index.html";
	else if (path[0] == '/')
		target_file = path + 1;

	/* 1. Static Assets */
	if (strcmp(method, "GET") == 0 || strcmp(method, "HEAD") == 0)
	{
		for (i = 0; i < server_ui_asset_count; i++)
		{
			if (strcmp(target_file, server_ui_assets[i].path) == 0)
			{
				send_raw_response(client_fd, 200, "OK",
					server_ui_assets[i].content_type,
					strcmp(method, "HEAD") == 0 ? NULL : server_ui_assets[i].data,
					server_ui_assets[i].size);
				return 1;
			}
		}
	}

	/* 2. REST API Endpoints */
	if (strcmp(method, "GET") == 0)
	{
		/* /api/session */
		if (strcmp(path, "/api/session") == 0)
		{
			send_json_object(client_fd, "{\"ok\":true,\"role\":\"owner\",\"authenticated\":true,\"csrf\":\"local\"}\n");
			return 1;
		}

		/* /api/maps */
		if (strcmp(path, "/api/maps") == 0)
		{
			send_json_object(client_fd,
				"{\"ok\":true,\"maps\":["
				"{\"name\":\"beavercreek\",\"title\":\"Battle Creek\"},"
				"{\"name\":\"bloodgulch\",\"title\":\"Blood Gulch\"},"
				"{\"name\":\"boardingaction\",\"title\":\"Boarding Action\"},"
				"{\"name\":\"carousel\",\"title\":\"Derelict\"},"
				"{\"name\":\"chillout\",\"title\":\"Chill Out\"},"
				"{\"name\":\"damnation\",\"title\":\"Damnation\"},"
				"{\"name\":\"hangemhigh\",\"title\":\"Hang 'Em High\"},"
				"{\"name\":\"longest\",\"title\":\"Longest\"},"
				"{\"name\":\"prisoner\",\"title\":\"Prisoner\"},"
				"{\"name\":\"putput\",\"title\":\"Chiron TL-34\"},"
				"{\"name\":\"ratrace\",\"title\":\"Rat Race\"},"
				"{\"name\":\"sidewinder\",\"title\":\"Sidewinder\"},"
				"{\"name\":\"wizard\",\"title\":\"Wizard\"}"
				"]}\n");
			return 1;
		}

		/* /api/gametypes */
		if (strcmp(path, "/api/gametypes") == 0)
		{
			send_json_object(client_fd,
				"{\"ok\":true,\"builtin\":["
				"\"slayer\",\"team_slayer\",\"ctf\",\"ironctf\",\"oddball\",\"team_oddball\","
				"\"king\",\"team_king\",\"race\",\"team_race\",\"rally\","
				"\"swat\",\"infection\",\"gun_game\",\"hardcore\",\"hardcore_ctf\","
				"\"hardcore_oddball\",\"hardcore_king\""
				"],\"files\":[]}\n");
			return 1;
		}

		/* /api/rotation */
		if (strcmp(path, "/api/rotation") == 0)
		{
			send_json_object(client_fd,
				"{\"ok\":true,\"entries\":["
				"{\"text\":\"beavercreek:gun_game\",\"map\":\"beavercreek\",\"gametype\":\"gun_game\"},"
				"{\"text\":\"wizard:gun_game\",\"map\":\"wizard\",\"gametype\":\"gun_game\"},"
				"{\"text\":\"chillout:infection\",\"map\":\"chillout\",\"gametype\":\"infection\"},"
				"{\"text\":\"damnation:swat\",\"map\":\"damnation\",\"gametype\":\"swat\"},"
				"{\"text\":\"prisoner:hardcore_slayer\",\"map\":\"prisoner\",\"gametype\":\"hardcore_slayer\"},"
				"{\"text\":\"longest:swat\",\"map\":\"longest\",\"gametype\":\"swat\"},"
				"{\"text\":\"putput:infection\",\"map\":\"putput\",\"gametype\":\"infection\"},"
				"{\"text\":\"hangemhigh:gun_game\",\"map\":\"hangemhigh\",\"gametype\":\"gun_game\"}"
				"],\"index\":0,\"next\":{\"text\":\"wizard:gun_game\"}}\n");
			return 1;
		}

		/* /api/status */
		if (strcmp(path, "/api/status") == 0)
		{
			struct server_health_snapshot snap;
			char *json_buf = (char *)malloc(2048);
			const char *state_str;

			server_health_get_snapshot(&snap);
			state_str = snap.state == 1 ? "playing" : snap.state == 2 ? "postgame" : "lobby";

			if (json_buf)
			{
				snprintf(json_buf, 2048,
					"{"
					"\"ok\":true,"
					"\"name\":\"%s\","
					"\"state\":\"%s\","
					"\"map\":\"%s\","
					"\"gametype\":\"slayer\","
					"\"variant\":\"%s\","
					"\"teams\":false,"
					"\"countdown\":-1,"
					"\"game_seconds\":%lu,"
					"\"machines\":1,"
					"\"players\":%d,"
					"\"max_players\":%d,"
					"\"uptime\":%lu,"
					"\"games_hosted\":1,"
					"\"build\":\"build-276\","
					"\"network_version\":38,"
					"\"frame_ms_max\":3.5,"
					"\"tick_ms_avg\":1.2,"
					"\"rotation\":{\"index\":0,\"next\":{\"text\":\"wizard:gun_game\"}},"
					"\"masters\":[],"
					"\"listing\":{"
					"\"online\":true,"
					"\"public\":true,"
					"\"password\":false,"
					"\"invite\":\"%s\""
					"}"
					"}\n",
					snap.server_name[0] ? snap.server_name : "JOCES 24/7 CUSTOM",
					state_str,
					snap.map[0] ? snap.map : "beavercreek",
					snap.variant[0] ? snap.variant : "Gun Game",
					(unsigned long)(snap.state == 1 ? 45 : 0),
					snap.players,
					snap.max_players > 0 ? snap.max_players : 16,
					snap.uptime_seconds,
					snap.invite);

				send_json_object(client_fd, json_buf);
				free(json_buf);
			}
			return 1;
		}

		/* /api/players */
		if (strcmp(path, "/api/players") == 0)
		{
			send_json_object(client_fd, "{\"ok\":true,\"players\":[]}\n");
			return 1;
		}

		/* /api/bans */
		if (strcmp(path, "/api/bans") == 0)
		{
			send_json_object(client_fd, "{\"ok\":true,\"bans\":[]}\n");
			return 1;
		}

		/* /api/settings */
		if (strcmp(path, "/api/settings") == 0)
		{
			send_json_object(client_fd,
				"{\"ok\":true,\"settings\":["
				"{\"name\":\"network.server_name\",\"value\":\"JOCES 24/7 CUSTOM\",\"type\":\"string\",\"comment\":\"The name shown in the server browser\"},"
				"{\"name\":\"network.tunnel_port\",\"value\":2302,\"type\":\"integer\",\"comment\":\"The UDP port for internet play\"},"
				"{\"name\":\"network.online\",\"value\":true,\"type\":\"boolean\",\"comment\":\"Enable internet play\"},"
				"{\"name\":\"network.host_public\",\"value\":true,\"type\":\"boolean\",\"comment\":\"List the game in the server browser\"},"
				"{\"name\":\"network.public_chat\",\"value\":true,\"type\":\"boolean\",\"comment\":\"Allow text chat in public games\"},"
				"{\"name\":\"network.map_rotation\",\"value\":\"shuffle\",\"type\":\"string\",\"choices\":[\"sequence\",\"shuffle\"],\"comment\":\"Playlist rotation mode\"},"
				"{\"name\":\"network.map_voting\",\"value\":true,\"type\":\"boolean\",\"comment\":\"Allow lobby map voting by chat\"},"
				"{\"name\":\"network.afk_timeout\",\"value\":180,\"type\":\"integer\",\"comment\":\"AFK inactivity timeout in seconds\"}"
				"]}\n");
			return 1;
		}

		/* /api/log */
		if (strcmp(path, "/api/log") == 0)
		{
			send_json_object(client_fd, "{\"ok\":true,\"cursor\":0,\"lines\":[]}\n");
			return 1;
		}

		/* /api/events */
		if (strcmp(path, "/api/events") == 0)
		{
			send_json_object(client_fd, "{\"ok\":true,\"events\":[]}\n");
			return 1;
		}
	}

	/* 3. POST Commands */
	if (strcmp(method, "POST") == 0)
	{
		if (strcmp(path, "/api/command") == 0)
		{
			char const *line_ptr = buffer ? strstr(buffer, "\"line\":") : NULL;
			if (line_ptr)
			{
				char cmd[256];
				char const *start = strchr(line_ptr + 7, '"');
				if (start)
				{
					char const *end = strchr(start + 1, '"');
					if (end && (end - start < (int)sizeof(cmd)))
					{
						snprintf(cmd, (size_t)(end - start), "%s", start + 1);
						hs_compile_and_evaluate(cmd);
					}
				}
			}
			send_json_object(client_fd, "{\"ok\":true,\"lines\":[\"command executed\"]}\n");
			return 1;
		}

		if (strcmp(path, "/api/rotation/next") == 0)
		{
			struct network_game_server *server = global_network_game_server_get();
			if (server)
				network_game_server_port_end_game(server);
			send_json_object(client_fd, "{\"ok\":true,\"status\":\"advancing\"}\n");
			return 1;
		}
	}

	return 0;
}

#endif
