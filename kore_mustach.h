/*
 * Copyright (c) 2021 Miguel Rodrigues <miguelangelorodrigues@enta.pt>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef KORE_MUSTACH_H
#define KORE_MUSTACH_H

/**
 * Flags specific to mustach
 */
#define Mustach_With_SingleDot    4     /* obsolete, always set */
#define Mustach_With_Equal        8
#define Mustach_With_Compare      16
#define Mustach_With_JsonPointer  32
#define Mustach_With_ObjectIter   64
#define Mustach_With_IncPartial   128   /* obsolete, always set */
#define Mustach_With_EscFirstCmp  256

#undef  Mustach_With_AllExtensions
#define Mustach_With_AllExtensions  511

/*
 * kore_mustach - Renders the mustache 'template' in 'result' for 'data'.
 *
 * @template:   the template string to instanciate
 * @data:       the data string to be parsed as a kore_json object. can be NULL
 * @flags:      the flags passed. @see https://gitlab.com/jobol/mustach#extensions
 * @result:     the kore_buf receiving the result. Free it with kore_buf_free(), even in case of error. @see kore_buf.
 *              If you want to cache the result, don't free it after you're done. @see kore_mem_tag
 *
 * Returns KORE_RESULT_OK in case of success or KORE_RESULT_ERROR in case of error.
 */
int kore_mustach(const char *template, const char *data, int flags, struct kore_buf **result);
/*
 * kore_mustach_json - Same as kore_mustach except instead of accepting a json
 *              string it requires a kore_json_item object.
 */
int kore_mustach_json(const char *template, struct kore_json_item *json, int flags, struct kore_buf **result);

/*
 * A lambda must be a string consisting only of '(=>)' in the json hash.
 *
 * A lambda prototype is as follows:
 *      void (*cb)(struct kore_json_item *item, struct kore_buf *buf)
 * 'item' is the root object.
 * 'buf' contains the text within the lambda's tags, already rendered.
 *
 * The lambda will searched using kore_runtime_getcall(), which uses dlsym(3).
 * */

/* kore_mustach_errno - Return mustach's error code */
int kore_mustach_errno(void);

/* kore_mustach_strerror - Return mustach's error as string */
const char *kore_mustach_strerror(void);

#endif
