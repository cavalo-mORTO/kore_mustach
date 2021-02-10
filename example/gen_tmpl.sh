#!/bin/bash -f
dir="assets"

printf "Generating tmpl...\n"

src=""

printf -v buf "#include <kore/kore.h>
#include <kore/http.h>
#include <mustach/kore_mustach.h>

#include \"assets.h\"

const void  *get_tmpl_item(const char *);
int         asset_serve_mustach(struct http_request *, int, const void *, const void *);

static const struct tmpl {
    const char          fname[256];
    const void          *fp;
} tmpl_list[] = {\n"
src+=$buf

for file in `ls $dir`
do
    if [ -f $dir/$file ];then
        name=$( echo ${file%.*} | tr .- _)
        printf -v buf "\t{ \"%s\", asset_%s_%s },\n" "${dir}/${file}" "${name}" "${file#*.}"
        src+=$buf
    fi
done

printf -v buf "};\n
const void *
get_tmpl_item(const char *fname)
{
    const void *v = 0;
    size_t  i, l = sizeof(tmpl_list) / sizeof(tmpl_list[0]);
    for (i = 0; i < l; i++) {
        if (!strcmp(fname, tmpl_list[i].fname)) {
            v = tmpl_list[i].fp;
            break;
        }
    }
    return (v);
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
}";

src+=$buf

printf "Writing tmpl to src/tmpl.c\n"
printf "%s" "$src" > src/tmpl.c
