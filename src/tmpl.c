#include <kore/kore.h>
#include "assets.h"

const uint8_t   *get_tmpl_item(const char *);

static const struct tmpl {
    const char          fname[256];
    const uint8_t       *fp;
} tmpl_list[] = {
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

const uint8_t *
get_tmpl_item(const char *fname)
{
    const uint8_t *v = 0;
    size_t  i, l = sizeof(tmpl_list) / sizeof(tmpl_list[0]);
    for (i = 0; i < l; i++) {
        if (!strcmp(fname, tmpl_list[i].fname)) {
            v = tmpl_list[i].fp;
            break;
        }
    }
    return (v);
}