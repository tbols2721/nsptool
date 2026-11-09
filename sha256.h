#ifndef SHA256_H
#define SHA256_H
#include <stddef.h>
#include <stdint.h>

/* One-shot SHA-256 of a buffer; writes 32 bytes to out. */
void sha256(const void *data, size_t len, uint8_t out[32]);

#endif
