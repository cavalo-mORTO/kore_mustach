#include <kore/kore.h>

#include "mustach.h"
#include "kore_mustach.h"

struct closure {
    struct kore_json_item   *item;
    struct kore_buf         buf;
    int                     depth;

    struct {
        struct kore_json_item   *root;
        struct kore_json_item   *section;
        int                     iterate;
    } stack[MUSTACH_MAX_DEPTH];

    const void *(*partial_cb)(const char *);
};

static int  start(void *);
static int  enter(void *, const char *);
static int  leave(void *);
static int  next(void *);
static int  get(void *, const char *, struct mustach_sbuf *);
static int  partial(void *, const char *, struct mustach_sbuf *);
static int  emit(void *, const char *, size_t, int, FILE *);

static struct kore_json_item    *json_get_item(struct kore_json_item *, const char *);
static char                     *json_get_item_value(struct kore_json_item *, const char *);
static char                     *json_get_self_value(struct kore_json_item *);

#if !defined(NO_EQUAL_VALUE_EXTENSION_FOR_MUSTACH)
static void     *key_for_kore_json(const char *, char **, char **);
static int      value_is(char *, char *);
#endif

int
start(void *closure)
{
    struct closure          *cl = closure;

    cl->depth = 0;
    cl->stack[cl->depth].root = cl->item;
    cl->stack[cl->depth].section = 0;
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
    char                    *value;
#if !defined(NO_EQUAL_VALUE_EXTENSION_FOR_MUSTACH)
    char                    *k, *v;
    void                    *tofree;
#endif

    if (!cl->item)
        return (0);

    if ((size_t)++cl->depth >= (sizeof(cl->stack) / sizeof(cl->stack[0])))
        return (MUSTACH_ERROR_TOO_DEEP);

#if !defined(NO_OBJECT_ITERATION_FOR_MUSTACH)
    if (name[0] == '*' && !name[1]) {
        if (cl->item->type <= KORE_JSON_TYPE_ARRAY &&
                (n = TAILQ_FIRST(&cl->item->data.items)))
        {
            cl->stack[cl->depth].root = cl->item;
            cl->stack[cl->depth].section = cl->item;
            cl->stack[cl->depth].iterate = 1;
            cl->item = n;
            return (1);
        }

        cl->depth--;
        return (0);
    }
#endif

    root = cl->item;

#if !defined(NO_EQUAL_VALUE_EXTENSION_FOR_MUSTACH)
    tofree = key_for_kore_json(name, &k, &v);
    if ((item = json_get_item(root, k)) != NULL) {
#else
    if ((item = json_get_item(root, name)) != NULL) {
#endif
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
                if (v && v[0] == '*' && !v[1] && (n = TAILQ_FIRST(&item->data.items)) != NULL) {
                    cl->item = n;
                    cl->stack[cl->depth].iterate = 1;
                    break;
                }
#endif
                cl->item = item;
                cl->stack[cl->depth].iterate = 0;
                break;

            default:
                if ((value = json_get_self_value(item)) == NULL) {
                    kore_free(value);
                    goto noenter;
                }
#if !defined(NO_EQUAL_VALUE_EXTENSION_FOR_MUSTACH)
                if (!value_is(value, v)) {
                    kore_free(value);
                    goto noenter;
                }
#endif
                kore_free(value);
                cl->stack[cl->depth].iterate = 0;
        }

        cl->stack[cl->depth].root = root;
        cl->stack[cl->depth].section = item;
#if !defined(NO_EQUAL_VALUE_EXTENSION_FOR_MUSTACH)
        kore_free(tofree);
#endif
        return (1);
    }

noenter:
#if !defined(NO_EQUAL_VALUE_EXTENSION_FOR_MUSTACH)
    kore_free(tofree);
#endif
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
    struct closure  *cl = closure;
    char    *value;
#if !defined(NO_EQUAL_VALUE_EXTENSION_FOR_MUSTACH)
    char    *k, *v;
    void    *tofree;
#endif

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
#if !defined(NO_EQUAL_VALUE_EXTENSION_FOR_MUSTACH)
        default:
            tofree = key_for_kore_json(name, &k, &v);
            value = json_get_item_value(cl->item, k);
            if (value_is(value, v)) {
                sbuf->value = value;
                sbuf->freecb = kore_free;
            } else {
                kore_free(value);
            }
            kore_free(tofree);
#else
        default:
            if ((value = json_get_item_value(cl->item, name))) {
                sbuf->value = value;
                sbuf->freecb = kore_free;
            }
#endif
    }

    return (MUSTACH_OK);
}

int
partial(void *closure, const char *name, struct mustach_sbuf *sbuf)
{
    struct closure  *cl = closure;
    const char      *s;

    sbuf->value = "";
    if (cl->item && (s = json_get_item_value(cl->item, name)) != NULL) {
        sbuf->value = s;
        sbuf->freecb = kore_free;
    } else if (cl->partial_cb && (s = cl->partial_cb(name)) != NULL) {
        sbuf->value = s;
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
        // kore_buf_replace_string(&b, "\\", "&#39;", 5);
        // kore_buf_replace_string(&b, "\"", "&quot;", 6);
        kore_buf_replace_string(&b, "<", "&lt;", 4);
        kore_buf_replace_string(&b, ">", "&gt;", 4);
        // kore_buf_replace_string(&b, "/", "&#x2F;", 6);

        kore_buf_append(&cl->buf, b.data, b.offset);
        kore_buf_cleanup(&b);
    } else {
        kore_buf_append(&cl->buf, buffer, size);
    }
    return (MUSTACH_OK);
}
        
char *
json_get_item_value(struct kore_json_item *root, const char *name)
{
    struct kore_json_item   *item;
    char    *v = NULL;

    if ((item = json_get_item(root, name)) != NULL)
        v = json_get_self_value(item);

    return (v);
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
            snprintf(b, sizeof(b), "%f", root->data.number);
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

#if !defined(NO_EQUAL_VALUE_EXTENSION_FOR_MUSTACH)
void *
key_for_kore_json(const char *in, char **key, char **value)
{
    char    *s, *o;

    *key = kore_strdup(in);
    *value = 0;

    for (o = s = *key; *s != '\0'; s++) {
        switch (*s) {
            case '*':
                *value = "*";
                break;

            case '=':
                *value = ++s;
                break;

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

    /* free this */
    return (*key);
}

int
value_is(char *value, char *v)
{
    if (!value)
        return (0);
    else if (!v)
        return (1);
    else
        return (v[0] == '!' ? strcmp(value, &v[1]) != 0 : strcmp(value, v) == 0);
}
#endif

int
kore_mustach(const void *template, const void *data, const void *(*partial_cb)(const char *), void **result, size_t *length)
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
        r = fmustach((const char *)template, &itf, &cl, 0);
        *result = kore_buf_release(&cl.buf, length);
        return (r);
    }

    kore_json_init(&j, data, strlen(data));
    if (kore_json_parse(&j)) {
        cl.item = j.root;
    }

    r = fmustach((const char *)template, &itf, &cl, 0);
    *result = kore_buf_release(&cl.buf, length);
    kore_json_cleanup(&j);
    return (r);
}
