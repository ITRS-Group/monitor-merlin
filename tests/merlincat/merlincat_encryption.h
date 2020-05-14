#ifndef TOOLS_MERLINCAT_ENCRYPTION_H_
#define TOOLS_MERLINCAT_ENCRYPTION_H_

#include <shared/node.h>
#include <naemon/naemon.h>
#include <sodium.h>

/**
 * Encrypts a merlin packet in place, if enabled using envrioment variables.
 *
 * Returns 0 on success and -1 otherwise
 */
int merlincat_encrypt_pkt(merlin_event * pkt);

/**
 * Decrypts a merlin packet in place, if enabled using envrioment variables.
 *
 * Returns 0 on success and -1 otherwise
 */
int merlincat_decrypt_pkt(merlin_event * pkt);

#endif /* TOOLS_MERLINCAT_ENCRYPTION_H_ */
