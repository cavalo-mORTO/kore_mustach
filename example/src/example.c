#include <ctype.h>
#include <kore/kore.h>
#include <kore/hooks.h>
#include <kore/http.h>
#include <mustach/mustach.h>
#include <mustach/kore_mustach.h>
#include "assets.h"

int asset_serve_mustach(struct http_request *, int, const void *, const void *);
void tick(void *, uint64_t);

int hello(struct http_request *);
int handler(struct http_request *);

void upper(struct kore_buf *);
void lower(struct kore_buf *);
void topipe(struct kore_buf *);

static const char *fpaths[] = { "assets" };

static struct lambda l[] = {
    {"upper", upper},
    {"lower", lower},
    {"topipe", topipe},
};

static struct {
    const char *uri;
    const void *template;
    const void *data;
} tests[] = {
    {"/t1", asset_test1_must, asset_test1_json},
    {"/t2", asset_test2_must, asset_test2_json},
    {"/t3", asset_test3_must, asset_test3_json},
    {"/t4", asset_test4_must, asset_test4_json},
    {"/t5", asset_test5_must, asset_test5_json},
    {"/t6", asset_test6_must, asset_test6_json},
};

void
kore_parent_configure(int argc, char **argv)
{
    kore_default_getopt(argc, argv);

    kore_mustach_sys_init();
    kore_mustach_bind_partials(fpaths, sizeof(fpaths) / sizeof(fpaths[0]));
    kore_mustach_bind_lambdas(l, sizeof(l) / sizeof(l[0]));
}

void
kore_worker_configure(void)
{
    /* if you want to track changes in your partials at runtime */
    kore_timer_add(tick, 1000, NULL, 0);
}

void
kore_worker_teardown(void)
{
    kore_mustach_sys_cleanup();
}

void
tick(void *unused, uint64_t now)
{
    (void)unused;
    (void)now;

    kore_mustach_bind_partials(fpaths, sizeof(fpaths) / sizeof(fpaths[0]));
}

int
handler(struct http_request *req)
{
    size_t i, len;

    len = sizeof(tests) / sizeof(tests[0]);
    for (i = 0; i < len; i++) {
        if (!strcmp(req->path, tests[i].uri))
            break;
    }

    if (i < len)
        return (asset_serve_mustach(req, 200, tests[i].template, tests[i].data));
    else
        http_response(req, 500, NULL, 0);

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
