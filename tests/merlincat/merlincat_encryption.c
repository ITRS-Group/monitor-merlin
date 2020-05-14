#include "merlincat_encryption.h"
#include <shared/shared.h>
#include <naemon/naemon.h>
#include <glib.h>
#include <sodium.h>

static int open_encryption_key_cukemerlin(char * path, unsigned char * target, size_t size);
static int init_sodium(void);
static bool sodium_init_done = false;

int init_sodium() {
	if (sodium_init_done == false) {
		if (sodium_init() < 0) {
			g_message("sodium_init failed");
			return -1;
		} else {
			sodium_init_done = true;
		}
	}
	return 0;
}

static int encryption_enabled() {
	if (getenv("MERLIN_ENCRYPTED") != NULL && strcmp(getenv("MERLIN_ENCRYPTED"),"TRUE") == 0) {
		return 1;
	} else {
		return 0;
	}
}

static int open_encryption_key_cukemerlin(char * path, unsigned char * target, size_t size){
	FILE *f;
	size_t read;

	f = fopen(path, "r");
	if (f == NULL) {
		return -1;
	}

	read = fread(target, size, 1, f);
	if (read != 1) {
		return -1;
	}

	if (fclose(f) != 0) {
		return -1;
	}
	return 0;
}

int merlincat_encrypt_pkt(merlin_event * pkt) {
	unsigned char pubkey[crypto_box_PUBLICKEYBYTES];
	unsigned char privkey[crypto_box_SECRETKEYBYTES];

	/* Only decrypt packet if enabled */
	if (!encryption_enabled()) {
		return 0;
	}

	if (*pkt->hdr.nonce != '\0') {
		g_message("encrypt_pkt: pkt already encrypted");
		return 0;
	}

	if (init_sodium() == -1) {
		g_message("encrypt_pkt: could not init sodium");
		return -1;
	}

	if ( open_encryption_key_cukemerlin( getenv("MERLIN_PRIVKEY"), privkey,
				crypto_box_SECRETKEYBYTES) ) {
		g_message("encrypt_pkt: could not open privatekey");
		return -1;
	}

	if ( open_encryption_key_cukemerlin( getenv("MERLIN_PUBKEY"), pubkey,
				crypto_box_PUBLICKEYBYTES) ) {
		g_message("encrypt_pkt: could not open pubkey");
		return -1;
	}

	randombytes_buf(pkt->hdr.nonce, sizeof(pkt->hdr.nonce));

	if (crypto_box_detached((unsigned char *)pkt->body, pkt->hdr.authtag, (unsigned char *)pkt->body, pkt->hdr.len, pkt->hdr.nonce, pubkey, privkey) != 0) {
		g_message("encrypt_pkt: could not encrypt pkt");
		return 0;
	}
	return 0;
}

int merlincat_decrypt_pkt(merlin_event * pkt) {
	unsigned char pubkey[crypto_box_PUBLICKEYBYTES];
	unsigned char privkey[crypto_box_SECRETKEYBYTES];

	/* Only decrypt packet if enabled */
	if (!encryption_enabled()) {
		return 0;
	}

	if (*pkt->hdr.nonce == '\0') {
		g_message("decrypt_pkt: nonce empty");
	}

	if (init_sodium() == -1) {
		g_message("decrypt_pkt: could not init sodium");
		return -1;
	}

	if ( open_encryption_key_cukemerlin( getenv("MERLIN_PRIVKEY"), privkey,
				crypto_box_SECRETKEYBYTES) ) {
		g_message("decrypt_pkt: could not open privatekey");
		return -1;
	}

	if ( open_encryption_key_cukemerlin( getenv("MERLIN_PUBKEY"), pubkey,
				crypto_box_PUBLICKEYBYTES) ) {
		g_message("decrypt_pkt: could not open pubkey");
		return -1;
	}

	if (crypto_box_open_detached((unsigned char *)pkt->body, (const unsigned char *)pkt->body, pkt->hdr.authtag, pkt->hdr.len, pkt->hdr.nonce, pubkey, privkey) != 0) {
		return -1;
	}

	*pkt->hdr.nonce = '\0';

	return 0;
}

