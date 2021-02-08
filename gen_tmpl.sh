#!/bin/bash -f
dir="assets"

printf "Generating tmpl...\n"

src=""

printf -v buf "#include <kore/kore.h>
#include \"assets.h\"

const uint8_t   *get_tmpl_item(const char *);

static const struct tmpl {
    const char          fname[256];
    const uint8_t       *fp;
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
}";
src+=$buf

printf "Writing tmpl to src/tmpl.c\n"
printf "%s" "$src" > src/tmpl.c
