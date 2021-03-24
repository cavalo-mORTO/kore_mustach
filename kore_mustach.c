#define _GNU_SOURCE

#include <math.h>
#include <ctype.h>
#include <kore/kore.h>
#include "tinyexpr.h"
#include "mustach.h"
#include "kore_mustach.h"


#if defined(NO_EXTENSION_FOR_MUSTACH)
# undef  NO_SINGLE_DOT_EXTENSION_FOR_MUSTACH
# define NO_SINGLE_DOT_EXTENSION_FOR_MUSTACH
# undef  NO_EQUAL_VALUE_EXTENSION_FOR_MUSTACH
# define NO_EQUAL_VALUE_EXTENSION_FOR_MUSTACH
# undef  NO_COMPARE_VALUE_EXTENSION_FOR_MUSTACH
# define NO_COMPARE_VALUE_EXTENSION_FOR_MUSTACH
# undef  NO_OBJECT_ITERATION_FOR_MUSTACH
# define NO_OBJECT_ITERATION_FOR_MUSTACH
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
static int  lambda(void *, const char *, const char *, size_t, int, FILE *);

static struct kore_json_item    *json_get_item(struct kore_json_item *, const char *);
static char                     *json_get_self_value(struct kore_json_item *);
static void                     keyval(char *, char **, enum comp *);
static int                      compare(struct kore_json_item *, const char *);
static int                      evalcomp(struct kore_json_item *, const char *, enum comp);
static int                      split_string_pbrk(char *, const char *, char **, size_t);
static double                   eval(struct kore_json_item *, const char *);

static struct mustach_itf itf = {
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

int
start(void *closure)
{
    struct closure          *cl = closure;

    cl->depth = 0;
    cl->depth_max = sizeof(cl->stack) / sizeof(cl->stack[0]);
    cl->stack[0].root = cl->context;
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

    if (!cl->context)
        return (0);

    if ((size_t)++cl->depth >= cl->depth_max)
        return (MUSTACH_ERROR_TOO_DEEP);

    cl->stack[cl->depth].root = cl->context;

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
    if ((item = json_get_item(cl->context, key)) != NULL) {
        switch (item->type) {
            case KORE_JSON_TYPE_LITERAL:
                if (item->data.literal != KORE_JSON_TRUE) {
                    cl->depth--;
                    return (0);
                }
                cl->stack[cl->depth].iterate = 0;
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
                cl->stack[cl->depth].iterate = 0;
                break;

            default:
                if (k != C_no && val &&
                        (val[0] == '!' ? evalcomp(item, &val[1], k) : !evalcomp(item, val, k))) {
                    cl->depth--;
                    return (0);
                }
                if (k == C_no) cl->context = item;
                cl->stack[cl->depth].iterate = 0;
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
    double                  d;

    sbuf->value = "";
    if (!cl->context)
        return (MUSTACH_OK);

    switch (name[0]) {
#if !defined(NO_OBJECT_ITERATION_FOR_MUSTACH)
        case '*':
            if (cl->context->name)
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
            if ((item = json_get_item(cl->context, key)) &&
                    ((val && (val[0] == '!' ? !evalcomp(item, &val[1], k) : evalcomp(item, val, k))) || k == C_no) &&
                    (value = json_get_self_value(item)) != NULL)
            {
                sbuf->value = value;
                sbuf->freecb = kore_free;
                break;
            }

            d = eval(cl->context, name);
            if (!isnan(d) && asprintf(&value, "%.9g", d) > -1 ) {
                sbuf->value = value;
                sbuf->freecb = free;
            }
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
    if (cl->context && (item = json_get_item(cl->context, name)) &&
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
    struct kore_buf b;

    (void)file; /* unused */

    if (escape) {
        kore_buf_init(&b, size + 1);
        kore_buf_append(&b, buffer, size);

        kore_buf_replace_string(&b, "&", "&amp;", 5);
        kore_buf_replace_string(&b, "<", "&lt;", 4);
        kore_buf_replace_string(&b, ">", "&gt;", 4);
        /* kore_buf_replace_string(&b, "\\", "&#39;", 5); 
         * kore_buf_replace_string(&b, "\"", "&quot;", 6);
         * kore_buf_replace_string(&b, "/", "&#x2F;", 6); */

        kore_buf_append(&cl->result, b.data, b.offset);
        kore_buf_cleanup(&b);
    } else {
        kore_buf_append(&cl->result, buffer, size);
    }
    return (MUSTACH_OK);
}

int
lambda(void *closure, const char *name, const char *buffer, size_t size, int escape, FILE *file)
{
    struct closure      *cl = closure;
    struct closure      tmp = { 0 };
    char                *copy;
    int                 rc;

    /* unused */
    (void)escape;

    copy = kore_malloc(size + 1);
    kore_strlcpy(copy, buffer, size + 1);

    /* copy closure */
    tmp.context = cl->context;
    tmp.partial_cb = cl->partial_cb;
    tmp.lambda_cb = cl->lambda_cb;

    /* render content */
    rc = fmustach(copy, &itf, &tmp, 0);
    kore_free(copy);

    if (rc < 0) {
        kore_buf_cleanup(&tmp.result);
        return (rc);
    }

    if (cl->lambda_cb)
        cl->lambda_cb(name, &tmp.result);

    emit(closure, (const char *)tmp.result.data, tmp.result.offset, 0, file);
    kore_buf_cleanup(&tmp.result);

    return (MUSTACH_OK);
}
        
struct kore_json_item *
json_get_item(struct kore_json_item *o, const char *name)
{
    struct kore_json_item   *item;
    int                     type;

    if (!name)
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
            return (o->data.s64 - strtoll(value, NULL, 10));

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

int
split_string_pbrk(char *s, const char *accept, char **out, size_t ele)
{
	int         count;
	char        **ap;

	if (ele == 0)
		return (0);

    ap = out;
    *ap++ = s;
	count = 1;
	for (; ap < &out[ele - 1] &&
            (s = strpbrk(s, accept)) != NULL;) {

        *s = '\0';
        if (*++s == '\0') break;

        *ap++ = s;
        count++;
    }

	*ap = NULL;
	return (count);
}

double
eval(struct kore_json_item *data, const char *expression)
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
        if ((o = json_get_item(data, vars_s[i])) != NULL) {
            switch (o->type) {
                case KORE_JSON_TYPE_NUMBER:
                    d[i] = o->data.number;
                    break;
                case KORE_JSON_TYPE_INTEGER:
                    d[i] = o->data.s64;
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
kore_mustach(const char *template, const char *data,
        int (*partial_cb)(const char *, struct mustach_sbuf *),
        int (*lambda_cb)(const char *, struct kore_buf *),
        char **result, size_t *length)
{
    struct closure      cl = { 0 };
    struct kore_json    j;
    int                 r;

    /* used in partial */
    cl.partial_cb = partial_cb;

    /* used in lambda */
    cl.lambda_cb = lambda_cb;

    if (!data) {
        r = fmustach(template, &itf, &cl, 0);
        *result = (char *)kore_buf_release(&cl.result, length);
        return (r);
    }

    kore_json_init(&j, data, strlen(data));
    if (kore_json_parse(&j)) {
        cl.context = j.root;
    } else {
        kore_log(LOG_NOTICE, "%s", kore_json_strerror(&j));
    }

    r = fmustach(template, &itf, &cl, 0);
    *result = (char *)kore_buf_release(&cl.result, length);
    kore_json_cleanup(&j);
    return (r);
}
