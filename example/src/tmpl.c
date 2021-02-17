#include <kore/kore.h>
#include <kore/http.h>
#include <mustach/mustach.h>
#include <mustach/kore_mustach.h>

#include "assets.h"

int         get_tmpl_item(const char *, struct mustach_sbuf *);
int         asset_serve_mustach(struct http_request *, int, const void *, const void *);

static const struct tmpl {
    const char          *name;
    const void          *fp;
} tmpl_list[] = {
	{ "assets/hello.html", asset_hello_html },
	{ "assets/special.must", asset_special_must },
	{ "assets/special.mustache", asset_special_mustache },
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
	{ "assets/test5_2.mustache", asset_test5_2_mustache },
	{ "assets/test5_3.mustache", asset_test5_3_mustache },
	{ "assets/test5.json", asset_test5_json },
	{ "assets/test5.must", asset_test5_must },
	{ "assets/test5.ref", asset_test5_ref },
	{ "assets/test6.json", asset_test6_json },
	{ "assets/test6.must", asset_test6_must },
	{ "assets/test6.ref", asset_test6_ref },
};
static const size_t tmpl_len = sizeof(tmpl_list) / sizeof(tmpl_list[0]);

int
get_tmpl_item(const char *name, struct mustach_sbuf *sbuf)
{
    size_t  i;
    for (i = 0; i < tmpl_len; i++) {
        if (!strcmp(name, tmpl_list[i].name)) {
            sbuf->value = tmpl_list[i].fp;
            break;
        }
    }
    return (MUSTACH_OK);
}

int
asset_serve_mustach(struct http_request *req, int status, const void *template, const void *data)
{
    void    *r;
    size_t  l;

    kore_mustach(template, data, get_tmpl_item, &r, &l);
    http_response(req, status, r, l);

    kore_free(r);
    return (KORE_RESULT_OK);
}