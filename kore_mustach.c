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
#include <kore/kore.h>
#include "mustach.h"
#include "kore_mustach.h"
#include "tinyexpr.h"

#if defined(NO_EXTENSION_FOR_MUSTACH)
# undef  NO_SINGLE_DOT_EXTENSION_FOR_MUSTACH
# define NO_SINGLE_DOT_EXTENSION_FOR_MUSTACH
# undef  NO_EQUAL_VALUE_EXTENSION_FOR_MUSTACH
# define NO_EQUAL_VALUE_EXTENSION_FOR_MUSTACH
# undef  NO_COMPARE_VALUE_EXTENSION_FOR_MUSTACH
# define NO_COMPARE_VALUE_EXTENSION_FOR_MUSTACH
# undef  NO_OBJECT_ITERATION_FOR_MUSTACH
# define NO_OBJECT_ITERATION_FOR_MUSTACH
# undef  NO_TINY_EXPR_EXTENSION_FOR_MUSTACH
# define NO_TINY_EXPR_EXTENSION_FOR_MUSTACH
#endif

#if !defined(NO_COMPARE_VALUE_EXTENSION_FOR_MUSTACH)
# undef NO_EQUAL_VALUE_EXTENSION_FOR_MUSTACH
#endif

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
    struct kore_buf         result;
    int                     depth;
    size_t                  depth_max;

    struct {
        struct kore_json_item   *root;
        struct kore_json_item   *lambda;
        int                     iterate;
    } stack[MUSTACH_MAX_DEPTH];

    int     (*partial_cb)(const char *, struct mustach_sbuf *);
    int     (*lambda_cb)(const char *, struct kore_buf *);
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
static char                     *json_get_self_value(struct kore_json_item *);
static void                     keyval(char *, char **, enum comp *);
static int                      compare(struct kore_json_item *, const char *);
static int                      evalcomp(struct kore_json_item *, const char *, enum comp);
static int                      islambda(struct closure *, int *);
#if !defined(NO_TINY_EXPR_EXTENSION_FOR_MUSTACH)
static int                      split_string_pbrk(char *, const char *, char **, size_t);
static double                   eval(struct closure *, const char *);
#endif

int
start(void *closure)
{
    struct closure          *cl = closure;

    cl->depth = 0;
    cl->depth_max = sizeof(cl->stack) / sizeof(cl->stack[0]);
    cl->stack[0].root = cl->context;
    cl->stack[0].lambda = NULL;
    cl->stack[0].iterate = 0;

    /* initialize our buffer with 8 kilobytes. */
    kore_buf_init(&cl->result, 8 << 10);

    return (MUSTACH_OK);
}

