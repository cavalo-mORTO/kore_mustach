/*
 * Copyright (c) 2021 Miguel Rodrigues <miguelangelorodrigues@enta.pt>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define _GNU_SOURCE
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <kore/kore.h>
#include "mustach.h"
#include "kore_mustach.h"

#if defined(__linux__)
#include <kore/seccomp.h>

static struct sock_filter filter_mustach[] = {
    KORE_SYSCALL_ALLOW(newfstatat),
};
#endif

struct cache {
    uint32_t        refs;
    char            *data;
    size_t          len;
};

struct asset {
    char            *path;
    struct cache    *cache;

    TAILQ_ENTRY(asset) list;
};

static TAILQ_HEAD(, asset)  assets;
static TAILQ_HEAD(, lambda) lambdas;
static int                  tailq_init = 0;

static struct cache     *cache_create(void);
static void             cache_ref_drop(struct cache **);
static struct asset     *asset_get(const char *);
static struct asset     *asset_create(const char *);
static void             asset_remove(struct asset *);
static void             file_open(const char *, int, int *);
static void             file_close(int);
static void             file_read(int, char **, size_t *);
static int              dir_exists(const char *);
static int              file_exists(const char *);
static void             find_files(const char *, void (*)(char *, struct dirent *));
static void             file_cb(char *, struct dirent *);
static void             releasecb(const char *, void *);

void
kore_mustach_sys_init(void)
{
    TAILQ_INIT(&assets);
    TAILQ_INIT(&lambdas);
    tailq_init = 1;

#if defined(__linux__)
    kore_seccomp_filter("mustach", filter_mustach,
            KORE_FILTER_LEN(filter_mustach));
#endif
}

void
kore_mustach_sys_cleanup(void)
{
    struct asset    *a;
    struct lambda   *l;

    if (!tailq_init) {
        kore_log(LOG_NOTICE, "(%s): must run kore_mustach_sys_init()", __func__);
        return;
    }

    while ((a = TAILQ_FIRST(&assets)) != NULL)
        asset_remove(a);

    while ((l = TAILQ_FIRST(&lambdas)) != NULL) {
        TAILQ_REMOVE(&lambdas, l, list);
        kore_free(l->name);
        kore_free(l);
    }
}

int
kore_mustach_bind_partials(const char *fpath[], size_t len)
{
    size_t i;

    if (!tailq_init) {
        kore_log(LOG_NOTICE, "(%s): must run kore_mustach_sys_init()", __func__);
        return (KORE_RESULT_ERROR);
    }

    for (i = 0; i < len; i++) {
        if (dir_exists(fpath[i]))
            find_files(fpath[i], file_cb);
        else
            asset_create(fpath[i]);
    }
    return (KORE_RESULT_OK);
}

int
kore_mustach_bind_lambdas(struct lambda lambda[], size_t len)
{
    struct lambda   *l;
    size_t i;

    if (!tailq_init) {
        kore_log(LOG_NOTICE, "(%s): must run kore_mustach_sys_init()", __func__);
        return (KORE_RESULT_ERROR);
    }

    for (i = 0; i < len; i++) {
        l = kore_calloc(1, sizeof(*l));
        l->name = kore_strdup(lambda[i].name);
        l->cb = lambda[i].cb;
        TAILQ_INSERT_TAIL(&lambdas, l, list);
    }
    return (KORE_RESULT_OK);
}

int
kore_mustach_partial(const char *name, struct mustach_sbuf *sbuf)
{
    struct asset    *a;

    if (!tailq_init) {
        kore_log(LOG_NOTICE, "(%s): must run kore_mustach_sys_init()", __func__);
        return (MUSTACH_OK);
    }

    a = asset_get(name);
    if (a != NULL) {
        sbuf->value = a->cache->data;
        sbuf->length = a->cache->len;

        sbuf->releasecb = releasecb;
        sbuf->closure = a;
    }

    return (MUSTACH_OK);
}

int
kore_mustach_lambda(const char *name, struct kore_buf *buf)
{
    struct lambda *l;

    if (!tailq_init) {
        kore_log(LOG_NOTICE, "(%s): must run kore_mustach_sys_init()", __func__);
        return (MUSTACH_OK);
    }

    l = NULL;
    TAILQ_FOREACH(l, &lambdas, list) {
        if (!strcmp(name, l->name))
            break;
    }

    if (l != NULL)
        l->cb(buf);

    return (MUSTACH_OK);
}

void
releasecb(const char *value, void *closure)
{
    struct asset  *a = closure;

    (void)value; /* unused */

    cache_ref_drop(&a->cache);
}

