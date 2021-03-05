#include <kore/kore.h>
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
    struct kore_json_item   *item;
    struct kore_buf         buf;
    int                     depth;
    size_t                  depth_max;

    struct {
        struct kore_json_item   *root;
        int                     iterate;
    } stack[MUSTACH_MAX_DEPTH];

    int     (*partial_cb)(const char *, struct mustach_sbuf *);
};

static int  start(void *);
static int  enter(void *, const char *);
static int  leave(void *);
static int  next(void *);
static int  get(void *, const char *, struct mustach_sbuf *);
static int  partial(void *, const char *, struct mustach_sbuf *);
static int  emit(void *, const char *, size_t, int, FILE *);

static struct kore_json_item    *json_get_item(struct kore_json_item *, const char *);
static char                     *json_get_self_value(struct kore_json_item *);
static void                     *keyval(const char *, char **, char **, enum comp *);
static int                      compare(struct kore_json_item *, const char *);
static int                      evalcomp(struct kore_json_item *, const char *, enum comp);

int
start(void *closure)
{
    struct closure          *cl = closure;

    cl->depth = 0;
    cl->depth_max = sizeof(cl->stack) / sizeof(cl->stack[0]);
    cl->stack[cl->depth].root = cl->item;
    cl->stack[cl->depth].iterate = 0;

    /* initialize our buffer with 8 kilobytes. */
    kore_buf_init(&cl->buf, 8 << 10);

    return (MUSTACH_OK);
}

int
enter(void *closure, const char *name)
{
    struct closure          *cl = closure;
    struct kore_json_item   *root, *item, *n;
    enum comp               k;
    char                    *key, *val;
    void                    *tofree;

    if (!cl->item)
        return (0);

    if ((size_t)++cl->depth >= cl->depth_max)
        return (MUSTACH_ERROR_TOO_DEEP);

#if !defined(NO_OBJECT_ITERATION_FOR_MUSTACH)
    if (name[0] == '*' && !name[1]) {
        if (cl->item->type == KORE_JSON_TYPE_OBJECT &&
                (n = TAILQ_FIRST(&cl->item->data.items)))
        {
            cl->stack[cl->depth].root = cl->item;
            cl->stack[cl->depth].iterate = 1;
            cl->item = n;
            return (1);
        }

        cl->depth--;
        return (0);
    }
#endif

    root = cl->item;
    tofree = keyval(name, &key, &val, &k);
    if ((item = json_get_item(root, key)) != NULL) {
        switch (item->type) {
            case KORE_JSON_TYPE_LITERAL:
                if (item->data.literal != KORE_JSON_TRUE) {
                    goto noenter;
                }
                cl->stack[cl->depth].iterate = 0;
                break;

            case KORE_JSON_TYPE_ARRAY:
                if ((n = TAILQ_FIRST(&item->data.items)) == NULL) {
                    goto noenter;
                }
                cl->item = n;
                cl->stack[cl->depth].iterate = 1;
                break;

            case KORE_JSON_TYPE_OBJECT:
#if !defined(NO_OBJECT_ITERATION_FOR_MUSTACH)
                if (val && val[0] == '*' &&
                        (n = TAILQ_FIRST(&item->data.items)) != NULL)
                {
                    cl->item = n;
                    cl->stack[cl->depth].iterate = 1;
                    break;
                }
#endif
                cl->item = item;
                cl->stack[cl->depth].iterate = 0;
                break;

            default:
                if (k != C_no && val &&
                        (val[0] == '!' ? evalcomp(item, &val[1], k) : !evalcomp(item, val, k))) {
                    goto noenter;
                }

                cl->stack[cl->depth].iterate = 0;
        }

        cl->stack[cl->depth].root = root;
        kore_free(tofree);
        return (1);
    }

noenter:
    kore_free(tofree);
    cl->depth--;
    return (0);
}

