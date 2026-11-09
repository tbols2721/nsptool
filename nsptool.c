/* Minimal libnspire CLI for TI-Nspire calculators over USB. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <nspire.h>
#include "ospkg.h"

static int fail(const char *what, int ret) {
	fprintf(stderr, "%s failed: %s (%d)\n", what, nspire_strerror(ret), ret);
	return 1;
}

static const char *hw_name(int t) {
	switch (t) {
	case NSPIRE_CAS: return "CAS (classic)";
	case NSPIRE_NONCAS: return "non-CAS (classic)";
	case NSPIRE_CASCX: return "CX CAS";
	case NSPIRE_NONCASCX: return "CX non-CAS";
	default: return "unknown";
	}
}

/* The calculator reports e.g. 6.4.0.74 as major 6, minor 40, build 74. */
static void print_version(const char *label, struct nspire_devinfo *i, int idx) {
	printf("%-12s: %u.%u.%u.%u\n", label, i->versions[idx].major,
	       i->versions[idx].minor / 10, i->versions[idx].minor % 10,
	       i->versions[idx].build);
}

static int confirm(const char *prompt) {
	char answer[16] = {0};
	printf("%s Type YES to continue: ", prompt);
	fflush(stdout);
	if (!fgets(answer, sizeof(answer), stdin) || strncmp(answer, "YES", 3)) {
		printf("Aborted.\n");
		return 0;
	}
	return 1;
}

static void *read_file(const char *path, size_t *size) {
	FILE *f = fopen(path, "rb");
	if (!f) { perror(path); return NULL; }
	fseek(f, 0, SEEK_END);
	*size = ftell(f);
	rewind(f);
	void *buf = malloc(*size ? *size : 1);
	if (!buf || fread(buf, 1, *size, f) != *size) {
		fprintf(stderr, "%s: read failed\n", path);
		free(buf);
		buf = NULL;
	}
	fclose(f);
	return buf;
}

static int info(nspire_handle_t *h) {
	struct nspire_devinfo i;
	int ret = nspire_device_info(h, &i);
	if (ret) return fail("device_info", ret);
	printf("Device name : %s\n", i.device_name);
	printf("Electronic ID: %s\n", i.electronic_id);
	printf("HW type     : 0x%02x %s\n", i.hw_type, hw_name(i.hw_type));
	print_version("OS version", &i, NSPIRE_VER_OS);
	print_version("Boot1", &i, NSPIRE_VER_BOOT1);
	print_version("Boot2", &i, NSPIRE_VER_BOOT2);
	printf("OS file ext : %s\n", i.extensions.os);
	printf("Doc file ext: %s\n", i.extensions.file);
	printf("Battery     : 0x%02x%s\n", i.batt.status, i.batt.is_charging ? " (charging)" : "");
	printf("Storage free: %llu / %llu bytes\n",
	       (unsigned long long)i.storage.free, (unsigned long long)i.storage.total);
	printf("Run level   : %d\n", i.runlevel);
	return 0;
}

static int cmp_version(unsigned a1, unsigned a2, unsigned a3,
                       unsigned b1, unsigned b2, unsigned b3) {
	if (a1 != b1) return a1 < b1 ? -1 : 1;
	if (a2 != b2) return a2 < b2 ? -1 : 1;
	if (a3 != b3) return a3 < b3 ? -1 : 1;
	return 0;
}

/* Checks before sending an OS: right model, intact package, not a downgrade. */
static int os_preflight(nspire_handle_t *h, const char *path, const uint8_t *buf, size_t size) {
	struct nspire_devinfo i;
	int ret = nspire_device_info(h, &i);
	if (ret) return fail("device_info", ret);

	const char *want = i.extensions.os;
	const char *file_ext = strrchr(path, '.');
	printf("Calculator  : %s, OS %u.%u.%u.%u, takes %s files\n", i.device_name,
	       i.versions[NSPIRE_VER_OS].major, i.versions[NSPIRE_VER_OS].minor / 10,
	       i.versions[NSPIRE_VER_OS].minor % 10, i.versions[NSPIRE_VER_OS].build, want);
	if (!file_ext || strcasecmp(file_ext, want)) {
		fprintf(stderr, "Refusing: %s is not a %s file, so it is for a different model.\n",
		        path, want);
		return 1;
	}

	struct ospkg_info pkg = {0};
	ret = ospkg_verify(buf, size, &pkg);
	if (ret < 0) {
		/* CX II packages are zips; older models use other formats. */
		if (want[0] && want[strlen(want) - 1] == '2') {
			fprintf(stderr, "Refusing: %s is not a valid CX II OS package.\n", path);
			return 1;
		}
		printf("Package     : not a zip package; contents cannot be verified here\n");
		return 0;
	}
	if (ret) {
		fprintf(stderr, "Refusing: %s failed verification (corrupt or modified).\n", path);
		return 1;
	}
	printf("Package     : OS %u.%u.%u.%u for %s files, all checksums match\n",
	       pkg.major, pkg.minor / 10, pkg.minor % 10, pkg.build, pkg.ext);
	if (strcasecmp(pkg.ext, want)) {
		fprintf(stderr, "Refusing: package is for %s calculators, not %s.\n", pkg.ext, want);
		return 1;
	}

	int c = cmp_version(pkg.major, pkg.minor, pkg.build, i.versions[NSPIRE_VER_OS].major,
	                    i.versions[NSPIRE_VER_OS].minor, i.versions[NSPIRE_VER_OS].build);
	if (c < 0) {
		fprintf(stderr, "Refusing: package is older than the installed OS; "
		        "TI calculators reject downgrades.\n");
		return 1;
	}
	if (c == 0)
		printf("Note        : this is the same version that is already installed.\n");
	return 0;
}

