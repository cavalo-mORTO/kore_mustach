#!/bin/bash -f
dir="assets"
src=""

printf "Generating partials...\n"
printf -v buf "#include <kore/kore.h>
#include <kore/http.h>
#include <mustach/mustach.h>
#include <mustach/kore_mustach.h>

#include \"assets.h\"

int         partial_cb(const char *, struct mustach_sbuf *);
int         asset_serve_mustach(struct http_request *, int, const void *, const void *);

static const struct {
    const char          *name;
    const void          *fp;
} partials[] = {\n"
src+=$buf

for file in `ls $dir`
do
    if [ -f $dir/$file ];then
        name=$( echo ${file%.*} | tr .- _)
        printf -v buf "\t{ \"%s\", asset_%s_%s },\n" "${dir}/${file}" "${name}" "${file#*.}"
        src+=$buf
    fi
done

printf -v buf "\t{ NULL, NULL }\n};

int
partial_cb(const char *name, struct mustach_sbuf *sbuf)
{
    size_t  i = 0;

    sbuf->value = \"\";
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
    int     r;

    r = kore_mustach((const char *)template, (const char *)data, partial_cb, NULL, &result, &len);
    http_response(req, status, result, len);
    kore_free(result);

    return (r == MUSTACH_OK);
}";

src+=$buf
printf "Writing asset to src/partials.c\n"
printf "%s" "$src" > src/partials.c