struct asset *
asset_get(const char *name)
{
    struct asset *a;
    int fd;

    a = NULL;
    TAILQ_FOREACH(a, &assets, list) {
        if (!strcmp(a->path, name))
            break;
    }

    if (a == NULL)
        return (NULL);

    if (a->cache == NULL) {
        a->cache = cache_create();
        file_open(a->path, O_RDONLY, &fd);
        file_read(fd, &a->cache->data, &a->cache->len);
        file_close(fd);
    }

    a->cache->refs++;
    return (a);
}

struct cache *
cache_create(void)
{
    struct cache *c;

    c = kore_calloc(1, sizeof(*c));
    c->refs++;

    return (c);
}

void
cache_ref_drop(struct cache **ptr)
{
    struct cache *c = *ptr;

    c->refs--;
    if (c->refs == 0) {
        kore_free(c->data);
        kore_free(c);
        *ptr = NULL;
    }
}

struct asset *
asset_create(const char *fpath)
{
    struct asset *a;

    if (!file_exists(fpath))
        return (NULL);

    a = kore_calloc(1, sizeof(*a));
    a->path = kore_strdup(fpath);
    TAILQ_INSERT_TAIL(&assets, a, list);

    return (a);
}

void
asset_remove(struct asset *a)
{
    TAILQ_REMOVE(&assets, a, list);

    if (a->cache != NULL)
        cache_ref_drop(&a->cache);

    kore_free(a->path);
    kore_free(a);
}

void
file_open(const char *fpath, int flags, int *fd)
{
	if ((*fd = open(fpath, flags, 0644)) == -1)
		fatal("file_open(%s): %s", fpath, errno_s);
}

void
file_close(int fd)
{
	if (close(fd) == -1)
		kore_log(LOG_NOTICE, "warning: close() %s", errno_s);
}

void
file_read(int fd, char **buf, size_t *len)
{
	struct stat	st;
	char		*p;
	ssize_t		ret;
	size_t		offset, bytes;

	if (fstat(fd, &st) == -1)
		fatal("fstat(): %s", errno_s);

	if (st.st_size > USHRT_MAX)
		fatal("file_read: way too big");

	offset = 0;
	bytes = st.st_size;
	p = kore_malloc(bytes);

	while (offset != bytes) {
		ret = read(fd, p + offset, bytes - offset);
		if (ret == -1) {
			if (errno == EINTR)
				continue;
			fatal("read(): %s", errno_s);
		}

		if (ret == 0)
			fatal("unexpected EOF");

		offset += (size_t)ret;
	}

	*buf = p;
	*len = bytes;
}

int
dir_exists(const char *fpath)
{
	struct stat		st;

	if (stat(fpath, &st) == -1)
		return (0);

	if (!S_ISDIR(st.st_mode))
		return (0);

	return (1);
}

int
file_exists(const char *fpath)
{
	struct stat		st;

	if (stat(fpath, &st) == -1)
		return (0);

	if (!S_ISREG(st.st_mode))
		return (0);

	return (1);
}

void
find_files(const char *path, void (*cb)(char *, struct dirent *))
{
	DIR			*d;
	struct stat		st;
	struct dirent		*dp;
	char			*fpath;

	if ((d = opendir(path)) == NULL)
		fatal("find_files: opendir(%s): %s", path, errno_s);

	while ((dp = readdir(d)) != NULL) {
		if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, ".."))
			continue;

		(void)asprintf(&fpath, "%s/%s", path, dp->d_name);
		if (stat(fpath, &st) == -1) {
			kore_log(LOG_NOTICE, "stat(%s): %s", fpath, errno_s);
			free(fpath);
			continue;
		}

		if (S_ISDIR(st.st_mode)) {
			find_files(fpath, cb);
		} else if (S_ISREG(st.st_mode)) {
			cb(fpath, dp);
		} else {
			kore_log(LOG_NOTICE, "ignoring %s", fpath);
		}

        free(fpath);
	}

	closedir(d);
}

void
file_cb(char *fpath, struct dirent *dp)
{
    (void)dp; /* unused */
    asset_create(fpath);
}
