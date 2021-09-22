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
#include "tinyexpr.h"

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
        struct kore_json_item   *lambda;
        int                     iterate;
    } stack[MUSTACH_MAX_DEPTH];
};

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
static void                     keyval(char *, char **, enum comp *, int);
static int                      compare(struct kore_json_item *, const char *);
static int                      evalcomp(struct kore_json_item *, const char *, enum comp);
static int                      islambda(struct closure *, int *);
static int                      split_string_pbrk(char *, const char *, char **, size_t);
static double                   eval(struct closure *, const char *);
static void                     partial_tosbuf(const char *, struct mustach_sbuf *);
static void                     releasecb(const char *, void *);
static void                     run_lambda(struct lambda *, const char *, struct kore_buf *);

int
start(void *closure)
{
    struct closure          *cl = closure;

    mustach_errno = 0;

    cl->depth = 0;
    cl->stack[0].root = cl->context;
    cl->stack[0].lambda = NULL;
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
    cl->stack[cl->depth].lambda = NULL;
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
    if ((item = json_item_in_stack(cl, key)) != NULL) {
        switch (item->type) {
            case KORE_JSON_TYPE_LITERAL:
                if (item->data.literal != KORE_JSON_TRUE) {
                    cl->depth--;
                    return (0);
                }
                return (1);

            case KORE_JSON_TYPE_ARRAY:
                if ((n = TAILQ_FIRST(&item->data.items)) == NULL) {
                    cl->depth--;
                    return (0);
                }
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
                    cl->stack[cl->depth].lambda = item;
                    return (1);
                }
                /* fallthrough */

            default:
                if (k != C_no && val != NULL &&
                        (val[0] == '!' ? evalcomp(item, &val[1], k) : !evalcomp(item, val, k))) {
                    cl->depth--;
                    return (0);
                }
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
    struct closure          *cl = closure;

    cl->context = cl->stack[cl->depth].root;
    if (--cl->depth <= 0)
        return (MUSTACH_ERROR_CLOSING);

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
    enum comp               k;
    char                    *val, dval[50], key[MUSTACH_MAX_LENGTH + 1];
    double                  d;

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
    if ((item = json_item_in_stack(cl, key)) != NULL &&
            ((val != NULL && (val[0] == '!' ? !evalcomp(item, &val[1], k) : evalcomp(item, val, k))) || k == C_no)) {
        json_tosbuf(item, sbuf);
    } else if (cl->flags & Mustach_With_TinyExpr) {
        d = eval(cl, name);
        if (!isnan(d)) {
            snprintf(dval, sizeof(dval), "%.9g", d);
            sbuf->value = kore_strdup(dval);
            sbuf->freecb = kore_free;
        }
    }

    return (MUSTACH_OK);
}

int
partial(void *closure, const char *name, struct mustach_sbuf *sbuf)
{
    struct closure          *cl = closure;
    struct kore_json_item   *item;

    sbuf->value = "";
    if (cl->context != NULL && (item = json_item_in_stack(cl, name)) != NULL)
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

    depth = cl->depth;
    while (islambda(cl, &depth)) {
        run_lambda(cl->lambdas, cl->stack[depth].lambda->name, &tmp);
        depth--;
    }

    kore_buf_append(cl->result, tmp.data, tmp.offset);
    kore_buf_cleanup(&tmp);
    return (MUSTACH_OK);
}
        
struct kore_json_item *
json_get_item(struct kore_json_item *o, const char *name)
{
    struct kore_json_item   *item;
    uint32_t                type;

    if (name == NULL)
        return (NULL);

    for (type = KORE_JSON_TYPE_OBJECT;
            type <= KORE_JSON_TYPE_INTEGER_U64; type <<= 1) {

        if ((item = kore_json_find(o, name, type)) != NULL)
            return (item);

        if (kore_json_errno() != KORE_JSON_ERR_TYPE_MISMATCH)
            return (NULL);
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
            sbuf->length = snprintf(b, sizeof(b), "%.9g", o->data.number);
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
split_string_pbrk(char *s, const char *accept, char **out, size_t ele)
{
    int         count;
    char        **ap;

    if (ele == 0)
        return (0);

    ap = out;
    count = 0;

    while (ap < &out[ele - 1]) {

        *ap++ = s;
        count++;

        if ((s = strpbrk(s, accept)) == NULL)
            break;

        *s++ = '\0';
    }

    *ap = NULL;
    return (count);
}

double
eval(struct closure *cl, const char *expression)
{
    const char  *accept = "+-*/^%(), ";
    struct kore_json_item *o;
    double      d[128], result;
    char        *vars_s[128], *copy;
    te_variable vars[128];
    te_expr     *expr;
    int         i, len, n;

    copy = kore_strdup(expression);
    len = split_string_pbrk(copy, accept,
            vars_s, sizeof(vars_s) / sizeof(vars_s[0]));

    n = 0;
    for (i = 0; i < len; i++) {
        if ((o = json_item_in_stack(cl, vars_s[i])) != NULL) {
            switch (o->type) {
                case KORE_JSON_TYPE_NUMBER:
                    d[i] = o->data.number;
                    break;
                case KORE_JSON_TYPE_INTEGER:
                    d[i] = o->data.integer;
                    break;
                case KORE_JSON_TYPE_INTEGER_U64:
                    d[i] = o->data.u64;
                    break;
                default:
                    d[i] = NAN;
            }

            vars[n].name = vars_s[i];
            vars[n].address = &d[i];
            n++;
        }
    }

    expr = te_compile(expression, vars, n, 0);
    if (expr) {
        result = te_eval(expr);
        te_free(expr);
    } else {
        result = NAN;
    }

    kore_free(copy);
    return (result);
}

int
islambda(struct closure *cl, int *depth)
{
    while (*depth && cl->stack[*depth].lambda == NULL)
        (*depth)--;

    return (*depth);
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
run_lambda(struct lambda *lambdas, const char *name, struct kore_buf *buf)
{
    if (lambdas == NULL)
        return;

    for (int i = 0; lambdas[i].name != NULL && lambdas[i].cb != NULL; i++) {
        if (!strcmp(name, lambdas[i].name)) {
            lambdas[i].cb(buf);
            return;
        }
    }
}

int
kore_mustach_errno(void)
{
    if (kore_json_errno() != KORE_JSON_ERR_NONE)
        return (kore_json_errno());

    return (mustach_errno);
}

const char *
kore_mustach_strerror(void)
{
    int err;

    if (kore_json_errno() != KORE_JSON_ERR_NONE)
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
    return (mustach_errno == MUSTACH_OK ? KORE_RESULT_OK : KORE_RESULT_ERROR);
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
