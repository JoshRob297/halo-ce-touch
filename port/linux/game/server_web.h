#ifndef SERVER_WEB_H
#define SERVER_WEB_H

#if !defined(_WIN32) && !defined(HALO_ANDROID) && !defined(HALO_ARM64_GUEST) && !defined(HALO_WEB)

struct server_ui_asset
{
	const char *path;
	const char *content_type;
	const unsigned char *data;
	unsigned long size;
	const char *etag;
};

extern const struct server_ui_asset server_ui_assets[];
extern const unsigned long server_ui_asset_count;

/* Handles an incoming HTTP connection from server_health thread.
   Returns 1 if handled, 0 if not (falling back to legacy /health). */
int server_web_dispatch(int client_fd, const char *method, const char *path, const char *buffer, int received);

#endif

#endif /* SERVER_WEB_H */
