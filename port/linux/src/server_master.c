/*
SERVER_MASTER.C

A dedicated server's publishing to the official master servers
(server_master.h; docs/master_protocol.md).
*/

#include "platform.h"
#include "port_config.h"
#include "server_master.h"
#include "server_mode.h"

#if HALO_SERVER_BUILD

#include "p2p.h"
#include "p2p_internal.h"
#include "posix.h"
#include "posix_server.h"

#include <SDL3/SDL.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- the publisher key */

static int hex_value(char character)
{
	if (character >= '0' && character <= '9')
		return character - '0';
	if (character >= 'a' && character <= 'f')
		return character - 'a' + 10;
	if (character >= 'A' && character <= 'F')
		return character - 'A' + 10;
	return -1;
}

/* a seed's 64 hex digits (spaces and a comment after them allowed): 1 */
static int seed_from_text(const char *text, unsigned char *seed)
{
	int index;

	while (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r')
		text++;
	for (index = 0; index < SERVER_MASTER_SEED_SIZE; index++)
	{
		int high = hex_value(text[2 * index]), low = high >= 0 ? hex_value(text[2 * index + 1]) : -1;

		if (high < 0 || low < 0)
			return 0;
		seed[index] = (unsigned char)(high << 4 | low);
	}
	text += 2 * SERVER_MASTER_SEED_SIZE;
	return !*text || *text == ' ' || *text == '\t' || *text == '\r' || *text == '\n' || *text == '#';
}

/* the key file's path: beside config.toml unless a full path */
static void key_file_path(char *path, int size)
{
	const char *setting = config_string("server.publisher_key_file");
	char folder[1024];

	if (setting[0] == '/' || (setting[0] && setting[1] == ':'))
	{
		snprintf(path, (size_t)size, "%s", setting);
		return;
	}
	config_folder(folder, sizeof(folder));
	snprintf(path, (size_t)size, "%s%s", folder, setting[0] ? setting : "publisher.key");
}

/* the seed: HALO_SERVER_PUBLISHER_KEY's, else the file's first line that is
not a comment; 1 */
static int publisher_seed(unsigned char *seed)
{
	const char *environment = getenv("HALO_SERVER_PUBLISHER_KEY");
	char path[1100];
	char *text;
	size_t size;
	const char *line;
	int found = 0;

	if (environment && environment[0])
		return seed_from_text(environment, seed);
	key_file_path(path, sizeof(path));
	text = config_file_read(path, &size);
	if (!text)
		return 0;
	for (line = text; *line && !found;)
	{
		const char *end = line + strcspn(line, "\n");

		while (*line == ' ' || *line == '\t')
			line++;
		if (*line != '#' && line < end)
			found = seed_from_text(line, seed);
		line = *end ? end + 1 : end;
	}
	memset(text, 0, size);
	free(text);
	return found;
}

static void key_id_of(const unsigned char *public_key, char *key_id)
{
	unsigned char digest[P2P_SHA256_SIZE];

	p2p_sha256(public_key, SERVER_MASTER_PUBLIC_KEY_SIZE, digest);
	p2p_hex(digest, SERVER_MASTER_KEY_ID_SIZE, key_id);
}

int server_master_publisher(unsigned char *public_key, char *key_id)
{
	unsigned char seed[SERVER_MASTER_SEED_SIZE];

	if (!publisher_seed(seed))
		return 0;
	p2p_ed25519_public(seed, public_key, NULL);
	memset(seed, 0, sizeof(seed));
	key_id_of(public_key, key_id);
	return 1;
}

/* a signature by the publisher key: 1 */
static int publisher_sign(const void *message, int size, unsigned char *signature)
{
	unsigned char seed[SERVER_MASTER_SEED_SIZE];
	unsigned char public_key[SERVER_MASTER_PUBLIC_KEY_SIZE];

	if (!publisher_seed(seed))
		return 0;
	p2p_ed25519_public(seed, public_key, NULL);
	p2p_ed25519_sign(seed, public_key, message, size, signature);
	memset(seed, 0, sizeof(seed));
	return 1;
}

int server_master_keygen(void)
{
	unsigned char seed[SERVER_MASTER_SEED_SIZE];
	unsigned char public_key[SERVER_MASTER_PUBLIC_KEY_SIZE];
	char key_id[2 * SERVER_MASTER_KEY_ID_SIZE + 1];
	char hex[2 * SERVER_MASTER_SEED_SIZE + 1];
	char path[1100];
	char text[512];
	FILE *file;
	int written;

	key_file_path(path, sizeof(path));
	if (getenv("HALO_SERVER_PUBLISHER_KEY") && getenv("HALO_SERVER_PUBLISHER_KEY")[0])
	{
		if (!publisher_seed(seed))
		{
			fprintf(stderr, "HALO_SERVER_PUBLISHER_KEY is not 64 hexadecimal digits\n");
			return 0;
		}
	}
	else if (!publisher_seed(seed))
	{
		file = fopen(path, "rb");
		if (file)
		{
			fclose(file);
			fprintf(stderr, "%s is there but holds no key (64 hexadecimal digits): move it aside to make a new one\n",
				path);
			return 0;
		}
		posix_random_bytes(seed, sizeof(seed));
		p2p_hex(seed, SERVER_MASTER_SEED_SIZE, hex);
		snprintf(text, sizeof(text),
			"# The publisher key of this server's operator for the OpenCE master servers (halo --server\n"
			"# --keygen). Keep it secret: whoever has it publishes as you. Its public key is in the\n"
			"# application you make to a master.\n%s\n", hex);
		memset(hex, 0, sizeof(hex));
		/* (only its owner may read it, from its creation: no moment when the
		key is in a file others can read) */
		written = posix_server_write_private_file(path, text, (unsigned long)strlen(text));
		memset(text, 0, sizeof(text));
		if (!written)
		{
			fprintf(stderr, "cannot write %s\n", path);
			return 0;
		}
		fprintf(stdout, "wrote a new publisher key to %s\n", path);
	}
	else
	{
		fprintf(stdout, "the publisher key in %s:\n", path);
	}
	p2p_ed25519_public(seed, public_key, NULL);
	memset(seed, 0, sizeof(seed));
	key_id_of(public_key, key_id);
	p2p_hex(public_key, SERVER_MASTER_PUBLIC_KEY_SIZE, hex);
	fprintf(stdout, "public key: ed25519:%s\nkey id: %s\n", hex, key_id);
	return 1;
}

int server_master_sign_text(const char *text)
{
	unsigned char signature[P2P_SIGNATURE_SIZE];
	char hex[2 * P2P_SIGNATURE_SIZE + 1];

	if (!publisher_sign(text, (int)strlen(text), signature))
	{
		fprintf(stderr, "there is no publisher key (halo --server --keygen makes one)\n");
		return 0;
	}
	p2p_hex(signature, P2P_SIGNATURE_SIZE, hex);
	fprintf(stdout, "%s\n", hex);
	return 1;
}

#else

int server_master_keygen(void)
{
	return 0;
}

int server_master_sign_text(const char *text)
{
	(void)text;
	return 0;
}

int server_master_publisher(unsigned char *public_key, char *key_id)
{
	(void)public_key;
	(void)key_id;
	return 0;
}

#endif
