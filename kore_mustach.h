#ifndef KORE_MUSTACH_H
#define KORE_MUSTACH_H
int     kore_mustach(const void *, const void *,
        int (*partial_cb)(const char *, struct mustach_sbuf *), void **, size_t *);
#endif
