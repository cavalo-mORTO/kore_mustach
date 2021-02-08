#include <kore/kore.h>
#include <kore/http.h>

#include "mustach.h"
#include "assets.h"

int     asset_serve_mustach(struct http_request *, int, const char *, const char *);
int     kore_mustach(const char *, const char *, char **, size_t *);

int test1(struct http_request *);
int test2(struct http_request *);
int test3(struct http_request *);
int test4(struct http_request *);
int test5(struct http_request *);
int test6(struct http_request *);

int
test1(struct http_request *req)
{
    return (asset_serve_mustach(req, 200, (const char *)asset_test1_must, (const char *)asset_test1_json));
}

int
test2(struct http_request *req)
{
    return (asset_serve_mustach(req, 200, (const char *)asset_test2_must, (const char *)asset_test2_json));
}

int
test3(struct http_request *req)
{
    return (asset_serve_mustach(req, 200, (const char *)asset_test3_must, (const char *)asset_test3_json));
}

int
test4(struct http_request *req)
{
    return (asset_serve_mustach(req, 200, (const char *)asset_test4_must, (const char *)asset_test4_json));
}

int
test5(struct http_request *req)
{
    return (asset_serve_mustach(req, 200, (const char *)asset_test5_must, (const char *)asset_test5_json));
}

int
test6(struct http_request *req)
{
    return (asset_serve_mustach(req, 200, (const char *)asset_test6_must, (const char *)asset_test6_json));
}

int
asset_serve_mustach(struct http_request *req, int status, const char *template, const char *data)
{
    char    *r;
    size_t  l;

    kore_mustach(template, data, &r, &l);
    http_response(req, status, r, l);

    kore_free(r);
    return (KORE_RESULT_OK);
}
