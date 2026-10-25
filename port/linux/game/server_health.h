#ifndef _SERVER_HEALTH_H
#define _SERVER_HEALTH_H

#include "cseries.h"

struct server_health_snapshot
{
	int state; /* 0: pregame, 1: in_game, 2: postgame, -1: no_server */
	char map[64];
	char variant[64];
	char server_name[64];
	char invite[128];
	int players;
	int max_players;
	int minutes_left;
	boolean listed;
	unsigned long uptime_seconds;
};

/* starts the endpoint on the port (0: off); called once the host is set up */
void server_health_start(short port);

/* publishes the current state for the endpoint to serve; called every frame
   from network_test_update() while hosting */
void server_health_publish(void);

/* reads a thread-safe copy of the latest published snapshot */
void server_health_get_snapshot(struct server_health_snapshot *out);

/* stops the endpoint (socket closed, thread told to exit) */
void server_health_stop(void);

#endif /* _SERVER_HEALTH_H */
