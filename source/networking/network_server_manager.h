/*
NETWORK_SERVER_MANAGER.H

header included in hcex build.
*/

#ifndef __NETWORK_SERVER_MANAGER_H
#define __NETWORK_SERVER_MANAGER_H
#pragma once

/* ---------- headers */

#include "cseries.h"

/* ---------- constants */

/* ---------- macros */

/* ---------- structures */

/* ---------- prototypes/NETWORK_SERVER_MANAGER.C */

/* port: whether the host's game is being played (not its lobby) */
boolean network_game_server_playing(
	struct network_game_server *server);

struct network_game_server;
struct game_variant;

struct network_game_server *network_game_server_create(
	void);
void network_game_server_dispose(
	struct network_game_server *server);
boolean network_game_server_idle(
	struct network_game_server *server);
void network_game_server_open_game(
	struct network_game_server *server);
void network_game_server_switch_to_postgame(
	struct network_game_server *server);
boolean network_game_server_graceful_shutdown(
	struct network_game_server *server);
boolean network_game_server_reset_to_pregame(
	struct network_game_server *server);
word network_game_server_get_state(
	struct network_game_server *server,
	short *substate);
struct network_game *network_game_server_get_game(
	struct network_game_server *server);
void network_game_server_pause_countdown(
	struct network_game_server *server,
	boolean pause_countdown);
/* port: the players a game a host runs asks for (network_test.c): a host
given a map list asks for one, so it starts with nobody on it */
void network_game_server_port_set_minimum_players(
	struct network_game_server *server,
	long minimum_players);
/* port: drop the joins a finished game left waiting, so the next one
starts with none (network_test.c's host) */
void network_game_server_port_clear_queued_players(
	struct network_game_server *server);
/* port: the players a host's game holds (a variant with teams asks for
one on each: server_needs_more_teams) */
long network_game_server_port_player_count(
	struct network_game_server *server);
/* port: the machines a host's game asks for (a host the map list is
given plays on its own: one) */
void network_game_server_port_set_minimum_machines(
	struct network_game_server *server,
	long minimum_machines);
/* port: why a game a host asks for is not starting: the server's state,
its countdown and the conditions it waits for (network_test.c) */
void network_game_server_port_log_state(
	struct network_game_server *server);
/* port: whether the game is counting down to its start. The lobby shows it
to every machine (network_game_server_countdown_started), so a host that lets
it run shows the players the time they have (network_test.c), rather than
starting the game itself. */
boolean network_game_server_port_countdown_active(
	struct network_game_server *server);
/* port: start the lobby's countdown, as a machine joining the lobby does
(network_game_server_update_countdown). A host with nobody but itself at the
menus has no client of its own to ask for one, and the countdown is what every
machine's lobby shows until the game begins (network_test.c). seconds: how
long it counts (0: the game's own, NETWORK_GAME_COUNTDOWN_TIME). */
void network_game_server_port_start_countdown(
	struct network_game_server *server,
	long seconds);
/* port: the minutes a game a host's list plays is given (the game's own
options come from the server, not from the menus' variant: a built-in
variant's time limit is none, and a game whose score nobody reaches would
never end and the list would stand still) */
void network_game_server_port_set_time_limit(
	struct network_game_server *server,
	long minutes);
/* port: end a host's game (postgame), for a dedicated host whose players have
all left (network_test.c) */
void network_game_server_port_end_game(
	struct network_game_server *server);
/* port: directly force the server to start loading the network game,
bypassing client countdown packets that can be dropped or delayed */
boolean network_game_server_port_force_start(
	struct network_game_server *server);
/* port: a dedicated server's lobby (port/linux/game/server_director.c) */
long network_game_server_port_countdown_milliseconds(
	struct network_game_server *server);
boolean network_game_server_port_countdown_paused(
	struct network_game_server *server);
void network_game_server_port_countdown_begin(
	struct network_game_server *server,
	long milliseconds);
boolean network_game_server_port_start_sent(
	struct network_game_server *server);
boolean network_game_server_port_ok_to_countdown(
	struct network_game_server *server);
void network_game_server_port_forget_kicked_address(
	unsigned long address,
	char const *hardware_id);
long network_game_server_port_remote_machine_count(
	struct network_game_server *server,
	boolean joined);
void network_game_generate_join_game_token(
	byte *join_token);
void network_game_server_kick_machine(
	long machine_index,
	boolean kept_out);
/* port: the rejection code of the last machine
network_game_server_accept_client_machine_into_game refused */
void network_game_server_refuse_with(short code);
short network_game_server_last_refusal_code(
	void);
/* the host's ban command (console.c, hs.c) */
enum
{
	NETWORK_GAME_SERVER_NAME_TEXT_SIZE = 16,
};
/* port: the PC menus' server settings: the game's name (empty: the
machine's) and the most players (0: every player the build holds), for
every game the server sets up */
void network_game_server_port_set_settings(
	wchar_t const *name,
	long maximum_players);
/* port: a co-op level was won (main.c). Ends the round for everyone as in
multiplayer; the next round is on next_map (NULL repeats the level). */
void network_game_server_port_cooperative_won(
	char const *next_map);
/* port: co-op server settings: the difficulty, sent to clients, and the
most players a co-op game starts with (Server Setup sets its own) */
void network_game_server_port_set_cooperative(
	struct network_game_server *server,
	short difficulty);
/* port: co-op's friendly fire between its players (a _friendly_fire_ mode:
Server Setup's FRIENDLY FIRE), kept for the levels after */
void network_game_server_port_set_cooperative_friendly_fire(
	short friendly_fire);
/* port: whether co-op's players collide with each other (Server Setup's
PLAYER COLLISIONS), kept for the levels after */
/* port: the co-op flag that says the host is a dedicated server */
void network_game_server_port_set_cooperative_dedicated(
	boolean dedicated);
void network_game_server_port_set_cooperative_player_collisions(
	boolean player_collisions);
/* port: whether co-op's players hurt their AI allies (Server Setup's ALLY
FRIENDLY FIRE), kept for the levels after */
void network_game_server_port_set_cooperative_ally_friendly_fire(
	boolean ally_friendly_fire);
boolean network_game_server_ban_player(
	char const *text);
/* port: the host's kick command: as the ban command, but nothing kept (no
bans.txt line, the address not kept out): the player may join again at once */
boolean network_game_server_kick_player(
	char const *text);
/* port: the host's Kick and Ban on the scoreboard: the client machine
(its index), kicked or banned as those commands do */
boolean network_game_server_kick_machine_of_player(
	long machine_index,
	boolean ban);
short network_game_server_matching_player_names(
	char const *text,
	char (*names)[NETWORK_GAME_SERVER_NAME_TEXT_SIZE],
	short maximum_count);
unsigned long network_game_server_machine_address(
	long machine_index);
char const *network_game_server_machine_hardware_id(
	long machine_index);
void network_game_server_update_ticks(
	struct network_game_server *server,
	short tick_count);
void network_game_server_change_map_name(
	struct network_game_server *server,
	char const *map_name);
void network_game_server_change_game_variant(
	struct network_game_server *server,
	struct game_variant *variant);

/* ---------- globals */

/* ---------- public code */

#endif // __NETWORK_SERVER_MANAGER_H
