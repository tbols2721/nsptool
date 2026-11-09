#ifndef OSPKG_H
#define OSPKG_H
#include <stddef.h>
#include <stdint.h>

/* What a CX II OS package (.tco2/.tcc2/.tct2) says about itself. */
struct ospkg_info {
	char ext[16];        /* e.g. ".tcc2", from the manifest's first line */
	unsigned major, minor, build;  /* same encoding the calculator reports */
};

/*
 * Check a CX II OS package: every file in the zip must be stored, listed in
 * manifest.txt and match its SHA-256. Problems are printed to stderr.
 * Returns 0 if everything matches, 1 if any check fails, and -1 if the data
 * is not a zip package at all (e.g. an older .tno/.tco file).
 * The manifest's signature is not checked here; the calculator does that.
 */
int ospkg_verify(const uint8_t *data, size_t size, struct ospkg_info *info);

#endif
