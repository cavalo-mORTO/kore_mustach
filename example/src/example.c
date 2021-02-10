#include <kore/kore.h>
#include <kore/http.h>

#include "assets.h"

int     asset_serve_mustach(struct http_request *, int, const void *, const void *);

int		page(struct http_request *);
int     test1(struct http_request *);
int     test2(struct http_request *);
int     test3(struct http_request *);
int     test4(struct http_request *);
int     test5(struct http_request *);
int     test6(struct http_request *);

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
