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
#include <math.h>
#include <float.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <kore/kore.h>
#include <kore/http.h>
#include "mustach.h"
#include "kore_mustach.h"

#define KORE_JSON_TYPE_ALL	0x007F

static const char *mustach_srv = NULL;

static int mustach_errno = 0;

static const char *mustach_errtab[] = {
    "no error",
    "system error",
    "unexpected end",
    "empty tag",
    "tag too long",
    "bad separators",
    "too many nested items",
    "too few nested items",
    "bad unescape tag",
    "invalid itf",
    "item not found",
    "partial not found",
};

enum comp {
	C_no = 0,
	C_eq = 1,
	C_lt = 5,
	C_le = 6,
	C_gt = 9,
	C_ge = 10
};

struct closure {
    struct kore_json_item   *context;
    struct kore_buf         *result;
    int                     flags;
    int                     depth;

    struct lambda           *lambdas;
    struct {
        struct kore_json_item   *root;
        int                     iterate;
        const char              *lambda_name;
        struct kore_buf         *lambda_ctx;
    } stack[MUSTACH_MAX_DEPTH];
};

static int  start(void *);
static int  enter(void *, const char *);
static int  leave(void *);
static int  next(void *);
static int  get(void *, const char *, struct mustach_sbuf *);
static int  partial(void *, const char *, struct mustach_sbuf *);
static int  emit(void *, const char *, size_t, int, FILE *);

static struct kore_json_item    *json_get_item(struct kore_json_item *, const char *, uint32_t);
static struct kore_json_item    *json_item_in_stack(struct closure *, const char *, uint32_t);
static void                     json_tosbuf(struct kore_json_item *, struct mustach_sbuf *);
static void                     keyval(char *, char **, enum comp *, int);
static int                      compare(struct kore_json_item *, const char *);
static int                      evalcomp(struct kore_json_item *, const char *, enum comp);
static int                      islambda(struct closure *);
static void                     partial_tosbuf(const char *, struct mustach_sbuf *);
static void                     releasecb(const char *, void *);
static void                     run_lambda(struct closure *, const char *, struct kore_buf *);

int
start(void *closure)
{
    struct closure          *cl = closure;

    mustach_errno = 0;

    cl->depth = 0;
    cl->stack[0].root = cl->context;
    cl->stack[0].lambda_name = NULL;
    cl->stack[0].lambda_ctx = NULL;
    cl->stack[0].iterate = 0;

    if (cl->flags & Mustach_With_Compare)
        cl->flags |= Mustach_With_Equal;

    /* initialize our buffer */
    cl->result = kore_buf_alloc(4096);

    /* json root must be an object, otherwise there might undefined behavior */
    if (cl->context != NULL &&
            cl->context->type != KORE_JSON_TYPE_OBJECT)
        return (MUSTACH_ERROR_INVALID_ITF);

    return (MUSTACH_OK);
}

int
enter(void *closure, const char *name)
{
    struct closure          *cl = closure;
    struct kore_json_item   *item, *n;
    enum comp               k;
    char                    key[MUSTACH_MAX_LENGTH + 1], *val;

    if (cl->context == NULL)
        return (0);

    if ((size_t)++cl->depth >= sizeof(cl->stack) / sizeof(cl->stack[0]))
        return (MUSTACH_ERROR_TOO_DEEP);

    cl->stack[cl->depth].root = cl->context;
    cl->stack[cl->depth].lambda_name = NULL;
    cl->stack[cl->depth].lambda_ctx = NULL;
    cl->stack[cl->depth].iterate = 0;

    if (name[0] == '*' && name[1] == '\0' &&
            (cl->flags & Mustach_With_ObjectIter)) {

        if (cl->context->type == KORE_JSON_TYPE_OBJECT &&
                (n = TAILQ_FIRST(&cl->context->data.items)) != NULL) {
            cl->context = n;
            cl->stack[cl->depth].iterate = 1;
            return (1);
        }

        cl->depth--;
        return (0);
    }

    kore_strlcpy(key, name, sizeof(key));
    keyval(key, &val, &k, cl->flags);
    if ((item = json_item_in_stack(cl, key, KORE_JSON_TYPE_ALL)) != NULL) {
        switch (item->type) {
            case KORE_JSON_TYPE_LITERAL:
                if (item->data.literal == KORE_JSON_TRUE)
                    return (1);
                break;

            case KORE_JSON_TYPE_ARRAY:
                if ((n = TAILQ_FIRST(&item->data.items)) == NULL)
                    break;

                cl->context = n;
                cl->stack[cl->depth].iterate = 1;
                return (1);

            case KORE_JSON_TYPE_OBJECT:
                if (val != NULL && val[0] == '*' &&
                        (n = TAILQ_FIRST(&item->data.items)) != NULL &&
                        (cl->flags & Mustach_With_ObjectIter)) {
                    cl->context = n;
                    cl->stack[cl->depth].iterate = 1;
                    return (1);
                }
                cl->context = item;
                return (1);

            case KORE_JSON_TYPE_STRING:
                if (cl->lambdas != NULL &&
                        !strcmp(item->data.string, "(=>)")) {
                    cl->stack[cl->depth].lambda_name = item->name;
                    cl->stack[cl->depth].lambda_ctx = kore_buf_alloc(128);
                    return (1);
                }
                /* fallthrough */

            default:
                if (k != C_no && val != NULL &&
                        (val[0] == '!' ? evalcomp(item, &val[1], k) : !evalcomp(item, val, k)))
                    break;

                cl->context = item;
                return (1);
        }
    }

    cl->depth--;
    return (0);
}

