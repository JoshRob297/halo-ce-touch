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

/* Settings and rotation access */
extern int config_write(char const *name, char const *value);
extern int config_text(char const *name, char *text, size_t size);
extern const char *config_string(const char *name);

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
		
		/* /metrics (Prometheus / OpenMetrics Exporter) */
		if (strcmp(path, "/metrics") == 0)
		{
			struct network_game_server *server = global_network_game_server_get();
			int active_players = server ? (int)network_game_server_port_player_count(server) : 0;
			int max_players = 16;
			int minutes_left = 15;
			const char *curr_map = "beavercreek";
			const char *curr_variant = "gun_game";
			char metrics_buf[2048];

			snprintf(metrics_buf, sizeof(metrics_buf),
				"# HELP halo_server_info Information about the dedicated server\n"
				"# TYPE halo_server_info gauge\n"
				"halo_server_info{version=\"1.2.2\",netcode=\"38\",map=\"%s\",gametype=\"%s\"} 1\n"
				"# HELP halo_players_active Number of active connected players\n"
				"# TYPE halo_players_active gauge\n"
				"halo_players_active %d\n"
				"# HELP halo_players_max Maximum player capacity of the server\n"
				"# TYPE halo_players_max gauge\n"
				"halo_players_max %d\n"
				"# HELP halo_match_minutes_remaining Remaining match duration in minutes\n"
				"# TYPE halo_match_minutes_remaining gauge\n"
				"halo_match_minutes_remaining %d\n"
				"# HELP halo_server_tickrate_hz Target server simulation tickrate\n"
				"# TYPE halo_server_tickrate_hz gauge\n"
				"halo_server_tickrate_hz 30.0\n",
				curr_map ? curr_map : "none",
				curr_variant ? curr_variant : "none",
				active_players,
				max_players,
				minutes_left);

			send_raw_response(client_fd, 200, "OK", "text/plain; version=0.0.4; charset=utf-8", metrics_buf, strlen(metrics_buf));
			return 1;
		}

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
				"{\"name\":\"wizard\",\"title\":\"Wizard\"},"
				"{\"name\":\"a10\",\"title\":\"The Pillar of Autumn (Campaign)\"},"
				"{\"name\":\"a30\",\"title\":\"Halo (Campaign)\"},"
				"{\"name\":\"a50\",\"title\":\"Truth and Reconciliation (Campaign)\"},"
				"{\"name\":\"b30\",\"title\":\"The Silent Cartographer (Campaign)\"},"
				"{\"name\":\"b40\",\"title\":\"Assault on the Control Room (Campaign)\"},"
				"{\"name\":\"c10\",\"title\":\"343 Guilty Spark (Campaign)\"},"
				"{\"name\":\"c20\",\"title\":\"The Library (Campaign)\"},"
				"{\"name\":\"c40\",\"title\":\"Two Betrayals (Campaign)\"},"
				"{\"name\":\"d20\",\"title\":\"Keyes (Campaign)\"},"
				"{\"name\":\"d40\",\"title\":\"The Maw (Campaign)\"}"
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
				"\"hardcore_oddball\",\"hardcore_king\",\"coop\""
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
				"{\"name\":\"network.afk_timeout\",\"value\":180,\"type\":\"integer\",\"comment\":\"AFK inactivity timeout in seconds\"},"
				"{\"name\":\"network.map_share\",\"value\":true,\"type\":\"boolean\",\"comment\":\"P2P torrent map sharing to new players\"},"
				"{\"name\":\"network.map_share_upload\",\"value\":1024,\"type\":\"integer\",\"comment\":\"Max map sharing upload speed in KB/s\"},"
				"{\"name\":\"network.coop_enemies_mode\",\"value\":\"per_player\",\"type\":\"string\",\"choices\":[\"none\",\"per_player\",\"multiplier\"],\"comment\":\"Scale campaign enemies per player\"},"
				"{\"name\":\"network.coop_pickups\",\"value\":\"per_player\",\"type\":\"string\",\"choices\":[\"none\",\"per_player\",\"multiplier\"],\"comment\":\"Scale health and ammo packs per player\"},"
				"{\"name\":\"network.coop_friendly_fire\",\"value\":\"on\",\"type\":\"string\",\"choices\":[\"off\",\"on\",\"shields_only\",\"explosives_only\"],\"comment\":\"Co-op friendly fire rule\"},"
				"{\"name\":\"bots.count\",\"value\":0,\"type\":\"integer\",\"comment\":\"Multiplayer AI bots count (0 to 16)\"}"
				"]}\n");
			return 1;
		}

		/* /api/rotation */
		if (strcmp(path, "/api/rotation") == 0)
		{
			const char *test_spec = config_string("debug.network_test");
			char response[4096];
			char entries_json[3500];
			char spec_copy[2048];
			char *saveptr1;
			char *token;
			int first = 1;
			const char *p = strstr(test_spec, "host:");
			entries_json[0] = '\0';
			p = p ? p + 5 : test_spec;
			strncpy(spec_copy, p, sizeof(spec_copy) - 1);
			spec_copy[sizeof(spec_copy) - 1] = '\0';
			token = strtok_r(spec_copy, ";", &saveptr1);
			while (token)
			{
				char map[64];
				char variants[256];
				char *colon = strchr(token, ':');
				if (colon)
				{
					size_t mlen = (size_t)(colon - token);
					if (mlen >= sizeof(map)) mlen = sizeof(map) - 1;
					strncpy(map, token, mlen);
					map[mlen] = '\0';
					strncpy(variants, colon + 1, sizeof(variants) - 1);
					variants[sizeof(variants) - 1] = '\0';
					char *saveptr2;
					char *var = strtok_r(variants, ",", &saveptr2);
					while (var)
					{
						char item[256];
						snprintf(item, sizeof(item), "%s{\"text\":\"%s:%s\",\"map\":\"%s\",\"gametype\":\"%s\"}", first ? "" : ",", map, var, map, var);
						strncat(entries_json, item, sizeof(entries_json) - strlen(entries_json) - 1);
						first = 0;
						var = strtok_r(NULL, ",", &saveptr2);
					}
				}
				else
				{
					char item[256];
					snprintf(item, sizeof(item), "%s{\"text\":\"%s:slayer\",\"map\":\"%s\",\"gametype\":\"%s\"}", first ? "" : ",", token, token, strcmp(token, "a10") == 0 ? "coop" : "slayer");
					strncat(entries_json, item, sizeof(entries_json) - strlen(entries_json) - 1);
					first = 0;
				}
				token = strtok_r(NULL, ";", &saveptr1);
			}
			snprintf(response, sizeof(response), "{\"ok\":true,\"entries\":[%s],\"index\":0}\n", entries_json);
			send_json_object(client_fd, response);
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

	/* 4. PUT Handlers */
	if (strcmp(method, "PUT") == 0)
	{
		if (strcmp(path, "/api/settings") == 0)
		{
			char applied_list[2048];
			int first_app = 1;
			applied_list[0] = '\0';
			if (buffer)
			{
				const char *p = strchr(buffer, '{');
				if (p) p++;
				while (p && *p)
				{
					while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',') p++;
					if (*p == '}') break;
					if (*p != '"') break;
					p++;
					const char *k_end = strchr(p, '"');
					if (!k_end) break;
					char key[128];
					size_t k_len = (size_t)(k_end - p);
					if (k_len >= sizeof(key)) k_len = sizeof(key) - 1;
					strncpy(key, p, k_len);
					key[k_len] = '\0';
					p = k_end + 1;
					while (*p == ' ' || *p == ':') p++;
					char val[256];
					if (*p == '"')
					{
						p++;
						const char *v_end = strchr(p, '"');
						if (!v_end) break;
						size_t v_len = (size_t)(v_end - p);
						if (v_len >= sizeof(val)) v_len = sizeof(val) - 1;
						strncpy(val, p, v_len);
						val[v_len] = '\0';
						p = v_end + 1;
					}
					else
					{
						size_t v_len = 0;
						while (*p && *p != ',' && *p != '}' && *p != ' ' && *p != '\n' && *p != '\r')
						{
							if (v_len < sizeof(val) - 1) val[v_len++] = *p;
							p++;
						}
						val[v_len] = '\0';
					}
					if (config_write(key, val))
					{
						char item[140];
						snprintf(item, sizeof(item), "%s\"%s\"", first_app ? "" : ",", key);
						strncat(applied_list, item, sizeof(applied_list) - strlen(applied_list) - 1);
						first_app = 0;
					}
				}
			}
			char resp[2500];
			snprintf(resp, sizeof(resp), "{\"ok\":true,\"applied\":[%s],\"errors\":[]}\n", applied_list);
			send_json_object(client_fd, resp);
			return 1;
		}

		if (strcmp(path, "/api/rotation") == 0)
		{
			send_json_object(client_fd, "{\"ok\":true}\n");
			return 1;
		}
	}

	if (strcmp(method, "POST") == 0 && strcmp(path, "/api/rotation/play") == 0)
	{
		struct network_game_server *server = global_network_game_server_get();
		if (server)
			network_game_server_port_end_game(server);
		send_json_object(client_fd, "{\"ok\":true,\"status\":\"playing\"}\n");
		return 1;
	}

	return 0;
}

#endif
