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

struct lambda; /* see below */

/*
 * kore_mustach - Renders the mustache 'template' in 'result' for 'data'.
 *
 * @req:        the http_request needed to include other files, only works with tls enabled. can be NULL
 * @template:   the template string to instanciate
 * @data:       the data string to be parsed as a kore_json object
 * @flags:      the flags passed. @see https://gitlab.com/jobol/mustach#extensions
 * @lambdas:    a null terminated list of lambdas. can be NULL
 * @result:     the pointer receiving the result when 0 is returned
 * @size:       the size of the returned result
 *
 * Returns 0 in case of success, -1 with errno set in case of system error
 * a other negative value in case of error.
 */
int kore_mustach(struct http_request *req, const char *template, const char *data,
        int flags, struct lambda *lambdas, char **result, size_t *length);
/*
 * kore_mustach_json - Same as kore_mustach except instead of accepting a json
 *              string it requires a kore_json_item object.
 */
int kore_mustach_json(struct http_request *req, const char *template, struct kore_json_item *json,
        int flags, struct lambda *lambdas, char **result, size_t *length);
/*
 * struct lambda - Bind a lambda's 'name' to its callback 'cb'.
 *
 * 'buf' contains the content of the lambda, already rendered.
 * User is free to manipulate the content of 'buf'.
 * 'buf' or its contents must not be freed. @see kore_buf.
 *
 * The lambda must be a string of 'name' that contains only '(=>)' in the hash.
 */
struct lambda {
    char    *name;
    void    (*cb)(struct kore_buf *buf);
};

/* kore_mustach_strerror - Log mustach's error */
const char *kore_mustach_strerror(void);

#endif
