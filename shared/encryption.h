#ifndef INCLUDE_encryption_h__
#define INCLUDE_encryption_h__

#include "shared.h"

int encrypt_pkt(merlin_event * pkt, merlin_node * sender);
int decrypt_pkt(merlin_event * pkt, merlin_node * recv);
int open_encryption_key(const char * path, unsigned char * target, size_t size);

#endif
