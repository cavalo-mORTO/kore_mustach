#include <kore/kore.h>
#include <kore/http.h>
#include <mustach/mustach.h>
#include <mustach/kore_mustach.h>

#include "assets.h"

int         partial_cb(const char *, struct mustach_sbuf *);
int         asset_serve_mustach(struct http_request *, int, const void *, const void *);

static const struct {
    const char          *name;
    const void          *fp;
} partials[] = {
	{ "assets/hello.html", asset_hello_html },
	{ "assets/test1.json", asset_test1_json },
	{ "assets/test1.must", asset_test1_must },
	{ "assets/test1.ref", asset_test1_ref },
	{ "assets/test2.json", asset_test2_json },
	{ "assets/test2.must", asset_test2_must },
	{ "assets/test2.ref", asset_test2_ref },
	{ "assets/test3.json", asset_test3_json },
	{ "assets/test3.must", asset_test3_must },
	{ "assets/test3.ref", asset_test3_ref },
	{ "assets/test4.json", asset_test4_json },
	{ "assets/test4.must", asset_test4_must },
	{ "assets/test4.ref", asset_test4_ref },
	{ "assets/test5_2.must", asset_test5_2_must },
	{ "assets/test5_3.mustache", asset_test5_3_mustache },
	{ "assets/test5.json", asset_test5_json },
	{ "assets/test5.must", asset_test5_must },
	{ "assets/test5.ref", asset_test5_ref },
	{ "assets/test6.json", asset_test6_json },
	{ "assets/test6.must", asset_test6_must },
	{ "assets/test6.ref", asset_test6_ref },
	{ NULL, NULL }
};

int
partial_cb(const char *name, struct mustach_sbuf *sbuf)
{
    size_t  i = 0;

    sbuf->value = "";
    while (partials[i].name && partials[i].fp) {
        if (!strcmp(name, partials[i].name)) {
            sbuf->value = partials[i].fp;
            break;
        }
        i++;
    }
    return (MUSTACH_OK);
}

int
asset_serve_mustach(struct http_request *req, int status, const void *template, const void *data)
{
    char    *result;
    size_t  len;
    int     rc;

    rc = kore_mustach((const char *)template, (const char *)data, partial_cb, NULL, Mustach_With_AllExtensions, &result, &len);
    http_response(req, status, result, len);
    kore_free(result);

    return (rc == MUSTACH_OK);
}