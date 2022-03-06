#include <ctype.h>
#include <math.h>
#include <kore/kore.h>
#include <kore/http.h>
#include <mustach/mustach.h>
#include <mustach/kore_mustach.h>

#if defined(__linux__)
#include <kore/seccomp.h>

KORE_SECCOMP_FILTER("app", KORE_SYSCALL_ALLOW(newfstatat))
#endif

#include "tinyexpr.h"
#include "assets.h"

double eval(struct kore_json_item *, const char *);

int hello(struct http_request *);
int handler(struct http_request *);

void upper(struct kore_json_item *, struct kore_buf *);
void lower(struct kore_json_item *, struct kore_buf *);
void bold(struct kore_json_item *, struct kore_buf *);
void taxed_value(struct kore_json_item *, struct kore_buf *);
void tinyexpr(struct kore_json_item *, struct kore_buf *);

static struct {
    const char uri;
    const void *template;
    const void *data;
} tests[] = {
    {'1', asset_test1_must, asset_test1_json},
    {'2', asset_test2_must, asset_test2_json},
    {'3', asset_test3_must, asset_test3_json},
    {'4', asset_test4_must, asset_test4_json},
    {'5', asset_test5_must, asset_test5_json},
    {'6', asset_test6_must, asset_test6_json},
};

int
handler(struct http_request *req)
{
    struct kore_buf *result = NULL;

    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        if (req->path[strlen(req->path) - 1] == tests[i].uri) {
            if (!kore_mustach(tests[i].template, tests[i].data, Mustach_With_AllExtensions, &result))
                kore_log(LOG_NOTICE, kore_mustach_strerror());

            http_response(req, 200, result->data, result->offset);
            kore_buf_free(result);
            return (KORE_RESULT_OK);
        }
    }

    http_response(req, 404, NULL, 0);
    return (KORE_RESULT_OK);
}

int
hello(struct http_request *req)
{
    struct kore_buf *result = NULL;
    struct kore_json_item *item = kore_json_create_object(NULL, NULL);

    kore_json_create_string(item, "hello", "hello world");
    kore_json_create_string(item, "msg", "This is an integration of mustache templates with kore.");
    kore_json_create_literal(item, "literal", 1);
    kore_json_create_number(item, "num", 234.43);
    kore_json_create_string(item, "bold", "(=>)");
    kore_json_create_string(item, "upper", "(=>)");
    kore_json_create_string(item, "tinyexpr", "(=>)");

    if (!kore_mustach_json((const char *)asset_hello_html, item, Mustach_With_AllExtensions, &result))
        kore_log(LOG_NOTICE, kore_mustach_strerror());

    http_response_header(req, "content-type", "text/html");
    http_response(req, 200, result->data, result->offset);
    kore_json_item_free(item);
    kore_buf_free(result);
    return (KORE_RESULT_OK);
}

void upper(struct kore_json_item *ctx, struct kore_buf *b)
{
    uint8_t *c, *end = b->data + b->offset;

    for (c = b->data; c < end; c++) *c = toupper(*c);
}

void lower(struct kore_json_item *ctx, struct kore_buf *b)
{
    uint8_t *c, *end = b->data + b->offset;

    for (c = b->data; c < end; c++) *c = tolower(*c);
}

void taxed_value(struct kore_json_item *ctx, struct kore_buf *b)
{
    struct kore_json_item *o;

    kore_buf_reset(b);
    if ((o = kore_json_find_integer(ctx, "value")) != NULL)
        kore_buf_appendf(b, "%g", o->data.integer * 0.6);
}

void bold(struct kore_json_item *ctx, struct kore_buf *b)
{
    char *s = kore_strdup(kore_buf_stringify(b, NULL));

    kore_buf_reset(b);
    kore_buf_appendf(b, "<b> %s </b>", s);
    kore_free(s);
}

void tinyexpr(struct kore_json_item *ctx, struct kore_buf *b)
{
    char *s = kore_strdup(kore_buf_stringify(b, NULL));
    double d = eval(ctx, s);

    kore_buf_reset(b);
    kore_buf_appendf(b, "%g", d);
    kore_free(s);
}

struct kore_json_item *
json_get_item(struct kore_json_item *o, const char *name)
{
    struct kore_json_item   *item;
    uint32_t                type;

    for (type = KORE_JSON_TYPE_OBJECT;
            type <= KORE_JSON_TYPE_INTEGER_U64; type <<= 1) {

        if ((item = kore_json_find(o, name, type)) != NULL)
            return (item);

        if (kore_json_errno() != KORE_JSON_ERR_TYPE_MISMATCH)
            return (NULL);
    }

    return (NULL);
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
eval(struct kore_json_item *item, const char *expression)
{
    const char  *accept = "+-*/^%() ";
    struct kore_json_item *o;
    double      *d, result;
    char        **vars_s, *copy;
    te_variable *vars;
    te_expr     *expr;
    int         i, n, len = 128;

    d = kore_calloc(len, sizeof(*d));
    vars_s = kore_calloc(len, sizeof(*vars_s));
    vars = kore_calloc(len, sizeof(*vars));

    copy = kore_strdup(expression);
    len = split_string_pbrk(copy, accept, vars_s, len);

    n = 0;
    for (i = 0; i < len; i++) {
        if ((o = json_get_item(item, vars_s[i])) != NULL) {
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
                    continue;
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
    kore_free(d);
    kore_free(vars);
    kore_free(vars_s);
    return (result);
}