int
leave(void *closure)
{
    struct closure  *cl = closure;
    struct kore_buf *lambda_ctx;
    const char      *lambda_name;
    int             depth;

    lambda_ctx = cl->stack[cl->depth].lambda_ctx;
    lambda_name = cl->stack[cl->depth].lambda_name;

    cl->context = cl->stack[cl->depth].root;
    if (--cl->depth < 0)
        return (MUSTACH_ERROR_CLOSING);

    if (lambda_ctx != NULL) {
        run_lambda(cl, lambda_name, lambda_ctx);

        if ((depth = islambda(cl)))
            kore_buf_append(cl->stack[depth].lambda_ctx, lambda_ctx->data, lambda_ctx->offset);
        else
            kore_buf_append(cl->result, lambda_ctx->data, lambda_ctx->offset);

        kore_buf_free(lambda_ctx);
    }

    return (MUSTACH_OK);
}

int
next(void *closure)
{
    struct closure          *cl = closure;
    struct kore_json_item   *n;

    if (cl->stack[cl->depth].iterate &&
            (n = TAILQ_NEXT(cl->context, list)) != NULL) {
        cl->context = n;
        return (1);
    }
    return (0);
}

int
get(void *closure, const char *name, struct mustach_sbuf *sbuf)
{
    struct closure          *cl = closure;
    struct kore_json_item   *item;
    struct kore_buf         tmp;
    enum comp               k;
    char                    *val, key[MUSTACH_MAX_LENGTH + 1];

    sbuf->value = "";
    if (cl->context == NULL)
        return (MUSTACH_OK);

    if (name[0] == '*' && name[1] == '\0' &&
            (cl->flags & Mustach_With_ObjectIter)) {

        if (cl->context->name != NULL)
            sbuf->value = cl->context->name;

        return (MUSTACH_OK);
    }

    if (name[0] == '.' && name[1] == '\0') {
        json_tosbuf(cl->context, sbuf);
        return (MUSTACH_OK);
    }

    if (cl->lambdas != NULL &&
            (item = json_item_in_stack(cl, name, KORE_JSON_TYPE_STRING)) != NULL &&
            !strcmp(item->data.string, "(=>)"))
    {
        kore_buf_init(&tmp, 128);
        run_lambda(cl, name, &tmp);
        sbuf->value = (char *)kore_buf_release(&tmp, &sbuf->length);
        sbuf->freecb = kore_free;
        return (MUSTACH_OK);
    }

    kore_strlcpy(key, name, sizeof(key));
    keyval(key, &val, &k, cl->flags);
    if ((item = json_item_in_stack(cl, key, KORE_JSON_TYPE_ALL)) != NULL &&
            ((val != NULL && (val[0] == '!' ? !evalcomp(item, &val[1], k) : evalcomp(item, val, k))) || k == C_no)) {
        json_tosbuf(item, sbuf);
    }

    return (MUSTACH_OK);
}

int
partial(void *closure, const char *name, struct mustach_sbuf *sbuf)
{
    struct closure          *cl = closure;
    struct kore_json_item   *item;

    sbuf->value = "";
    if (cl->context != NULL && (item = json_item_in_stack(cl, name, KORE_JSON_TYPE_ALL)) != NULL)
        json_tosbuf(item, sbuf);
    else
        partial_tosbuf(name, sbuf);

    return (MUSTACH_OK);
}

