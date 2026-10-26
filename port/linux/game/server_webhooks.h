#ifndef SERVER_WEBHOOKS_H
#define SERVER_WEBHOOKS_H

/* Asynchronous webhook dispatcher for Discord and Telegram */
void server_webhooks_init(void);
void server_webhooks_notify_match_start(const char *map, const char *gametype, int players, int max_players);
void server_webhooks_notify_match_end(const char *map, const char *gametype, const char *winner, int winning_score);
void server_webhooks_notify_ban(const char *player_name, const char *reason, const char *duration);

#endif
