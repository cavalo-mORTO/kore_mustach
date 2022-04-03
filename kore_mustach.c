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
#include <float.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <kore/kore.h>
#include "mustach.h"
#include "kore_mustach.h"

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
    "invalid itf, possibly malformed json hash",
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

struct stack {
    struct kore_json_item       *root;
    int                         iterate;
    struct kore_runtime_call    *rcall;
    struct kore_buf             *buf;
};

struct closure {
    struct kore_json_item   *context;
    struct kore_buf         *result;
    int                     flags;
    int                     depth;
    struct stack            stack[MUSTACH_MAX_DEPTH];
};

static struct closure *global_cl = NULL;

static int  start(void *);
static int  enter(void *, const char *);
static int  leave(void *);
static int  next(void *);
static int  get(void *, const char *, struct mustach_sbuf *);
static int  partial(void *, const char *, struct mustach_sbuf *);
static int  emit(void *, const char *, size_t, int, FILE *);

static struct kore_json_item    *json_get_item(struct kore_json_item *, const char *);
static struct kore_json_item    *json_item_in_stack(struct closure *, const char *);
static void                     json_tosbuf(struct kore_json_item *, struct mustach_sbuf *);
static int                      json_item_islambda(struct kore_json_item *);
static void                     keyval(char *, char **, enum comp *, int);
static int                      compare(struct kore_json_item *, const char *);
static int                      evalcomp(struct kore_json_item *, const char *, enum comp);
static int                      islambda(struct closure *);
static void                     partial_tosbuf(const char *, struct mustach_sbuf *);
static void                     releasecb(const char *, void *);
static void                     mustach_runtime_execute(void *, struct kore_buf *);

static const struct mustach_itf itf = {
    .start = start,
    .enter = enter,
    .next = next,
    .leave = leave,
    .partial = partial,
    .get = get,
    .emit = emit
};

int
start(void *closure)
{
    struct closure *cl = closure;

    mustach_errno = 0;

    cl->result = kore_buf_alloc(1024);
    cl->depth = 0;
    cl->stack[cl->depth] = (struct stack){};
    cl->stack[cl->depth].root = cl->context;

    if (cl->flags & Mustach_With_Compare)
        cl->flags |= Mustach_With_Equal;

    /* json root must be an object, otherwise there might undefined behavior */
    if (cl->context != NULL && cl->context->type != KORE_JSON_TYPE_OBJECT)
        return (MUSTACH_ERROR_INVALID_ITF);

    return (MUSTACH_OK);
}

int
enter(void *closure, const char *name)
{
    struct closure              *cl = closure;
    struct kore_runtime_call    *rcall;
    struct kore_json_item       *item, *n;
    enum comp                   k;
    char                        key[MUSTACH_MAX_LENGTH + 1], *val;

    if (cl->context == NULL)
        return (0);

    if ((size_t)++cl->depth >= sizeof(cl->stack) / sizeof(cl->stack[0]))
        return (MUSTACH_ERROR_TOO_DEEP);

    cl->stack[cl->depth] = (struct stack){};
    cl->stack[cl->depth].root = cl->context;

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
    item = json_item_in_stack(cl, key);

    if (item != NULL) {
        n = TAILQ_FIRST(&item->data.items);
        switch (item->type) {
            case KORE_JSON_TYPE_LITERAL:
                if (item->data.literal == KORE_JSON_TRUE)
                    return (1);
                break;

            case KORE_JSON_TYPE_ARRAY:
                if (n != NULL) {
                    cl->context = n;
                    cl->stack[cl->depth].iterate = 1;
                    return (1);
                }
                break;

            case KORE_JSON_TYPE_OBJECT:
                if (val != NULL && val[0] == '*' && n != NULL &&
                        (cl->flags & Mustach_With_ObjectIter)) {
                    cl->context = n;
                    cl->stack[cl->depth].iterate = 1;
                    return (1);
                }
                cl->context = item;
                return (1);

            default:
                if ((val != NULL && evalcomp(item, val, k)) || k == C_no) {
                    if (json_item_islambda(item) && (rcall = kore_runtime_getcall(item->name)) != NULL) {
                        cl->stack[cl->depth].rcall = rcall;
                        cl->stack[cl->depth].buf = kore_buf_alloc(128);
                    }
                    cl->context = item;
                    return (1);
                }
        }
    }

    cl->depth--;
    return (0);
}

int
leave(void *closure)
{
    struct closure  *cl = closure;
    struct stack    *prev = &cl->stack[cl->depth];
    int depth;

    cl->context = cl->stack[cl->depth].root;
    if (--cl->depth < 0)
        return (MUSTACH_ERROR_CLOSING);

    if (prev->rcall != NULL) {
        mustach_runtime_execute(prev->rcall->addr, prev->buf);

        depth = islambda(cl);
        if (depth)
            kore_buf_append(cl->stack[depth].buf, prev->buf->data, prev->buf->offset);
        else
            kore_buf_append(cl->result, prev->buf->data, prev->buf->offset);

        kore_buf_free(prev->buf);
        kore_free(prev->rcall);
    }

    return (MUSTACH_OK);
}

int
next(void *closure)
{
    struct closure          *cl = closure;
    struct kore_json_item   *n = TAILQ_NEXT(cl->context, list);

    if (cl->stack[cl->depth].iterate && n != NULL) {
        cl->context = n;
        return (1);
    }
    return (0);
}