int
emit(void *closure, const char *buffer, size_t size, int escape, FILE *file)
{
    struct closure  *cl = closure;
    struct kore_buf tmp;
    int depth;

    (void)file; /* unused */

    kore_buf_init(&tmp, size);
    kore_buf_append(&tmp, buffer, size);

    if (escape) {
        kore_buf_replace_string(&tmp, "&", "&amp;", 5);
        kore_buf_replace_string(&tmp, "<", "&lt;", 4);
        kore_buf_replace_string(&tmp, ">", "&gt;", 4);
        kore_buf_replace_string(&tmp, "\"", "&quot;", 6);
        /* kore_buf_replace_string(&tmp, "\\", "&#39;", 5); 
         * kore_buf_replace_string(&tmp, "/", "&#x2F;", 6); */
    }

    if ((depth = islambda(cl)))
        kore_buf_append(cl->stack[depth].lambda_ctx, tmp.data, tmp.offset);
    else
        kore_buf_append(cl->result, tmp.data, tmp.offset);

    kore_buf_cleanup(&tmp);
    return (MUSTACH_OK);
}
        
struct kore_json_item *
json_get_item(struct kore_json_item *o, const char *name, uint32_t typemask)
{
    struct kore_json_item   *item;
    uint32_t                type;

    for (type = KORE_JSON_TYPE_OBJECT;
            type <= KORE_JSON_TYPE_INTEGER_U64; type <<= 1) {

        if (typemask & type) {
            if ((item = kore_json_find(o, name, type)) != NULL)
                return (item);

            if (kore_json_errno() != KORE_JSON_ERR_TYPE_MISMATCH)
                return (NULL);
        }
    }

    return (NULL);
}

void
json_tosbuf(struct kore_json_item *o, struct mustach_sbuf *sbuf)
{
    char            b[50], *name;
    struct kore_buf buf;
   
    switch (o->type) {
        case KORE_JSON_TYPE_STRING:
            sbuf->value = kore_strdup(o->data.string);
            break;

        case KORE_JSON_TYPE_NUMBER:
            sbuf->length = snprintf(b, sizeof(b), "%g", o->data.number);
            sbuf->value = kore_strdup(b);
            break;

        default:
            name = o->name;
            o->name = NULL;
            kore_buf_init(&buf, 1024);
            kore_json_item_tobuf(o, &buf);
            o->name = name;
            sbuf->value = (char *)kore_buf_release(&buf, &sbuf->length);
    }
    sbuf->freecb = kore_free;
}

struct kore_json_item *
json_item_in_stack(struct closure *cl, const char *name, uint32_t typemask)
{
    struct kore_json_item *o;
    int depth;
    
    if ((o = json_get_item(cl->context, name, typemask)) != NULL)
        return (o);

    depth = cl->depth;
    while (depth && (o = json_get_item(cl->stack[depth].root, name, typemask)) == NULL)
        depth--;

    return (o);
}

void
keyval(char *key, char **val, enum comp *k, int flags)
{
    char    *s, *o;

    *val = NULL;
    *k = C_no;

    for (o = s = key; *s != '\0' && *val == NULL; s++) {
        switch (*s) {
            case '*':
                if (flags & Mustach_With_ObjectIter) {
                    *val = "*";
                    continue;
                }
                break;

            case '<':
            case '>':
                if (flags & Mustach_With_Compare) {
                    if (*s == '<')
                        *k = C_lt;
                    else
                        *k = C_gt;

                    if (*++s == '=') {
                        (*k)++;
                        s++;
                    }
                    *val = s;
                    continue;
                }
                break;

            case '=':
                if (flags & Mustach_With_Equal) {
                    *k = C_eq;
                    *val = ++s;
                    continue;
                }
                break;

            case '~':
                if (flags & Mustach_With_JsonPointer) {
                    switch (*++s) {
                        case '1': *o++ = '/'; break;
                        case '0': *o++ = '~'; break;
                        default: *o++ = *--s; break;
                    }
                    continue;
                }
                break;

            case '.':
                *o++ = '/';
                continue;

            case '\\':
                s++;
        }
        *o++ = *s;
    }
    *o = '\0';
}

