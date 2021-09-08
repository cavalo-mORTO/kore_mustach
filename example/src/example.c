#include <ctype.h>
#include <kore/kore.h>
#include <kore/hooks.h>
#include <kore/http.h>
#include <mustach/mustach.h>
#include <mustach/kore_mustach.h>

#if defined(__linux__)
#include <kore/seccomp.h>

KORE_SECCOMP_FILTER("my_filter",
        KORE_SYSCALL_ALLOW(newfstatat))
#endif

#include "assets.h"

int asset_serve_mustach(struct http_request *, int, const void *, const void *);

int hello(struct http_request *);
int handler(struct http_request *);

void upper(struct kore_buf *);
void lower(struct kore_buf *);
void topipe(struct kore_buf *);

static struct lambda my_lambdas[] = {
    { "upper", upper },
    { "lower", lower },
    { "topipe", topipe },
    { NULL, NULL }
};

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
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        if (req->path[strlen(req->path) - 1] == tests[i].uri)
            return (asset_serve_mustach(req, 200, tests[i].template, tests[i].data));
    }

    http_response(req, 404, NULL, 0);
    return (KORE_RESULT_OK);
}

int
hello(struct http_request *req)
{
    const char  *json = "{\"hello\": \"hello world\","
        "\"msg\": \"This is an integration of mustache templates with kore.\","
        "\"literal\": true,"
        "\"upper\": \"(=>)\","
        "\"num\": 234.43}";

    return (asset_serve_mustach(req, 200, asset_hello_html, json));
}

int
asset_serve_mustach(struct http_request *req, int status, const void *template, const void *data)
{
    char *result;
    size_t len;

    kore_mustach("tls", template, data, Mustach_With_AllExtensions, my_lambdas, &result, &len);
    http_response(req, status, result, len);
    kore_free(result);
    return (KORE_RESULT_OK);
}

void
upper(struct kore_buf *b)
{
    uint8_t *c, *end = b->data + b->offset;

    for (c = b->data; c < end; c++) *c = toupper(*c);
}

void
lower(struct kore_buf *b)
{
    uint8_t *c, *end = b->data + b->offset;

    for (c = b->data; c < end; c++) *c = tolower(*c);
}

void
topipe(struct kore_buf *b)
{
    kore_buf_replace_string(b, " ", "|", 1);
}
