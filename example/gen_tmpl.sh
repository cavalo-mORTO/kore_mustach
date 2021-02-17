#!/bin/bash -f
dir="assets"

printf "Generating tmpl...\n"

src=""

printf -v buf "#include <kore/kore.h>
#include <kore/http.h>
#include <mustach/mustach.h>
#include <mustach/kore_mustach.h>

#include \"assets.h\"

int         get_tmpl_item(const char *, struct mustach_sbuf *);
int         asset_serve_mustach(struct http_request *, int, const void *, const void *);

static const struct tmpl {
    const char          *name;
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

printf -v buf "};
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
}";

src+=$buf

printf "Writing tmpl to src/tmpl.c\n"
printf "%s" "$src" > src/tmpl.c
