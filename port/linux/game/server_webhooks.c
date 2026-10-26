#include "server_webhooks.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

extern const char *config_string(const char *name);

static void send_http_post(const char *url, const char *json_payload)
{
    /* Format curl command in a detached process or quiet execution */
    char cmd[4096];
    if (!url || !*url || !json_payload)
        return;

    /* Execute asynchronously in background (&) without stalling the game thread */
    snprintf(cmd, sizeof(cmd),
        "curl -s -X POST -H 'Content-Type: application/json' -d '%s' '%s' >/dev/null 2>&1 &",
        json_payload, url);
    system(cmd);
}

void server_webhooks_init(void)
{
    /* Webhooks ready */
}

void server_webhooks_notify_match_start(const char *map, const char *gametype, int players, int max_players)
{
    const char *discord_url = config_string("webhooks.discord");
    const char *telegram_url = config_string("webhooks.telegram");
    char payload[1024];

    if (discord_url && *discord_url)
    {
        snprintf(payload, sizeof(payload),
            "{\"content\":\"Halo Server: Match Started on **%s** (%s) - Players: %d/%d\"}",
            map ? map : "Unknown", gametype ? gametype : "Slayer", players, max_players);
        send_http_post(discord_url, payload);
    }

    if (telegram_url && *telegram_url)
    {
        snprintf(payload, sizeof(payload),
            "{\"text\":\"Halo Server: Match Started on %s (%s) - Players: %d/%d\"}",
            map ? map : "Unknown", gametype ? gametype : "Slayer", players, max_players);
        send_http_post(telegram_url, payload);
    }
}

void server_webhooks_notify_match_end(const char *map, const char *gametype, const char *winner, int winning_score)
{
    const char *discord_url = config_string("webhooks.discord");
    const char *telegram_url = config_string("webhooks.telegram");
    char payload[1024];

    if (discord_url && *discord_url)
    {
        snprintf(payload, sizeof(payload),
            "{\"content\":\"Halo Server: Match Finished on **%s** (%s)! Winner: **%s** (%d points)\"}",
            map ? map : "Unknown", gametype ? gametype : "Slayer", winner ? winner : "None", winning_score);
        send_http_post(discord_url, payload);
    }

    if (telegram_url && *telegram_url)
    {
        snprintf(payload, sizeof(payload),
            "{\"text\":\"Halo Server: Match Finished on %s (%s)! Winner: %s (%d points)\"}",
            map ? map : "Unknown", gametype ? gametype : "Slayer", winner ? winner : "None", winning_score);
        send_http_post(telegram_url, payload);
    }
}

void server_webhooks_notify_ban(const char *player_name, const char *reason, const char *duration)
{
    const char *discord_url = config_string("webhooks.discord");
    const char *telegram_url = config_string("webhooks.telegram");
    char payload[1024];

    if (discord_url && *discord_url)
    {
        snprintf(payload, sizeof(payload),
            "{\"content\":\"Halo Server Moderation: Player **%s** was BANNED [%s] - Reason: %s\"}",
            player_name ? player_name : "Unknown", duration ? duration : "permanent", reason ? reason : "cheating");
        send_http_post(discord_url, payload);
    }

    if (telegram_url && *telegram_url)
    {
        snprintf(payload, sizeof(payload),
            "{\"text\":\"Halo Server Moderation: Player %s was BANNED [%s] - Reason: %s\"}",
            player_name ? player_name : "Unknown", duration ? duration : "permanent", reason ? reason : "cheating");
        send_http_post(telegram_url, payload);
    }
}