int
leave(void *closure)
{
    struct closure          *cl = closure;

    cl->item = cl->stack[cl->depth].root;
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
            (n = TAILQ_NEXT(cl->item, list)))
    {
        cl->item = n;
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
    char                    *key, *val, *value;
    void                    *tofree;

    sbuf->value = "";
    if (!cl->item)
        return (MUSTACH_OK);

    switch (name[0]) {
#if !defined(NO_OBJECT_ITERATION_FOR_MUSTACH)
        case '*':
            if (cl->item->name)
                sbuf->value = cl->item->name;
            break;
#endif
#if !defined(NO_SINGLE_DOT_EXTENSION_FOR_MUSTACH)
        case '.':
            if ((value = json_get_self_value(cl->item)) != NULL) {
                sbuf->value = value;
                sbuf->freecb = kore_free;
            }
            break;
#endif
        default:
            tofree = keyval(name, &key, &val, &k);
            if ((item = json_get_item(cl->item, key)) &&
                    ((val && (val[0] == '!' ? !evalcomp(item, &val[1], k) : evalcomp(item, val, k))) || k == C_no) &&
                    (value = json_get_self_value(item)))
            {
                sbuf->value = value;
                sbuf->freecb = kore_free;
            }
            kore_free(tofree);
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
    if (cl->item && (item = json_get_item(cl->item, name)) &&
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

        kore_buf_append(&cl->buf, b.data, b.offset);
        kore_buf_cleanup(&b);
    } else {
        kore_buf_append(&cl->buf, buffer, size);
    }
    return (MUSTACH_OK);
}
        
struct kore_json_item *
json_get_item(struct kore_json_item *root, const char *name)
{
    struct kore_json_item   *item = NULL;
    size_t  i, l;

    int types[] = {
        KORE_JSON_TYPE_OBJECT,
        KORE_JSON_TYPE_ARRAY,
        KORE_JSON_TYPE_STRING,
        KORE_JSON_TYPE_NUMBER,
        KORE_JSON_TYPE_LITERAL,
        KORE_JSON_TYPE_INTEGER,
        KORE_JSON_TYPE_INTEGER_U64,
    };
    l = sizeof(types) / sizeof(types[0]);

    if (!name)
        return (0);

    for (i = 0; i < l; i++) {
        if ((item = kore_json_find(root, name, types[i])) != NULL)
            break;
    }

    return (item);
}

char *
json_get_self_value(struct kore_json_item *root)
{
    size_t          len;
    char            b[256], *name, *v = 0;
    struct kore_buf buf;
   
    switch (root->type) {
        case KORE_JSON_TYPE_STRING:
            v = kore_strdup(root->data.string);
            break;

        case KORE_JSON_TYPE_NUMBER:
            snprintf(b, sizeof(b), "%.9g", root->data.number);
            v = kore_strdup(b);
            break;

        case KORE_JSON_TYPE_LITERAL:
            if (root->data.literal == KORE_JSON_TRUE)
                v = kore_strdup("true");
            else if (root->data.literal == KORE_JSON_FALSE)
                v = kore_strdup("false");
            else
                v = kore_strdup("null");
            break;

        case KORE_JSON_TYPE_INTEGER:
            snprintf(b, sizeof(b), "%ld", root->data.s64);
            v = kore_strdup(b);
            break;

        case KORE_JSON_TYPE_INTEGER_U64:
            snprintf(b, sizeof(b), "%lu", root->data.u64);
            v = kore_strdup(b);
            break;

        case KORE_JSON_TYPE_OBJECT:
        case KORE_JSON_TYPE_ARRAY:
            name = root->name;
            root->name = 0;
            kore_buf_init(&buf, 1024);
            kore_json_item_tobuf(root, &buf);
            kore_buf_append(&buf, "\0", 1);
            v = (char *)kore_buf_release(&buf, &len);
            root->name = name;
            break;
    }

    return (v);
}

void *
keyval(const char *in, char **key, char **val, enum comp *k)
{
    char    *s, *o;

    *key = kore_strdup(in);
    *val = 0;
    *k = C_no;

    for (o = s = *key; *s != '\0'; s++) {
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
            case '.':
                *o++ = '/';
                continue;
#if !defined(NO_JSON_POINTER_EXTENSION_FOR_MUSTACH)
            case '~':
                switch (*++s) {
                    case '1': *o++ = '/'; break;
                    case '0': *o++ = '~'; break;
                    default: *o++ = *--s; break;
                }
                continue;
#endif
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

    /* free this */
    return (*key);
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
    }

    return (0);
}

int
evalcomp(struct kore_json_item *o, const char *value, enum comp k)
{
    int     r, c;

    c = compare(o, value);
    switch (k) {
        case C_eq: r = c == 0; break;
#if !defined(NO_COMPARE_VALUE_EXTENSION_FOR_MUSTACH)
        case C_lt: r = c < 0; break;
        case C_le: r = c <= 0; break;
        case C_gt: r = c > 0; break;
        case C_ge: r = c >= 0; break;
#endif
        default: r = 0; break;
    }

    return (r);
}

int
kore_mustach(const void *template, const void *data,
        int (*partial_cb)(const char *, struct mustach_sbuf *), void **result, size_t *length)
{
    struct closure      cl = { 0 };
    struct kore_json    j;
    int  r;

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

    /* used in partial */
    cl.partial_cb = partial_cb;

    if (!data) {
        r = fmustach(template, &itf, &cl, 0);
        *result = kore_buf_release(&cl.buf, length);
        return (r);
    }

    kore_json_init(&j, data, strlen(data));
    if (kore_json_parse(&j)) {
        cl.item = j.root;
    } else {
        kore_log(LOG_NOTICE, "%s", kore_json_strerror(&j));
    }

    r = fmustach(template, &itf, &cl, 0);
    *result = kore_buf_release(&cl.buf, length);
    kore_json_cleanup(&j);
    return (r);
}
