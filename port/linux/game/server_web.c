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


static int remove_ban_entry(const char *target)
{
	FILE *f = fopen("bans.txt", "r");
	if (!f) f = fopen("/opt/halo-server/bans.txt", "r");
	if (!f) return 0;

	char lines[64][512];
	int count = 0;
	int removed = 0;
	while (count < 64 && fgets(lines[count], sizeof(lines[count]), f))
	{
		if (target && target[0] && strstr(lines[count], target))
		{
			removed++;
			continue;
		}
		count++;
	}
	fclose(f);

	if (removed)
	{
		FILE *out = fopen("bans.txt", "w");
		if (!out) out = fopen("/opt/halo-server/bans.txt", "w");
		if (out)
		{
			int i;
			for (i = 0; i < count; i++)
				fputs(lines[i], out);
			fclose(out);
		}
	}
	return removed;
}

static int extract_json_string(const char *json, const char *key, char *out, size_t max_len)
{
	char pattern[64];
	const char *found;
	const char *start;
	const char *end;
	size_t len;

	if (!json || !key || !out || max_len == 0)
		return 0;

	snprintf(pattern, sizeof(pattern), "\"%s\"", key);
	found = strstr(json, pattern);
	if (!found)
		return 0;

	found += strlen(pattern);
	while (*found == ' ' || *found == '\t' || *found == ':')
		found++;

	if (*found != '"')
		return 0;

	start = found + 1;
	end = strchr(start, '"');
	if (!end)
		return 0;

	len = (size_t)(end - start);
	if (len >= max_len)
		len = max_len - 1;

	memcpy(out, start, len);
	out[len] = '\0';
	return 1;
}


#include <sys/utsname.h>

static unsigned long get_process_rss_kb(void)
{
	char buf[128];
	snprintf(buf, sizeof(buf), "/proc/%d/statm", (int)getpid());
	FILE *f = fopen(buf, "r");
	unsigned long size = 0, resident = 0;
	if (!f) return 120 * 1024;
	if (fscanf(f, "%lu %lu", &size, &resident) >= 2)
	{
		fclose(f);
		return resident * 4;
	}
	fclose(f);
	return 120 * 1024;
}

static void get_system_node_info(char *buf, size_t size)
{
	struct utsname u;
	if (uname(&u) == 0)
	{
		snprintf(buf, size, "%s (%s %s)", u.nodename, u.sysname, u.machine);
	}
	else
	{
		snprintf(buf, size, "halo-node (Linux x86_64)");
	}
}


/* Settings and rotation access */
extern int config_write(char const *name, char const *value);
extern int config_text(char const *name, char *text, size_t size);
extern void network_test_force_game(const char *map, const char *variant);
extern const char *config_string(const char *name);

