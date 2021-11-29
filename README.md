# Introduction to kore mustach

`kore mustach` is a [kore](https://kore.io) integration of the C implementation
`mustach` [gitlab](https://gitlab.com/jobol/mustach), of [mustache](http://mustache.github.io "main site for mustache")
template specification.

Requires kore latest [commit](https://git.kore.io/kore).


## Using kore mustach

The file **mustach.h** is the main documentation. Look at it.

The current source files are:

- **mustach.c** core implementation of mustache in C
- **mustach.h** header file for core definitions
- **kore_mustach.h** header file for integration function definitions
- **kore_mustach.c** implementation of mustach with kore
- **example/** example usage of this implementation

To use this implementation run
```
make install
```
and link with -lkore_mustach. Might need to specify -L/usr/local/lib -Wl,-R/usr/local/lib.


## Lambda support

This implementation supports lambdas. Check kore_mustach.h for details.
