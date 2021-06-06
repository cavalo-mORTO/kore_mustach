#include <ctype.h>
#include <kore/kore.h>
#include <kore/hooks.h>
#include <kore/http.h>
#include <mustach/mustach.h>
#include <mustach/kore_mustach.h>

#include "assets.h"

int     asset_serve_mustach(struct http_request *, int, const void *, const void *);

int		page(struct http_request *);
int     test1(struct http_request *);
int     test2(struct http_request *);
int     test3(struct http_request *);
int     test4(struct http_request *);
int     test5(struct http_request *);
int     test6(struct http_request *);

void     upper(struct kore_buf *);
void     lower(struct kore_buf *);
void     topipe(struct kore_buf *);

void
kore_parent_configure(int argc, char *argv[])
{
    const char *fpaths[] = {
        "assets",
        "foo"
    };
    struct lambda l[] = {
        {"upper", upper},
        {"lower", lower},
        {"topipe", topipe},
    };

    kore_mustach_sys_init();
    kore_mustach_bind_partials(fpaths, sizeof(fpaths) / sizeof(fpaths[0]));
    kore_mustach_bind_lambdas(l, sizeof(l) / sizeof(l[0]));
}

void
kore_parent_teardown(void)
{
    kore_mustach_sys_cleanup();
}

int
page(struct http_request *req)
{
    const char  *json = "{\"hello\": \"hello world\","
        "\"msg\": \"This is an integration of mustache templates with kore.\","
        "\"literal\": true,"
        "\"upper\": \"(=>)\","
        "\"num\": 234.43}";

    return (asset_serve_mustach(req, 200, asset_hello_html, json));
}

int
test1(struct http_request *req)
{
    return (asset_serve_mustach(req, 200, asset_test1_must, asset_test1_json));
}

int
test2(struct http_request *req)
{
    return (asset_serve_mustach(req, 200, asset_test2_must, asset_test2_json));
}

int
test3(struct http_request *req)
{
    return (asset_serve_mustach(req, 200, asset_test3_must, asset_test3_json));
}

int
test4(struct http_request *req)
{
    return (asset_serve_mustach(req, 200, asset_test4_must, asset_test4_json));
}

int
test5(struct http_request *req)
{
    return (asset_serve_mustach(req, 200, asset_test5_must, asset_test5_json));
}

int
test6(struct http_request *req)
{
    return (asset_serve_mustach(req, 200, asset_test6_must, asset_test6_json));
}

int
asset_serve_mustach(struct http_request *req, int status, const void *template, const void *data)
{
    char    *result;
    size_t  len;

    kore_mustach((const char *)template, (const char *)data, Mustach_With_AllExtensions, &result, &len);
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
