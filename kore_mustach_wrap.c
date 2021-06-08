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
#define _XOPEN_SOURCE 500
#include <ftw.h>
#include <fcntl.h>
#include <kore/kore.h>
#include "mustach.h"
#include "kore_mustach.h"

struct cache {
    uint32_t        refs;
    char            *data;
    size_t          len;
};

struct asset {
    char            *path;
    size_t          fsize;
    struct cache    *cache;

    TAILQ_ENTRY(asset) list;
};

static TAILQ_HEAD(, asset)  assets;
static TAILQ_HEAD(, lambda) lambdas;
static int                  tailq_init = 0;

static struct cache     *cache_create(void);
static void             cache_ref_drop(struct cache **);
static struct asset     *asset_get(const char *);
static struct asset     *asset_create(const char *, size_t);
static void             asset_remove(struct asset *);
static void             file_open(const char *, int, int *);
static void             file_close(int);
static void             file_read(int, size_t, char **, size_t *);
static void             releasecb(const char *, void *);
static int              ftw_cb(const char *, const struct stat *, int, struct FTW *);

void
kore_mustach_sys_init(void)
{
    TAILQ_INIT(&assets);
    TAILQ_INIT(&lambdas);
    tailq_init = 1;
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
        if (nftw(fpath[i], ftw_cb, 20, FTW_PHYS | FTW_MOUNT) == -1) {
            kore_log(LOG_NOTICE, "nftw: %s", errno_s);
        }
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
        file_read(fd, a->fsize, &a->cache->data, &a->cache->len);
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
asset_create(const char *fpath, size_t fsize)
{
    struct asset *a;

    a = kore_calloc(1, sizeof(*a));
    a->path = kore_strdup(fpath);
    a->fsize = fsize;
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
file_read(int fd, size_t fsize, char **buf, size_t *len)
{
	char		*p;
	ssize_t		ret;
	size_t		offset;

	if (fsize > USHRT_MAX)
		fatal("file_read: way too big");

	offset = 0;
	p = kore_malloc(fsize);

	while (offset != fsize) {
		ret = read(fd, p + offset, fsize - offset);
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
	*len = fsize;
}

int
ftw_cb(const char *fpath, const struct stat *sb,
        int typeflag, struct FTW *ftwbuf)
{
    (void)typeflag; /* unused */
    (void)ftwbuf; /* unused */

    if (S_ISREG(sb->st_mode) &&
            sb->st_size <= USHRT_MAX)
        asset_create(fpath, sb->st_size);

    return (0);
}
