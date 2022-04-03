# Introduction to kore_mustach

`kore_mustach` is a [kore](https://kore.io) integration of the C implementation
`mustach` [gitlab](https://gitlab.com/jobol/mustach), of [mustache](http://mustache.github.io "main site for mustache")
template specification.

Requires kore latest [commit](https://git.kore.io/kore).

Compatible with kore release [tarballs](https://kore.io/source) version >= 4.0.0, if applying the patches.


## Using kore_mustach

The file **mustach.h** is the main documentation. Look at it.

The current source files are:

- **mustach.c** core implementation of mustache in C
- **mustach.h** header file for core definitions
- **kore_mustach.h** header file for integration function definitions
- **kore_mustach.c** implementation of mustach with kore
- **example/** example usage of this implementation

If you're using a kore release tarball, version must be >= 4.0.0, run
```
make patch
```

Install with
```
make install
```
and link with -lkore_mustach. Might need to specify -L/usr/local/lib -Wl,-R/usr/local/lib.


## Lambda support

This implementation supports lambdas. Check kore_mustach.h for details.


## Sample code
```c
struct kore_buf *result = NULL;

if (kore_mustach("hello {{name}}!", "{\"name\": \"kore\"}", Mustach_With_AllExtensions, &result)) {
    kore_log(LOG_NOTICE, kore_buf_stringify(result, NULL));
    kore_buf_free(result);
} else {
    kore_log(LOG_NOTICE, kore_mustach_strerror());
}
```
