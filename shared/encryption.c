#include "shared.h"
#include "configuration.h"
#include "logging.h"
#include "ipc.h"
#include "node.h"
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sodium.h>
#include <stdbool.h>

bool sodium_init_done = false;
int init_sodium(void);

int open_encryption_key(const char * path, unsigned char * target, size_t size){
	FILE *f;
	size_t read;

	f = fopen(path, "r");
	if (f == NULL) {
		lerr("Failed to open encryption file for writing");
		return -1;
	}

	read = fread(target, size, 1, f);
	if (read != 1) {
		lerr("Could not read encryption key");
		return -1;
	}

	if (fclose(f) != 0) {
		lerr("Failed open encryption file stream");
		return -1;
	}
	return 0;
}

int init_sodium() {
	if (sodium_init_done == false) {
		if (sodium_init() < 0) {
			lwarn("sodium_init failed\n");
			return -1;
		} else {
			sodium_init_done = true;
		}
	}
	return 0;
}

int encrypt_pkt(merlin_event * pkt, merlin_node * recv) {
	ldebug("Encrypting pkt for node: %s", recv->name);
	if (init_sodium() == -1) {
		return -1;
	}

	randombytes_buf(pkt->hdr.nonce, sizeof(pkt->hdr.nonce));

	if (crypto_box_detached_afternm(
				(unsigned char *)pkt->body, pkt->hdr.authtag,
				(unsigned char *)pkt->body, pkt->hdr.len,
				pkt->hdr.nonce, recv->sharedkey) != 0) {
		lerr("could not encrypt msg!\n");
		return -1;
	}

	ldebug("Pkt encryption for node: %s succeeded", recv->name);

	return 0;
}

int decrypt_pkt(merlin_event * pkt, merlin_node * sender) {
	ldebug("Decrypting pkt from node: %s", sender->name);
	if (init_sodium() == -1) {
		return -1;
	}

	if (crypto_box_open_detached_afternm(
				(unsigned char *)pkt->body,
				(const unsigned char *)pkt->body,
				pkt->hdr.authtag, pkt->hdr.len, pkt->hdr.nonce,
				sender->sharedkey) != 0) {
		lerr("Encrypted message forged!\n");
		return -1;
	}

	ldebug("Pkt decryption from node: %s succeeded", sender->name);

	return 0;
}