int
get(void *closure, const char *name, struct mustach_sbuf *sbuf)
{
    struct closure              *cl = closure;
    struct kore_runtime_call    *rcall;
    struct kore_json_item       *item;
    struct kore_buf             tmp;
    enum comp                   k;
    char                        *val, key[MUSTACH_MAX_LENGTH + 1];

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

    kore_strlcpy(key, name, sizeof(key));
    keyval(key, &val, &k, cl->flags);
    item = json_item_in_stack(cl, key);

    if (item != NULL && ((val != NULL && evalcomp(item, val, k)) || k == C_no)) {

        if (json_item_islambda(item) && (rcall = kore_runtime_getcall(item->name)) != NULL) {
            kore_buf_init(&tmp, 128);
            mustach_runtime_execute(rcall->addr, &tmp);
            sbuf->value = (char *)kore_buf_release(&tmp, &sbuf->length);
            sbuf->freecb = kore_free;
            kore_free(rcall);
        } else {
            json_tosbuf(item, sbuf);
        }
    }

    return (MUSTACH_OK);
}

int
partial(void *closure, const char *name, struct mustach_sbuf *sbuf)
{
    struct closure          *cl = closure;
    struct kore_json_item   *item = json_item_in_stack(cl, name);

    sbuf->value = "";
    (item != NULL) ? json_tosbuf(item, sbuf) : partial_tosbuf(name, sbuf);

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

    depth = islambda(cl);
    if (depth)
        kore_buf_append(cl->stack[depth].buf, tmp.data, tmp.offset);
    else
        kore_buf_append(cl->result, tmp.data, tmp.offset);

    kore_buf_cleanup(&tmp);
    return (MUSTACH_OK);
}
        
struct kore_json_item *
json_get_item(struct kore_json_item *o, const char *name)
{
    struct kore_json_item   *item;
    uint32_t                type = KORE_JSON_TYPE_OBJECT;

    if (o == NULL)
        return (NULL);

    while (type <= KORE_JSON_TYPE_INTEGER_U64) {

        if ((item = kore_json_find(o, name, type)) != NULL)
            return (item);

        if (kore_json_errno() != KORE_JSON_ERR_TYPE_MISMATCH)
            return (NULL);

        type = type << 1;
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
json_item_in_stack(struct closure *cl, const char *name)
{
    struct kore_json_item *o;
    int depth;
    
    if ((o = json_get_item(cl->context, name)) != NULL)
        return (o);

    depth = cl->depth;
    while (depth && (o = json_get_item(cl->stack[depth].root, name)) == NULL)
        depth--;

    return (o);
}

int
json_item_islambda(struct kore_json_item *item)
{
    return (item->name != NULL && item->type == KORE_JSON_TYPE_STRING && !strcmp(item->data.string, "(=>)"));
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
    int c, rc, flip = (value[0] == '!');

    c = compare(o, &value[flip]);
    switch (k) {
        case C_eq: rc = (c == 0); break;
        case C_lt: rc = (c < 0); break;
        case C_le: rc = (c <= 0); break;
        case C_gt: rc = (c > 0); break;
        case C_ge: rc = (c >= 0); break;
        default: return (0);
    }
    return (flip ? !rc : rc);
}

int
islambda(struct closure *cl)
{
    int depth = cl->depth;

    while (depth && cl->stack[depth].rcall == NULL) depth--;

    return (depth);
}

void
partial_tosbuf(const char *path, struct mustach_sbuf *sbuf)
{
    struct kore_server srv = { .tls = 1 };
    struct kore_fileref *ref;
    struct stat st;
    int fd;

    if ((ref = kore_fileref_get(path, srv.tls)) == NULL) {
        if ((fd = open(path, O_RDONLY | O_NOFOLLOW)) == -1)
            return;

        if (fstat(fd, &st) == -1 || !S_ISREG(st.st_mode) || st.st_size <= 0) {
            close(fd);
            return;
        }

        if ((ref = kore_fileref_create(&srv, path, fd, st.st_size, &st.st_mtim)) == NULL) {
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
mustach_runtime_execute(void *addr, struct kore_buf *buf)
{
    void (*cb)(struct kore_buf *);

    *(void **)&(cb) = addr;
    cb(buf);
}

int
kore_mustach_errno(void)
{
    return (mustach_errno);
}

const char *
kore_mustach_strerror(void)
{
    size_t err = mustach_errno * -1;

    if (mustach_errno == kore_json_errno())
        return (kore_json_strerror());

    if (err < sizeof(mustach_errtab) / sizeof(mustach_errtab[0]))
        return (mustach_errtab[err]);

    return ("unknown mustach error");
}

struct kore_json_item *
kore_mustach_find_json_item(const char *name)
{
    if (global_cl == NULL)
        return (NULL);

    return (json_item_in_stack(global_cl, name));
}

int
kore_mustach_json(const char *template, struct kore_json_item *json, int flags,
        struct kore_buf **result)
{
    struct closure  cl = { .context = json, .flags = flags };

    global_cl = &cl;
    mustach_errno = mustach_file(template, 0, &itf, &cl, flags, 0);

    if (mustach_errno >= 0) {
        mustach_errno = kore_json_errno();
        *result = cl.result;
    } else {
        kore_buf_free(cl.result);
        *result = NULL;
    }

    global_cl = NULL;
    return (mustach_errno >= 0 ? KORE_RESULT_OK : KORE_RESULT_ERROR);
}

int
kore_mustach(const char *template, const char *data, int flags,
        struct kore_buf **result)
{
    struct kore_json json = {};
    mustach_errno = 0;

    if (data != NULL) {
        kore_json_init(&json, data, strlen(data));
        if (!kore_json_parse(&json))
            mustach_errno = MUSTACH_ERROR_INVALID_ITF;
    }

    if (mustach_errno == 0)
        kore_mustach_json(template, json.root, flags, result);

    kore_json_cleanup(&json);
    return (mustach_errno >= 0 ? KORE_RESULT_OK : KORE_RESULT_ERROR);
}
