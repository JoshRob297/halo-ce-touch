/*
SERVER_MASTER.H

A dedicated server's publishing to the official master servers
(server_master.c; docs/master_protocol.md): its operator's publisher key
(publisher.key, made by halo --server --keygen), the masters of masters.txt,
the state of the game published to each every 30 seconds and at once when
it changes, signed with the key, and the masters' reachability probes
answered on the tunnel's port. It runs on a thread of its own: a slow master
never holds the game.
*/

#ifndef __HALO_LINUX_SERVER_MASTER_H
#define __HALO_LINUX_SERVER_MASTER_H

enum
{
	SERVER_MASTER_PUBLIC_KEY_SIZE = 32,
	SERVER_MASTER_SEED_SIZE = 32,
	SERVER_MASTER_KEY_ID_SIZE = 8,
	/* "ed25519:" and the key in hex */
	SERVER_MASTER_PUBLIC_KEY_TEXT_SIZE = 8 + 64 + 1,
	SERVER_MASTER_MAXIMUM_MASTERS = 4,
};

/* --keygen: a new publisher key written (unless there is one), its public
key and id printed; 1 on success */
int server_master_keygen(void);
/* --sign <text>: the key's signature of the text printed in hex; 1 */
int server_master_sign_text(const char *text);

/* the publisher key (HALO_SERVER_PUBLISHER_KEY, else the file): 1 with its
public key and id (16 hex) */
int server_master_publisher(unsigned char *public_key, char *key_id);

#endif
