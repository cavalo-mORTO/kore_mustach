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
 * Flags specific to mustach wrap
 */
#define Mustach_With_SingleDot    4
#define Mustach_With_Equal        8
#define Mustach_With_Compare      16
#define Mustach_With_JsonPointer  32
#define Mustach_With_ObjectIter   64
#define Mustach_With_IncPartial   128
#define Mustach_With_EscFirstCmp  256
#define Mustach_With_TinyExpr     512

/*
 * kore_mustach - Renders the mustache 'template' in 'result' for 'data'.
 *
 * @template:   the template string to instanciate
 * @data:       the data string to be parsed as a kore_json object
 * @partial_cb: the function to be called in the interface partial
 * @lambda_cb:  the function to be called in the interface lambda
 * @flags:      the flags passed. @see https://gitlab.com/jobol/mustach#extensions
 * @result:     the pointer receiving the result when 0 is returned
 * @size:       the size of the returned result
 *
 * Returns 0 in case of success, -1 with errno set in case of system error
 * a other negative value in case of error.
 *
 * Function pointers passed to kore_mustach
 *
 * @partial_cb: If defined (can be NULL), returns in 'sbuf' the content of the
 *              partial of 'name'. @see mustach_sbuf.
 *
 * @lambda_cb: If defined (can be NULL). 'buf' contains the content of the
 *             lambda of 'name', already rendered. User is free to manipulate
 *             the content of 'buf'. 'buf' or its contents must not be freed.
 *             @see kore_buf.
 *
 *             The lambda must be defined as a string consisting exclusively of
 *             '(=>)' in the hash.
 */
int kore_mustach(const char *template, const char *data,
        int (*partial_cb)(const char *name, struct mustach_sbuf *sbuf),
        int (*lambda_cb)(const char *name, struct kore_buf *buf),
        int flags, char **result, size_t *length);
/*
 * kore_mustach_json - Same as kore_mustach except instead of accepting a json
 *              string it requires a kore_json_item object.
 */
int kore_mustach_json(const char *template, struct kore_json_item *json,
        int (*partial_cb)(const char *name, struct mustach_sbuf *sbuf),
        int (*lambda_cb)(const char *name, struct kore_buf *buf),
        int flags, char **result, size_t *length);
#endif