static int osinstall(nspire_handle_t *h, const char *path) {
	size_t size;
	void *buf = read_file(path, &size);
	if (!buf) return 1;

	if (os_preflight(h, path, buf, size)) { free(buf); return 1; }

	char prompt[512];
	snprintf(prompt, sizeof(prompt),
	         "About to send %zu bytes from %s as an OS update.", size, path);
	if (!confirm(prompt)) { free(buf); return 1; }

	printf("Sending... (do not unplug the calculator)\n");
	int ret = nspire_os_send(h, buf, size);
	free(buf);
	if (ret) return fail("os_send", ret);
	printf("Transfer complete; the calculator should now install and reboot.\n");
	return 0;
}

static int ls(nspire_handle_t *h, const char *path) {
	struct nspire_dir_info *d;
	int ret = nspire_dirlist(h, path, &d);
	if (ret) return fail("ls", ret);
	for (unsigned long n = 0; n < d->num; n++)
		printf("%s %10lu  %s\n", d->items[n].type == NSPIRE_DIR ? "d" : "-",
		       d->items[n].size, d->items[n].name);
	nspire_dirlist_free(d);
	return 0;
}

static int put(nspire_handle_t *h, const char *local, const char *remote) {
	size_t size;
	void *buf = read_file(local, &size);
	if (!buf) return 1;
	int ret = nspire_file_write(h, remote, buf, size);
	free(buf);
	if (ret) return fail("put", ret);
	printf("%s -> %s (%zu bytes)\n", local, remote, size);
	return 0;
}

static int mkdir_cmd(nspire_handle_t *h, const char *path) {
	int ret = nspire_dir_create(h, path);
	if (ret == -NSPIRE_ERR_EXISTS) return 0;
	if (ret) return fail("mkdir", ret);
	return 0;
}

static int rm(nspire_handle_t *h, const char *path) {
	int ret = nspire_file_delete(h, path);
	if (ret) return fail("rm", ret);
	return 0;
}

/*
 * Some files report a different size than they read as (NspireLogs.zip is
 * generated on each read), so start from the listed size and retry with a
 * bigger buffer if the read turns out longer. This relies on patches/0001,
 * which stops nspire_file_read() from writing past the buffer.
 */
static int fetch(nspire_handle_t *h, const char *remote, uint8_t **out, size_t *len) {
	struct nspire_dir_item attr;
	int ret = nspire_attr(h, remote, &attr);
	if (ret) return ret;

	size_t cap = attr.size;
	for (int attempt = 0; attempt < 3; attempt++) {
		size_t got;
		uint8_t *buf = malloc(cap ? cap : 1);
		if (!buf) return -NSPIRE_ERR_NOMEM;
		ret = nspire_file_read(h, remote, buf, cap, &got);
		if (ret) { free(buf); return ret; }
		if (got <= cap) {
			*out = buf;
			*len = got;
			return 0;
		}
		free(buf);
		cap = got + got / 8 + 4096;
	}
	return -NSPIRE_ERR_INVALID;
}

static int write_file(const char *path, const void *buf, size_t size) {
	FILE *f = fopen(path, "wb");
	if (!f) { perror(path); return 1; }
	int bad = fwrite(buf, 1, size, f) != size;
	if (fclose(f) || bad) { perror(path); return 1; }
	return 0;
}

static int get(nspire_handle_t *h, const char *remote, const char *local) {
	uint8_t *buf;
	size_t size;
	int ret = fetch(h, remote, &buf, &size);
	if (ret) return fail("get", ret);
	ret = write_file(local, buf, size);
	free(buf);
	if (!ret) printf("%s -> %s (%zu bytes)\n", remote, local, size);
	return ret;
}

struct backup_stats { unsigned files, errors; size_t bytes; };

