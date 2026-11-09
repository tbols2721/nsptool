#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ospkg.h"
#include "sha256.h"

#define MAX_ENTRIES 64

struct entry {
	char name[128];
	const uint8_t *data;
	size_t size;
	int covered;
};

static uint32_t le32(const uint8_t *p) {
	return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t le16(const uint8_t *p) {
	return p[0] | p[1] << 8;
}

/* Walk the zip's local file headers. TI's packages store files uncompressed. */
static int read_entries(const uint8_t *data, size_t size, struct entry *e, int *count) {
	size_t off = 0;
	*count = 0;
	while (off + 30 <= size && le32(data + off) == 0x04034b50) {
		uint16_t flags = le16(data + off + 6), method = le16(data + off + 8);
		uint32_t csize = le32(data + off + 18);
		uint16_t name_len = le16(data + off + 26), extra_len = le16(data + off + 28);
		size_t start = off + 30 + name_len + extra_len;

		if (*count == MAX_ENTRIES || name_len >= sizeof(e->name)) {
			fprintf(stderr, "package: too many files or name too long\n");
			return 1;
		}
		if (start > size || csize > size - start) {
			fprintf(stderr, "package: truncated\n");
			return 1;
		}
		struct entry *cur = &e[(*count)++];
		memcpy(cur->name, data + off + 30, name_len);
		cur->name[name_len] = '\0';
		if (method != 0 || (flags & 0x08)) {
			fprintf(stderr, "package: %s is compressed or streamed; cannot verify\n", cur->name);
			return 1;
		}
		cur->data = data + start;
		cur->size = csize;
		cur->covered = 0;
		off = start + csize;
	}
	if (*count == 0)
		return -1;
	return 0;
}

static struct entry *find(struct entry *e, int count, const char *name) {
	for (int i = 0; i < count; i++)
		if (!strcmp(e[i].name, name))
			return &e[i];
	return NULL;
}

int ospkg_verify(const uint8_t *data, size_t size, struct ospkg_info *info) {
	struct entry e[MAX_ENTRIES];
	int count, ret = read_entries(data, size, e, &count);
	if (ret)
		return ret;

	struct entry *m = find(e, count, "manifest.txt");
	if (!m) {
		fprintf(stderr, "package: no manifest.txt\n");
		return 1;
	}
	m->covered = 1;
	struct entry *sig = find(e, count, "manifest.sig");
	if (sig)
		sig->covered = 1;  /* signs the manifest; checked by the calculator */

	/*
	 * manifest.txt: "# TI-Nspire.tcc2", OS version, another version field,
	 * then pairs of "<file>" and "<sha256 hex>", separated by whitespace.
	 */
	char *text = malloc(m->size + 1);
	if (!text)
		return 1;
	memcpy(text, m->data, m->size);
	text[m->size] = '\0';

	int failures = 0, field = 0;
	char *name = NULL, *save;
	for (char *tok = strtok_r(text, " \t\r\n", &save); tok;
	     tok = strtok_r(NULL, " \t\r\n", &save), field++) {
		if (field == 0) {
			if (strcmp(tok, "#")) break;
		} else if (field == 1) {
			const char *dot = strrchr(tok, '.');
			snprintf(info->ext, sizeof(info->ext), "%s", dot ? dot : "");
		} else if (field == 2) {
			if (sscanf(tok, "%u.%u.%u", &info->major, &info->minor, &info->build) != 3)
				break;
		} else if (field == 3) {
			/* Second version field; meaning not documented, so not used. */
		} else if (!name) {
			name = tok;
		} else {
			struct entry *f = find(e, count, name);
			if (!f) {
				fprintf(stderr, "package: %s listed in manifest but missing\n", name);
				failures++;
			} else {
				uint8_t digest[32];
				char hex[65];
				sha256(f->data, f->size, digest);
				for (int i = 0; i < 32; i++)
					sprintf(hex + 2 * i, "%02x", digest[i]);
				f->covered = 1;
				if (strcasecmp(hex, tok)) {
					fprintf(stderr, "package: %s checksum mismatch\n", name);
					failures++;
				}
			}
			name = NULL;
		}
	}
	free(text);

	if (field < 4 || !info->ext[0]) {
		fprintf(stderr, "package: manifest.txt not in the expected format\n");
		return 1;
	}
	for (int i = 0; i < count; i++)
		if (!e[i].covered) {
			fprintf(stderr, "package: %s is not covered by the manifest\n", e[i].name);
			failures++;
		}
	return failures ? 1 : 0;
}
