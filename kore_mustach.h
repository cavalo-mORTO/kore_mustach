#ifndef KORE_MUSTACH_H
#define KORE_MUSTACH_H
int     kore_mustach(const void *, const void *,
        int (*)(const char *, struct mustach_sbuf *), 
        int (*)(const char *, struct kore_buf *),
        void **, size_t *);
#endif
