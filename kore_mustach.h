#ifndef KORE_MUSTACH_H
#define KORE_MUSTACH_H
/*
 * kore_mustach - Renders the mustache 'template' in 'result' for 'data'.
 *
 * @template:   the template string to instanciate
 * @data:       the data string to be parsed as a kore_json object
 * @partial_cb: the function to be called in the interface partial
 * @lambda_cb:  the function to be called in the interface lambda
 * @result:     the pointer receiving the result when 0 is returned
 * @size:       the size of the returned result
 *
 * Returns 0 in case of success, -1 with errno set in case of system error
 * a other negative value in case of error.
 */
int kore_mustach(const char *template, const char *data,
        int (*partial_cb)(const char *name, struct mustach_sbuf *sbuf),
        int (*lambda_cb)(const char *name, struct kore_buf *buf),
        char **result, size_t *length);
/*
 * Function pointers passed to kore_mustach
 *
 * @partial_cb: If defined (can be NULL), returns in 'sbuf' the content of the
 *              partial of 'name'. @see mustach_sbuf.
 *
 * @lambda_cb: If defined (can be NULL). 'buf' contains the content of the
 *             lambda of 'name'. User is free to manipulate the content of
 *             'buf'. 'buf' or its contents must not be freed.
 *             @see kore_buf.
 *
 *             A lambda name must be prefixed by '()' in the mustache tag.
 */
#endif