static void backup_dir(nspire_handle_t *h, const char *remote, const char *local,
                       struct backup_stats *st) {
	struct nspire_dir_info *d;
	int ret = nspire_dirlist(h, remote, &d);
	if (ret) {
		fprintf(stderr, "ls %s failed: %s\n", remote, nspire_strerror(ret));
		st->errors++;
		return;
	}
	for (unsigned long n = 0; n < d->num; n++) {
		const char *name = d->items[n].name;
		/* Names become local paths, so never let one escape the backup dir. */
		if (!strcmp(name, ".") || !strcmp(name, "..") || strchr(name, '/')) {
			fprintf(stderr, "skipping unsafe name %s in %s\n", name, remote);
			st->errors++;
			continue;
		}
		char rpath[1024], lpath[4096];
		snprintf(rpath, sizeof(rpath), "%s%s%s", remote,
		         remote[strlen(remote) - 1] == '/' ? "" : "/", name);
		snprintf(lpath, sizeof(lpath), "%s/%s", local, name);

		if (d->items[n].type == NSPIRE_DIR) {
			if (mkdir(lpath, 0755) && errno != EEXIST) {
				perror(lpath);
				st->errors++;
				continue;
			}
			backup_dir(h, rpath, lpath, st);
			continue;
		}

		uint8_t *buf;
		size_t size;
		if ((ret = fetch(h, rpath, &buf, &size))) {
			fprintf(stderr, "get %s failed: %s\n", rpath, nspire_strerror(ret));
			st->errors++;
			continue;
		}
		if (write_file(lpath, buf, size)) {
			st->errors++;
		} else {
			printf("  %s (%zu bytes)\n", rpath, size);
			st->files++;
			st->bytes += size;
		}
		free(buf);
	}
	nspire_dirlist_free(d);
}

static int backup(nspire_handle_t *h, const char *dir) {
	char name[64];
	if (!dir) {
		time_t now = time(NULL);
		strftime(name, sizeof(name), "nspire-backup-%Y%m%d-%H%M%S", localtime(&now));
		dir = name;
	}
	if (mkdir(dir, 0755)) {
		if (errno == EEXIST)
			fprintf(stderr, "%s already exists; choose a new folder.\n", dir);
		else
			perror(dir);
		return 1;
	}
	printf("Backing up My Documents to %s/\n", dir);
	struct backup_stats st = {0};
	backup_dir(h, "/", dir, &st);
	printf("Backed up %u files (%zu bytes) to %s/", st.files, st.bytes, dir);
	if (st.errors) {
		printf(" with %u ERRORS; see messages above.\n", st.errors);
		return 1;
	}
	printf("\n");
	return 0;
}

static int usage(const char *argv0, FILE *stream) {
	fprintf(stream,
	        "usage: %s <command> [args]\n"
	        "  info                     model, OS/boot versions, storage (read-only)\n"
	        "  ls <dir>                 list a directory; / is My Documents (read-only)\n"
	        "  get <remote> <local>     copy a file from the calculator\n"
	        "  backup [dir]             copy all of My Documents to dir (default:\n"
	        "                           nspire-backup-<date>-<time>; must not exist)\n"
	        "  put <local> <remote>     copy a file to the calculator\n"
	        "  mkdir <dir>              create a directory (no error if it exists)\n"
	        "  rm <file>                delete a file\n"
	        "  osinstall <file>         check and send an OS update (asks first)\n",
	        argv0);
	return 2;
}

int main(int argc, char **argv) {
	static const struct { const char *name; int min, max; } cmds[] = {
		{"info", 0, 0}, {"ls", 1, 1}, {"get", 2, 2}, {"backup", 0, 1}, {"put", 2, 2},
		{"mkdir", 1, 1}, {"rm", 1, 1}, {"osinstall", 1, 1},
	};
	setvbuf(stdout, NULL, _IOLBF, 0);  /* keep stdout and stderr in order */
	if (argc < 2) return usage(argv[0], stderr);
	if (argc == 2 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))) {
		usage(argv[0], stdout);
		return 0;
	}
	int known = 0;
	for (size_t n = 0; n < sizeof(cmds) / sizeof(cmds[0]); n++)
		if (!strcmp(argv[1], cmds[n].name)) {
			if (argc - 2 < cmds[n].min || argc - 2 > cmds[n].max) return usage(argv[0], stderr);
			known = 1;
		}
	if (!known) return usage(argv[0], stderr);

	nspire_handle_t *h;
	int ret = nspire_init(&h);
	if (ret) return fail("init", ret);

	int rc;
	if (!strcmp(argv[1], "info"))
		rc = info(h);
	else if (!strcmp(argv[1], "ls"))
		rc = ls(h, argv[2]);
	else if (!strcmp(argv[1], "get"))
		rc = get(h, argv[2], argv[3]);
	else if (!strcmp(argv[1], "backup"))
		rc = backup(h, argc > 2 ? argv[2] : NULL);
	else if (!strcmp(argv[1], "put"))
		rc = put(h, argv[2], argv[3]);
	else if (!strcmp(argv[1], "mkdir"))
		rc = mkdir_cmd(h, argv[2]);
	else if (!strcmp(argv[1], "rm"))
		rc = rm(h, argv[2]);
	else
		rc = osinstall(h, argv[2]);
	nspire_free(h);
	return rc;
}
