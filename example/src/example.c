#include <ctype.h>
#include <kore/kore.h>
#include <kore/http.h>
#include <mustach/mustach.h>
#include <mustach/kore_mustach.h>

#include "assets.h"

int     partial_cb(const char *, struct mustach_sbuf *);
int     asset_serve_mustach(struct http_request *, int, const void *, const void *);

int		page(struct http_request *);
int     test1(struct http_request *);
int     test2(struct http_request *);
int     test3(struct http_request *);
int     test4(struct http_request *);
int     test5(struct http_request *);
int     test6(struct http_request *);
int     lambda(struct http_request *);

int     lambda_cb(const char *, struct kore_buf *);
int     upper(struct kore_buf *);
int     lower(struct kore_buf *);

int
page(struct http_request *req)
{
    const char  *json = "{\"hello\": \"hello world\", \"msg\": \"This is an integration of mustache templates with kore.\"}";

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
lambda(struct http_request *req)
{
    void    *result;
    size_t  len;

    kore_mustach(asset_lambda_must, NULL, partial_cb, lambda_cb, &result, &len);
    http_response(req, 200, result, len);

    kore_free(result);
    return (KORE_RESULT_OK);
}

int
lambda_cb(const char *name, struct kore_buf *b)
{
    const struct {
        const char  *name;
        int         (*fn)(struct kore_buf *);
    } lambdas[] = {
        { "upper", upper },
        { "lower", lower },
        { NULL, NULL },
    };

    int i = 0;
    while (lambdas[i].name && lambdas[i].fn) {
        if (!strcmp(lambdas[i].name, name)) {
            lambdas[i].fn(b);
            break;
        }
        i++;
    }

    return (KORE_RESULT_OK);
}

int
upper(struct kore_buf *b)
{
    uint8_t *c, *end = b->data + b->offset;

    for (c = b->data; c < end; c++) *c = toupper(*c);
    return (KORE_RESULT_OK);
}

int
lower(struct kore_buf *b)
{
    uint8_t *c, *end = b->data + b->offset;

    for (c = b->data; c < end; c++) *c = tolower(*c);
    return (KORE_RESULT_OK);
}