int
compare(struct kore_json_item *o, const char *value)
{
    double      d;
    int64_t     i;
    uint64_t    u;
    int         err;

    switch (o->type) {
        case KORE_JSON_TYPE_NUMBER:
            d = kore_strtodouble(value, DBL_MIN, DBL_MAX, &err);
            return (!err) ? 0 : (o->data.number > d) - (o->data.number < d);

        case KORE_JSON_TYPE_INTEGER:
            i = kore_strtonum64(value, 1, &err);
            return (!err) ? 0 : (o->data.integer > i) - (o->data.integer < i);

        case KORE_JSON_TYPE_INTEGER_U64:
            u = kore_strtonum64(value, 0, &err);
            return (!err) ? 0 : (o->data.u64 > u) - (o->data.u64 < u);

        case KORE_JSON_TYPE_STRING:
            return (strcmp(o->data.string, value));

        default: return (0);
    }
}

int
evalcomp(struct kore_json_item *o, const char *value, enum comp k)
{
    int c;

    c = compare(o, value);
    switch (k) {
        case C_eq: return (c == 0);
        case C_lt: return (c < 0);
        case C_le: return (c <= 0);
        case C_gt: return (c > 0);
        case C_ge: return (c >= 0);
        default: return (0);
    }
}

int
islambda(struct closure *cl)
{
    int depth = cl->depth;

    while (depth && cl->stack[depth].lambda_name == NULL)
        depth--;

    return (depth);
}

void
partial_tosbuf(const char *path, struct mustach_sbuf *sbuf)
{
    struct kore_server  *srv;
    struct kore_fileref *ref;
    struct stat st;
    int fd;

    if (mustach_srv == NULL ||
            (srv = kore_server_lookup(mustach_srv)) == NULL)
        return;

    if ((ref = kore_fileref_get(path, srv->tls)) == NULL) {
        if ((fd = open(path, O_RDONLY | O_NOFOLLOW)) == -1)
            return;

        if (fstat(fd, &st) == -1 || !S_ISREG(st.st_mode) || st.st_size <= 0) {
            close(fd);
            return;
        }

        if ((ref = kore_fileref_create(srv, path, fd, st.st_size, &st.st_mtim)) == NULL) {
            close(fd);
            return;
        }
    }

    if (ref->base != NULL) {
        sbuf->value = ref->base;
        sbuf->length = ref->size;
    }

    sbuf->releasecb = releasecb;
    sbuf->closure = ref;
}

void
releasecb(const char *value, void *closure)
{
    struct kore_fileref *ref = closure;

    (void)value; /* unused */
    kore_fileref_release(ref);
}

void
run_lambda(struct closure *cl, const char *name, struct kore_buf *buf)
{
    if (cl->lambdas == NULL)
        return;

    for (int i = 0; cl->lambdas[i].name != NULL && cl->lambdas[i].cb != NULL; i++) {
        if (!strcmp(name, cl->lambdas[i].name)) {
            cl->lambdas[i].cb(cl->context, buf);
            return;
        }
    }
}

int
kore_mustach_errno(void)
{
    return (kore_json_errno() ? kore_json_errno() : mustach_errno);
}

const char *
kore_mustach_strerror(void)
{
    int err;

    if (kore_json_errno())
        return (kore_json_strerror());

    err = mustach_errno * -1;
    if (err >= 0 &&
            (size_t)err < sizeof(mustach_errtab) / sizeof(mustach_errtab[0]))
        return (mustach_errtab[err]);

    return ("unknown mustach error");
}

int
kore_mustach_json(const char *server_name, const char *template, struct kore_json_item *json,
        int flags, struct lambda *lambdas, char **result, size_t *length)
{
    struct mustach_itf itf = {
        .start = start,
        .enter = enter,
        .next = next,
        .leave = leave,
        .partial = partial,
        .get = get,
        .emit = emit,
    };
    struct closure cl = {
        .lambdas = lambdas,
        .context = json,
        .flags = flags,
    };

    mustach_srv = server_name;
    mustach_errno = mustach_file(template, 0, &itf, &cl, flags, 0);
    *result = (char *)kore_buf_release(cl.result, length);

    mustach_srv = NULL;
    return (!mustach_errno && !kore_json_errno() ? KORE_RESULT_OK : KORE_RESULT_ERROR);
}

int
kore_mustach(const char *server_name, const char *template, const char *data,
        int flags, struct lambda *lambdas, char **result, size_t *length)
{
    struct kore_json_item *item = NULL;
    struct kore_json json = { 0 };
    int rc;

    if (data != NULL) {
        kore_json_init(&json, data, strlen(data));
        if (kore_json_parse(&json))
            item = json.root;
    }

    rc = kore_mustach_json(server_name, template, item, flags, lambdas, result, length);
    kore_json_cleanup(&json);
    return (rc);
}