int
enter(void *closure, const char *name)
{
    struct closure          *cl = closure;
    struct kore_json_item   *item, *n;
    enum comp               k;
    char                    key[MUSTACH_MAX_LENGTH], *val;

    if (cl->context == NULL)
        return (0);

    if ((size_t)++cl->depth >= cl->depth_max)
        return (MUSTACH_ERROR_TOO_DEEP);

    cl->stack[cl->depth].root = cl->context;
    cl->stack[cl->depth].lambda = NULL;
    cl->stack[cl->depth].iterate = 0;

#if !defined(NO_OBJECT_ITERATION_FOR_MUSTACH)
    if (name[0] == '*' && !name[1]) {
        if (cl->context->type == KORE_JSON_TYPE_OBJECT &&
                (n = TAILQ_FIRST(&cl->context->data.items)))
        {
            cl->context = n;
            cl->stack[cl->depth].iterate = 1;
            return (1);
        }

        cl->depth--;
        return (0);
    }
#endif
    kore_strlcpy(key, name, sizeof(key));
    keyval(key, &val, &k);
    if ((item = json_item_in_stack(cl, key)) != NULL) {
        switch (item->type) {
            case KORE_JSON_TYPE_LITERAL:
                if (item->data.literal != KORE_JSON_TRUE) {
                    cl->depth--;
                    return (0);
                }
                break;

            case KORE_JSON_TYPE_ARRAY:
                if ((n = TAILQ_FIRST(&item->data.items)) == NULL) {
                    cl->depth--;
                    return (0);
                }
                cl->context = n;
                cl->stack[cl->depth].iterate = 1;
                break;

            case KORE_JSON_TYPE_OBJECT:
#if !defined(NO_OBJECT_ITERATION_FOR_MUSTACH)
                if (val && val[0] == '*' &&
                        (n = TAILQ_FIRST(&item->data.items)) != NULL)
                {
                    cl->context = n;
                    cl->stack[cl->depth].iterate = 1;
                    break;
                }
#endif
                cl->context = item;
                break;

            case KORE_JSON_TYPE_STRING:
                if (!memcmp(item->data.string, "(=>)", 4)) {
                    cl->stack[cl->depth].lambda = item;
                }
                /* fallthrough */

            default:
                if (k != C_no && val &&
                        (val[0] == '!' ? evalcomp(item, &val[1], k) : !evalcomp(item, val, k))) {
                    cl->depth--;
                    return (0);
                }
                cl->context = item;
        }

        return (1);
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
            (n = TAILQ_NEXT(cl->context, list)))
    {
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
    char                    *val, *value, key[MUSTACH_MAX_LENGTH];
#if !defined(NO_TINY_EXPR_EXTENSION_FOR_MUSTACH)
    double                  d;
#endif

    sbuf->value = "";
    if (cl->context == NULL)
        return (MUSTACH_OK);

    switch (name[0]) {
#if !defined(NO_OBJECT_ITERATION_FOR_MUSTACH)
        case '*':
            if (cl->context->name != NULL)
                sbuf->value = cl->context->name;
            break;
#endif
#if !defined(NO_SINGLE_DOT_EXTENSION_FOR_MUSTACH)
        case '.':
            if ((value = json_get_self_value(cl->context)) != NULL) {
                sbuf->value = value;
                sbuf->freecb = kore_free;
            }
            break;
#endif
        default:
            kore_strlcpy(key, name, sizeof(key));
            keyval(key, &val, &k);
            if ((item = json_item_in_stack(cl, key)) != NULL &&
                    ((val && (val[0] == '!' ? !evalcomp(item, &val[1], k) : evalcomp(item, val, k))) || k == C_no) &&
                    (value = json_get_self_value(item)) != NULL)
            {
                sbuf->value = value;
                sbuf->freecb = kore_free;
                break;
            }
#if !defined(NO_TINY_EXPR_EXTENSION_FOR_MUSTACH)
            d = eval(cl, name);
            if (!isnan(d) && asprintf(&value, "%.9g", d) > -1 ) {
                sbuf->value = value;
                sbuf->freecb = free;
            }
#endif
    }

    return (MUSTACH_OK);
}

int
partial(void *closure, const char *name, struct mustach_sbuf *sbuf)
{
    struct closure  *cl = closure;
    struct kore_json_item   *item;
    const char      *s;

    sbuf->value = "";
    if (cl->context && (item = json_item_in_stack(cl, name)) &&
            (s = json_get_self_value(item))) {
        sbuf->value = s;
        sbuf->freecb = kore_free;
    } else if (cl->partial_cb) {
        return (cl->partial_cb(name, sbuf));
    }

    return (MUSTACH_OK);
}

int
emit(void *closure, const char *buffer, size_t size, int escape, FILE *file)
{
    struct closure  *cl = closure;
    struct kore_buf tmp;
    int depth;

    (void)file; /* unused */
        
    kore_buf_init(&tmp, size + 1);
    kore_buf_append(&tmp, buffer, size);

    if (escape) {
        kore_buf_replace_string(&tmp, "&", "&amp;", 5);
        kore_buf_replace_string(&tmp, "<", "&lt;", 4);
        kore_buf_replace_string(&tmp, ">", "&gt;", 4);
        /* kore_buf_replace_string(&tmp, "\\", "&#39;", 5); 
         * kore_buf_replace_string(&tmp, "\"", "&quot;", 6);
         * kore_buf_replace_string(&tmp, "/", "&#x2F;", 6); */
    }

    if (cl->lambda_cb) {
        depth = cl->depth;
        while (islambda(cl, &depth)) {
            cl->lambda_cb(cl->stack[depth].lambda->name, &tmp);
            depth--;
        }
    }

    kore_buf_append(&cl->result, tmp.data, tmp.offset);
    kore_buf_cleanup(&tmp);
    return (MUSTACH_OK);
}
        
struct kore_json_item *
json_get_item(struct kore_json_item *o, const char *name)
{
    struct kore_json_item   *item;
    int                     type;

    if (name == NULL)
        return (NULL);

    for (type = KORE_JSON_TYPE_OBJECT;
            type <= KORE_JSON_TYPE_INTEGER_U64; type *= 2) {
        if ((item = kore_json_find(o, name, type)) != NULL)
            return (item);
    }

    return (NULL);
}

char *
json_get_self_value(struct kore_json_item *o)
{
    size_t          len;
    char            b[256], *name;
    struct kore_buf buf;
   
    switch (o->type) {
        case KORE_JSON_TYPE_STRING:
            return (kore_strdup(o->data.string));

        case KORE_JSON_TYPE_NUMBER:
            snprintf(b, sizeof(b), "%.9g", o->data.number);
            return (kore_strdup(b));

        default:
            name = o->name;
            o->name = NULL;
            kore_buf_init(&buf, 1024);
            kore_json_item_tobuf(o, &buf);
            kore_buf_append(&buf, "\0", 1);
            o->name = name;
            return ((char *)kore_buf_release(&buf, &len));
    }
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
keyval(char *key, char **val, enum comp *k)
{
    char    *s, *o;

    *val = NULL;
    *k = C_no;

    for (o = s = key; *s != '\0'; s++) {
        switch (*s) {
#if !defined(NO_OBJECT_ITERATION_FOR_MUSTACH)
            case '*':
                *val = "*";
                break;
#endif
#if !defined(NO_COMPARE_VALUE_EXTENSION_FOR_MUSTACH)
            case '>':
                if (*++s == '=') {
                    *k = C_ge;
                    *val = ++s;
                } else {
                    *k = C_gt;
                    *val = s;
                }
                break;

            case '<':
                if (*++s == '=') {
                    *k = C_le;
                    *val = ++s;
                } else {
                    *k = C_lt;
                    *val = s;
                }
                break;
#endif
#if !defined(NO_EQUAL_VALUE_EXTENSION_FOR_MUSTACH)
            case '=':
                *k = C_eq;
                *val = ++s;
                break;
#endif
#if !defined(NO_JSON_POINTER_EXTENSION_FOR_MUSTACH)
            case '~':
                switch (*++s) {
                    case '1': *o++ = '/'; break;
                    case '0': *o++ = '~'; break;
                    default: *o++ = *--s; break;
                }
                continue;
#endif
            case '.':
                *o++ = '/';
                continue;

            case '\\':
                *o++ = *++s;
                continue;

            default:
                *o++ = *s;
                continue;
        }
        break;
    }
    *o = '\0';
}

int
compare(struct kore_json_item *o, const char *value)
{
    switch (o->type) {
        case KORE_JSON_TYPE_NUMBER:
            return (o->data.number - strtod(value, NULL));

        case KORE_JSON_TYPE_INTEGER:
            return (o->data.integer - strtoll(value, NULL, 10));

        case KORE_JSON_TYPE_INTEGER_U64:
            return (o->data.u64 - strtoull(value, NULL, 10));

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
#if !defined(NO_COMPARE_VALUE_EXTENSION_FOR_MUSTACH)
        case C_lt: return (c < 0);
        case C_le: return (c <= 0);
        case C_gt: return (c > 0);
        case C_ge: return (c >= 0);
#endif
        default: return (0);
    }
}

#if !defined(NO_TINY_EXPR_EXTENSION_FOR_MUSTACH)
int
split_string_pbrk(char *s, const char *accept, char **out, size_t ele)
{
    int         count;
    char        **ap;

    if (ele == 0)
        return (0);

    ap = out;
    count = 0;

    if (ap < &out[ele - 1]) {
        *ap++ = s;
        count++;
    }

    while (ap < &out[ele - 1] &&
            (s = strpbrk(s, accept)) != NULL) {

        *s++ = '\0';
        if (*s == '\0') break;
        *ap++ = s;
        count++;
    }

    *ap = NULL;
    return (count);
}

double
eval(struct closure *cl, const char *expression)
{
    const char  *accept = "+-*/^%(), ";
    struct kore_json_item *o;
    double      d[256], result;
    char        *vars_s[256], *copy;
    te_variable vars[256];
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
#endif

int
islambda(struct closure *cl, int *depth)
{
    while (*depth && cl->stack[*depth].lambda == NULL)
        (*depth)--;

    return (*depth);
}

int
kore_mustach_hash(const char *template, struct kore_json_item *hash,
        int (*partial_cb)(const char *, struct mustach_sbuf *),
        int (*lambda_cb)(const char *, struct kore_buf *),
        char **result, size_t *length)
{
    int r;
    struct mustach_itf itf = {
        .start = start,
        .put = NULL,
        .enter = enter,
        .next = next,
        .leave = leave,
        .partial = partial,
        .get = get,
        .emit = emit,
        .stop = NULL
    };
    struct closure cl = {
        .context = hash,
        .partial_cb = partial_cb,
        .lambda_cb = lambda_cb
    };

    r = fmustach(template, &itf, &cl, 0);
    *result = (char *)kore_buf_release(&cl.result, length);
    return (r);
}

int
kore_mustach(const char *template, const char *data,
        int (*partial_cb)(const char *, struct mustach_sbuf *),
        int (*lambda_cb)(const char *, struct kore_buf *),
        char **result, size_t *length)
{
    struct kore_json    j;
    int                 r;

    if (!data) {
        return (kore_mustach_hash(template, NULL, partial_cb, lambda_cb, result, length));
    }

    kore_json_init(&j, data, strlen(data));
    if (kore_json_parse(&j)) {
        r = kore_mustach_hash(template, j.root, partial_cb, lambda_cb, result, length);
    } else {
        kore_log(LOG_NOTICE, "%s", kore_json_strerror());
        r = kore_mustach_hash(template, NULL, partial_cb, lambda_cb, result, length);
    }

    kore_json_cleanup(&j);
    return (r);
}