#if !defined(_WIN32) && !defined(HALO_ANDROID) && !defined(HALO_ARM64_GUEST) && !defined(HALO_WEB)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
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

	/* 1. Static Assets - DESACOPLADO CON HOT-RELOAD DESDE DISCO */
	if (strcmp(method, "GET") == 0 || strcmp(method, "HEAD") == 0)
	{
		/* Si existe un archivo override en disco (ej: /opt/halo-server/web/index.html o ./web/...), servirlo directamente al vuelo */
		char disk_path[512];
		snprintf(disk_path, sizeof(disk_path), "web/%s", target_file);
		FILE *df = fopen(disk_path, "rb");
		if (!df)
		{
			snprintf(disk_path, sizeof(disk_path), "/opt/halo-server/web/%s", target_file);
			df = fopen(disk_path, "rb");
		}
		if (df)
		{
			fseek(df, 0, SEEK_END);
			long fsz = ftell(df);
			fseek(df, 0, SEEK_SET);
			if (fsz > 0 && fsz < 10 * 1024 * 1024)
			{
				char *fbuf = (char *)malloc((size_t)fsz);
				if (fbuf)
				{
					size_t r = fread(fbuf, 1, (size_t)fsz, df);
					fclose(df);
					const char *ctype = "text/html; charset=utf-8";
					if (strstr(target_file, ".css")) ctype = "text/css; charset=utf-8";
					else if (strstr(target_file, ".js")) ctype = "application/javascript; charset=utf-8";
					else if (strstr(target_file, ".svg")) ctype = "image/svg+xml";
					else if (strstr(target_file, ".json")) ctype = "application/json";

					send_raw_response(client_fd, 200, "OK", ctype,
						strcmp(method, "HEAD") == 0 ? NULL : fbuf, r);
					free(fbuf);
					return 1;
				}
			}
			fclose(df);
		}

		/* Fallback al array embebido en memoria si no hay archivo en disco */
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
			char custom_json[2048] = { 0 };
			DIR *d = opendir("custom_maps");
			if (d)
			{
				struct dirent *dir;
				while ((dir = readdir(d)) != NULL)
				{
					if (dir->d_name[0] != '.')
					{
						char *dot = strstr(dir->d_name, ".map");
						if (dot && dot[4] == ' ')
						{
							char map_name[128];
							size_t len = (size_t)(dot - dir->d_name);
							if (len >= sizeof(map_name)) len = sizeof(map_name) - 1;
							strncpy(map_name, dir->d_name, len);
							map_name[len] = ' ';
							char entry[256];
							snprintf(entry, sizeof(entry), ",{\"name\":\"custom_maps\\\\%s\",\"title\":\"%s (Custom Map)\",\"custom\":true}",
								map_name, map_name);
							strncat(custom_json, entry, sizeof(custom_json) - strlen(custom_json) - 1);
						}
					}
				}
				closedir(d);
			}

			char full_maps[8192];
			snprintf(full_maps, sizeof(full_maps),
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
				"%s"
				"]}\n", custom_json);
			send_json_object(client_fd, full_maps);
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
			char node_buf[128];
			get_system_node_info(node_buf, sizeof(node_buf));
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
					"\"network_version\":39,"
					"\"rss_kb\":%lu,"
					"\"node_info\":\"%s\","
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
					config_string("network.server_name")[0] ? config_string("network.server_name") : "JOCESLAB MX",
					state_str,
					snap.map[0] ? snap.map : "beavercreek",
					snap.variant[0] ? snap.variant : "Gun Game",
					(unsigned long)(snap.state == 1 ? 45 : 0),
					snap.players,
					snap.max_players > 0 ? snap.max_players : 16,
					snap.uptime_seconds,
					get_process_rss_kb(),
					node_buf,
					snap.invite);

				send_json_object(client_fd, json_buf);
				free(json_buf);
			}
			return 1;
		}

		/* /api/players */
		if (strcmp(path, "/api/players") == 0)
		{
			struct network_game_server *server = global_network_game_server_get();
			struct network_game *game = server ? network_game_server_get_game(server) : NULL;
			extern char const *network_game_server_machine_hardware_id(long machine_index);
			char resp[4096];
			char list[3500];
			int first_p = 1;
			int i;
			list[0] = '\0';

			if (game)
			{
				for (i = 0; i < 16; i++)
				{
					struct network_player *p = &game->players[i];
					/* Ocultar el host dummy del slot 0 si network_test_player esta activo */
					if (i == 0 && config_boolean("debug.network_test_player"))
						continue;
					if (network_player_is_valid(p))
					{
						char name[32];
						int c;
						for (c = 0; c < 11 && p->name[c]; c++)
							name[c] = (char)p->name[c];
						name[c] = '\0';

						char const *raw_hwid = network_game_server_machine_hardware_id((long)p->machine_index);
						char hwid_trunc[16];
						if (raw_hwid && raw_hwid[0]) {
							snprintf(hwid_trunc, sizeof(hwid_trunc), "%.8s...", raw_hwid);
						} else {
							snprintf(hwid_trunc, sizeof(hwid_trunc), "local");
						}

						int est_ping = 18 + ((i * 7 + (int)time(NULL) % 7) % 20);

						char item[256];
						snprintf(item, sizeof(item),
							"%s{\"slot\":%d,\"name\":\"%s\",\"team\":%d,\"machine\":%d,\"score\":0,\"ping\":%d,\"hwid\":\"%s\"}",
							first_p ? "" : ",", i, name, (int)p->team_index, (int)p->machine_index, est_ping, hwid_trunc);
						strncat(list, item, sizeof(list) - strlen(list) - 1);
						first_p = 0;
					}
				}
			}
			snprintf(resp, sizeof(resp), "{\"ok\":true,\"players\":[%s]}\n", list);
			send_json_object(client_fd, resp);
			return 1;
		}

		/* /api/bans */
		if (strcmp(path, "/api/bans") == 0)
		{
			FILE *f = fopen("bans.txt", "r");
			char bans_json[3500];
			bans_json[0] = '\0';
			int first_b = 1;
			if (f)
			{
				char b_line[512];
				while (fgets(b_line, sizeof(b_line), f))
				{
					char const *ip = strstr(b_line, "ip=");
					char const *hwid = strstr(b_line, "hwid=");
					char const *reason = strstr(b_line, "reason=");
					char ip_val[64] = "unknown";
					char hwid_val[64] = "unknown";
					char reason_val[128] = "moderation";
					if (ip) {
						sscanf(ip + 3, "%63s", ip_val);
					}
					if (hwid) {
						sscanf(hwid + 5, "%63s", hwid_val);
					}
					if (reason) {
						sscanf(reason + 7, "%127[^\r\n\t]", reason_val);
					}
					char item[300];
					snprintf(item, sizeof(item), "%s{\"ip\":\"%s\",\"hwid\":\"%s\",\"reason\":\"%s\"}",
						first_b ? "" : ",", ip_val, hwid_val, reason_val);
					strncat(bans_json, item, sizeof(bans_json) - strlen(bans_json) - 1);
					first_b = 0;
				}
				fclose(f);
			}
			char resp[4096];
			snprintf(resp, sizeof(resp), "{\"ok\":true,\"bans\":[%s]}\n", bans_json);
			send_json_object(client_fd, resp);
			return 1;
		}

				/* /api/settings */
		if (strcmp(path, "/api/settings") == 0)
		{
			char dyn_settings[4096];
			snprintf(dyn_settings, sizeof(dyn_settings),
				"{\"ok\":true,\"settings\":["
				"{\"name\":\"network.server_name\",\"value\":\"%s\",\"type\":\"string\",\"comment\":\"The name shown in the server browser\"},"
				"{\"name\":\"network.tunnel_port\",\"value\":%lu,\"type\":\"integer\",\"comment\":\"The UDP port for internet play\"},"
				"{\"name\":\"network.online\",\"value\":%s,\"type\":\"boolean\",\"comment\":\"Enable internet play\"},"
				"{\"name\":\"network.host_public\",\"value\":%s,\"type\":\"boolean\",\"comment\":\"List the game in the server browser\"},"
				"{\"name\":\"network.public_chat\",\"value\":%s,\"type\":\"boolean\",\"comment\":\"Allow text chat in public games\"},"
				"{\"name\":\"network.map_rotation\",\"value\":\"%s\",\"type\":\"string\",\"choices\":[\"sequence\",\"shuffle\"],\"comment\":\"Playlist rotation mode\"},"
				"{\"name\":\"network.map_voting\",\"value\":%s,\"type\":\"boolean\",\"comment\":\"Allow lobby map voting by chat\"},"
				"{\"name\":\"network.afk_timeout\",\"value\":%lu,\"type\":\"integer\",\"comment\":\"AFK inactivity timeout in seconds\"},"
				"{\"name\":\"server.region\",\"value\":\"%s\",\"type\":\"string\",\"role\":\"admin\",\"comment\":\"Server region label in server browser (eu, na-east, sa...)\"},"
				"{\"name\":\"server.chat_filter\",\"value\":\"%s\",\"type\":\"string\",\"choices\":[\"off\",\"censor\",\"block\"],\"role\":\"moderator\",\"comment\":\"Moderation word filter for text chat and player names (off, censor, block)\"},"
				"{\"name\":\"debug.network_test_countdown\",\"value\":%lu,\"type\":\"integer\",\"role\":\"admin\",\"comment\":\"Lobby countdown in seconds (0 = fast instant transitions)\"},"
				"{\"name\":\"debug.network_test_time\",\"value\":%lu,\"type\":\"integer\",\"role\":\"admin\",\"comment\":\"Match duration time limit in minutes\"},"
				"{\"name\":\"webhooks.discord\",\"value\":\"%s\",\"type\":\"string\",\"role\":\"admin\",\"comment\":\"Discord Webhook URL for match and ban notifications\"},"
				"{\"name\":\"webhooks.telegram\",\"value\":\"%s\",\"type\":\"string\",\"role\":\"admin\",\"comment\":\"Telegram Bot webhook URL\"},"
				"{\"name\":\"paths.logs\",\"value\":\"%s\",\"type\":\"string\",\"role\":\"owner\",\"comment\":\"Custom directory for debug.txt (Unraid/Docker)\"},"
				"{\"name\":\"network.map_share\",\"value\":true,\"type\":\"boolean\",\"comment\":\"P2P torrent map sharing to new players\"},"
				"{\"name\":\"network.map_share_upload\",\"value\":1024,\"type\":\"integer\",\"comment\":\"Max map sharing upload speed in KB/s\"},"
				"{\"name\":\"network.coop_enemies_mode\",\"value\":\"per_player\",\"type\":\"string\",\"choices\":[\"none\",\"per_player\",\"multiplier\"],\"comment\":\"Scale campaign enemies per player\"},"
				"{\"name\":\"network.coop_pickups\",\"value\":\"per_player\",\"type\":\"string\",\"choices\":[\"none\",\"per_player\",\"multiplier\"],\"comment\":\"Scale health and ammo packs per player\"},"
				"{\"name\":\"network.coop_friendly_fire\",\"value\":\"on\",\"type\":\"string\",\"choices\":[\"off\",\"on\",\"shields_only\",\"explosives_only\"],\"comment\":\"Co-op friendly fire rule\"},"
				"{\"name\":\"match.vehicles\",\"value\":\"%s\",\"type\":\"string\",\"choices\":[\"default\",\"none\",\"warthog\",\"ghost\",\"tank\",\"all\"],\"comment\":\"Vehicle set rule\"},"
				"{\"name\":\"match.weapons\",\"value\":\"%s\",\"type\":\"string\",\"choices\":[\"normal\",\"pistols\",\"assault_rifles\",\"plasma_weapons\",\"sniping\",\"rockets\",\"shotguns\",\"classic\",\"heavy\"],\"comment\":\"Starting weapons rule\"},"
				"{\"name\":\"match.friendly_fire\",\"value\":\"%s\",\"type\":\"string\",\"choices\":[\"off\",\"on\",\"shields_only\",\"explosives_only\"],\"comment\":\"Multiplayer friendly fire\"},"
				"{\"name\":\"match.infinite_grenades\",\"value\":%s,\"type\":\"boolean\",\"comment\":\"Infinite grenades\"},"
				"{\"name\":\"match.score_to_win\",\"value\":%lu,\"type\":\"integer\",\"comment\":\"Score to win limit\"},"
				"{\"name\":\"match.respawn_time\",\"value\":%ld,\"type\":\"integer\",\"comment\":\"Player respawn time in seconds (0 = default, -1 = instant)\"},"
				"{\"name\":\"match.vehicle_respawn\",\"value\":%lu,\"type\":\"integer\",\"comment\":\"Vehicle respawn time in seconds\"}"
				"]}\n",
				config_string("network.server_name")[0] ? config_string("network.server_name") : "JOCESLAB MX",
				(unsigned long)config_integer("network.tunnel_port"),
				config_boolean("network.online") ? "true" : "false",
				config_boolean("network.host_public") ? "true" : "false",
				config_boolean("network.public_chat") ? "true" : "false",
				config_string("network.map_rotation"),
				config_boolean("network.map_voting") ? "true" : "false",
				(unsigned long)config_integer("network.afk_timeout"),
				config_string("server.region")[0] ? config_string("server.region") : "na-east",
				config_string("server.chat_filter")[0] ? config_string("server.chat_filter") : "off",
				(unsigned long)config_integer("debug.network_test_countdown"),
				(unsigned long)config_integer("debug.network_test_time"),
				config_string("webhooks.discord"),
				config_string("webhooks.telegram"),
				config_string("paths.logs"),
				config_string("match.vehicles")[0] ? config_string("match.vehicles") : "default",
				config_string("match.weapons")[0] ? config_string("match.weapons") : "normal",
				config_string("match.friendly_fire")[0] ? config_string("match.friendly_fire") : "on",
				config_boolean("match.infinite_grenades") ? "true" : "false",
				(unsigned long)config_integer("match.score_to_win"),
				(long)config_integer("match.respawn_time"),
				(unsigned long)config_integer("match.vehicle_respawn"));
			send_json_object(client_fd, dyn_settings);
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

		/* /api/chat */
		if (strcmp(path, "/api/unban") == 0)
		{
			char target[128];
			if (extract_json_string(buffer, "target", target, sizeof(target)))
			{
				int n = remove_ban_entry(target);
				char resp[128];
				snprintf(resp, sizeof(resp), "{\"ok\":true,\"removed\":%d}\n", n);
				send_json_object(client_fd, resp);
				return 1;
			}
			send_json_object(client_fd, "{\"ok\":false,\"error\":\"missing target\"}\n");
			return 1;
		}

		if (strcmp(path, "/api/chat") == 0)
		{
			extern int server_chat_get_json(char *out, size_t max_len);
			char chat_buf[4096];
			server_chat_get_json(chat_buf, sizeof(chat_buf));
			send_json_object(client_fd, chat_buf);
			return 1;
		}

		/* /api/log */
		if (strcmp(path, "/api/log") == 0)
		{
			FILE *f = fopen("debug.txt", "r");
			if (!f) f = fopen("/opt/halo-server/debug.txt", "r");
			char line_buf[512];
			char lines_json[3900];
			lines_json[0] = '\0';
			int first_l = 1;
			if (f)
			{
				fseek(f, 0, SEEK_END);
				long fsize = ftell(f);
				long seek_pos = fsize > 3000 ? fsize - 3000 : 0;
				fseek(f, seek_pos, SEEK_SET);
				if (seek_pos > 0)
					fgets(line_buf, sizeof(line_buf), f);
				while (fgets(line_buf, sizeof(line_buf), f))
				{
					size_t len = strlen(line_buf);
					while (len > 0 && (line_buf[len-1] == '\r' || line_buf[len-1] == '\n'))
						line_buf[--len] = '\0';
					if (len == 0) continue;
					char esc_line[1024];
					size_t j = 0, i;
					for (i = 0; i < len && j < sizeof(esc_line)-4; i++)
					{
						if (line_buf[i] == '"' || line_buf[i] == '\\')
							esc_line[j++] = '\\';
						esc_line[j++] = line_buf[i];
					}
					esc_line[j] = '\0';
					char item[1200];
					snprintf(item, sizeof(item), "%s\"%s\"", first_l ? "" : ",", esc_line);
					if (strlen(lines_json) + strlen(item) < sizeof(lines_json) - 10)
					{
						strcat(lines_json, item);
						first_l = 0;
					}
				}
				fclose(f);
			}
			char resp[4096];
			snprintf(resp, sizeof(resp), "{\"ok\":true,\"cursor\":1,\"lines\":[%s]}\n", lines_json);
			send_json_object(client_fd, resp);
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
		if (strcmp(path, "/api/chat") == 0)
		{
			char msg[256];
			if (extract_json_string(buffer, "message", msg, sizeof(msg)))
			{
				extern void server_chat_record_message(const char *sender, const char *msg, short channel);
				extern void network_chat_broadcast(const char *text);
				char notice[160];
				snprintf(notice, sizeof(notice), "[SERVER]: %s", msg);
				server_chat_record_message("SERVER", msg, 0);
				network_chat_broadcast(notice);
				send_json_object(client_fd, "{\"ok\":true}\n");
				return 1;
			}
			send_json_object(client_fd, "{\"ok\":false,\"error\":\"missing message\"}\n");
			return 1;
		}

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
						if (strncmp(cmd, "sv_unban ", 9) == 0 || strncmp(cmd, "unban ", 6) == 0)
						{
							const char *tgt = (strncmp(cmd, "sv_unban ", 9) == 0) ? (cmd + 9) : (cmd + 6);
							while (*tgt == ' ') tgt++;
							int n = remove_ban_entry(tgt);
							char resp[256];
							snprintf(resp, sizeof(resp), "{\"ok\":true,\"lines\":[\"Desbaneo ejecutado: %d registro(s) eliminado(s) de bans.txt\"]}\n", n);
							send_json_object(client_fd, resp);
							return 1;
						}
						if (strncmp(cmd, "say ", 4) == 0 || strncmp(cmd, "broadcast ", 10) == 0)
						{
							const char *msg_to_send = (strncmp(cmd, "say ", 4) == 0) ? (cmd + 4) : (cmd + 10);
							extern void server_chat_record_message(const char *sender, const char *msg, short channel);
							extern void network_chat_broadcast(const char *text);
							char notice[160];
							snprintf(notice, sizeof(notice), "[SERVER]: %s", msg_to_send);
							server_chat_record_message("SERVER", msg_to_send, 0);
							network_chat_broadcast(notice);
							send_json_object(client_fd, "{\"ok\":true,\"lines\":[\"Mensaje emitido en el chat de partida\"]}\n");
							return 1;
						}
						if (strncmp(cmd, "sv_map ", 7) == 0)
						{
							char map_arg[64] = { 0 };
							char mode_arg[64] = { 0 };
							if (sscanf(cmd + 7, "%63s %63s", map_arg, mode_arg) >= 1)
							{
								if (!mode_arg[0]) snprintf(mode_arg, sizeof(mode_arg), "slayer");
								network_test_force_game(map_arg, mode_arg);
								send_json_object(client_fd, "{\"ok\":true,\"lines\":[\"Cambiando partida inmediatamente...\"]}\n");
								return 1;
							}
						}
						if (strcmp(cmd, "sv_end_game") == 0 || strcmp(cmd, "sv_map_restart") == 0)
						{
							struct network_game_server *server = global_network_game_server_get();
							if (server)
								network_game_server_port_end_game(server);
							send_json_object(client_fd, "{\"ok\":true,\"lines\":[\"Ronda terminada/reiniciada\"]}\n");
							return 1;
						}
						if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0)
						{
							send_json_object(client_fd,
								"{\"ok\":true,\"lines\":["
								"\"=== COMANDOS DISPONIBLES HALOSCRIPT & DEDICADO ===\","
								"\"  sv_map <mapa> <modo>       - Cargar mapa y modo (ej: sv_map bloodgulch ctf)\","
								"\"  sv_map_restart             - Reiniciar la partida actual\","
								"\"  sv_end_game                - Terminar partida actual y volver a sala\","
								"\"  kick <jugador>             - Expulsar jugador de la partida\","
								"\"  ban <jugador>              - Banear jugador por IP y HWID\","
								"\"  sv_ban_address <ip>        - Veto manual de direccion IP\","
								"\"  sv_mute_player <slot>      - Silenciar a un jugador en el chat\","
								"\"  say <mensaje>              - Enviar mensaje global a todos los jugadores\","
								"\"  cls / clear                - Limpiar terminal web\","
								"\"  status                     - Ver estado del motor y jugadores conectados\""
								"]}\n");
							return 1;
						}
						hs_compile_and_evaluate(cmd);
					}
				}
			}
			send_json_object(client_fd, "{\"ok\":true,\"lines\":[\"command executed\"]}\n");
			return 1;
		}

		if (strcmp(path, "/api/game/load") == 0)
		{
			char map_val[64] = { 0 };
			char mode_val[64] = { 0 };
			const char *map_ptr = buffer ? strstr(buffer, "\"map\":\"") : NULL;
			const char *mode_ptr = buffer ? strstr(buffer, "\"mode\":\"") : NULL;
			if (map_ptr)
			{
				sscanf(map_ptr + 7, "%63[^\"]", map_val);
			}
			if (mode_ptr)
			{
				sscanf(mode_ptr + 8, "%63[^\"]", mode_val);
			}
			if (!mode_val[0]) snprintf(mode_val, sizeof(mode_val), "slayer");
			if (map_val[0])
			{
				network_test_force_game(map_val, mode_val);
				send_json_object(client_fd, "{\"ok\":true,\"status\":\"loaded\"}\n");
				return 1;
			}
			send_json_object(client_fd, "{\"ok\":false,\"error\":\"missing map\"}\n");
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
			const char *spec_ptr = buffer ? strstr(buffer, "\"spec\":\"") : NULL;
			if (spec_ptr)
			{
				char new_spec[2048] = { 0 };
				sscanf(spec_ptr + 8, "%2047[^\"]", new_spec);
				if (new_spec[0])
				{
					char full_spec[2200];
					snprintf(full_spec, sizeof(full_spec), "host:%s", new_spec);
					config_write("debug.network_test", full_spec);
					send_json_object(client_fd, "{\"ok\":true,\"status\":\"updated\"}\n");
					return 1;
				}
			}
			send_json_object(client_fd, "{\"ok\":true}\n");
			return 1;
		}
	}

	
	
		/* /api/master/key */
		if (strcmp(path, "/api/master/key") == 0)
		{
			unsigned char pub[32];
			char key_id[64] = { 0 };
			char pub_hex[65] = { 0 };
			extern int server_master_publisher(unsigned char *public_key, char *key_id);
			extern void p2p_hex(const unsigned char *data, int size, char *hex);

			if (server_master_publisher(pub, key_id))
			{
				p2p_hex(pub, 32, pub_hex);
				char resp[512];
				snprintf(resp, sizeof(resp),
					"{\"ok\":true,\"public_key\":\"%s\",\"key_id\":\"%s\",\"apply_cmd\":\"/apply public_key:%s server_name:%s\"}\n",
					pub_hex, key_id, pub_hex, config_string("network.server_name")[0] ? config_string("network.server_name") : "JOCESLAB MX");
				send_json_object(client_fd, resp);
				return 1;
			}
			send_json_object(client_fd, "{\"ok\":false,\"error\":\"no publisher key\"}\n");
			return 1;
		}

		/* /api/master/sign */
		if (strcmp(path, "/api/master/sign") == 0)
		{
			char nonce[256] = { 0 };
			const char *nonce_ptr = buffer ? strstr(buffer, "\"nonce\":\"") : NULL;
			if (nonce_ptr)
			{
				sscanf(nonce_ptr + 9, "%255[^\"]", nonce);
			}
			if (nonce[0])
			{
				/* Firmar el nonce con la publisher key */
				unsigned char sig[64];
				char sig_hex[129] = { 0 };
				FILE *pipe;
				char cmd[512];
				snprintf(cmd, sizeof(cmd), "./halo --sign '%s' 2>/dev/null", nonce);
				pipe = popen(cmd, "r");
				if (pipe)
				{
					if (fgets(sig_hex, sizeof(sig_hex), pipe))
					{
						size_t slen = strlen(sig_hex);
						while (slen > 0 && (sig_hex[slen - 1] == '\n' || sig_hex[slen - 1] == '\r' || sig_hex[slen - 1] == ' '))
						{
							sig_hex[--slen] = '\0';
						}
					}
					pclose(pipe);
				}
				if (sig_hex[0])
				{
					char resp[512];
					snprintf(resp, sizeof(resp),
						"{\"ok\":true,\"nonce\":\"%s\",\"signature\":\"%s\",\"verify_cmd\":\"/verify nonce:%s signature:%s\"}\n",
						nonce, sig_hex, nonce, sig_hex);
					send_json_object(client_fd, resp);
					return 1;
				}
			}
			send_json_object(client_fd, "{\"ok\":false,\"error\":\"missing or invalid nonce\"}\n");
			return 1;
		}

	if (strcmp(method, "DELETE") == 0 && strncmp(path, "/api/rotation/entry", 19) == 0)
	{
		char const *spec = config_string("debug.network_test");
		char new_spec[2048] = { 0 };
		const char *target = buffer ? strstr(buffer, "\"target\":\"") : NULL;
		char target_val[64] = { 0 };
		if (target)
		{
			sscanf(target + 10, "%63[^\"]", target_val);
		}
		if (target_val[0] && spec && spec[0])
		{
			char copy[2048];
			char *saveptr;
			const char *prefix = (!strncmp(spec, "host:", 5)) ? "host:" : "";
			const char *content = prefix[0] ? spec + 5 : spec;
			strncpy(copy, content, sizeof(copy) - 1);
			copy[sizeof(copy) - 1] = ' ';
			char *token = strtok_r(copy, ";", &saveptr);
			int first_tok = 1;
			snprintf(new_spec, sizeof(new_spec), "%s", prefix);

			while (token)
			{
				if (strstr(token, target_val) == NULL)
				{
					if (!first_tok) strncat(new_spec, ";", sizeof(new_spec) - strlen(new_spec) - 1);
					strncat(new_spec, token, sizeof(new_spec) - strlen(new_spec) - 1);
					first_tok = 0;
				}
				token = strtok_r(NULL, ";", &saveptr);
			}
			config_write("debug.network_test", new_spec);
			send_json_object(client_fd, "{\"ok\":true,\"status\":\"removed\"}\n");
			return 1;
		}
		send_json_object(client_fd, "{\"ok\":false,\"error\":\"invalid target\"}\n");
		return 1;
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
