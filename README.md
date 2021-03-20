# Introduction to kore mustach

`kore mustach` is a [kore](https://kore.io) integration of the C implementation
`mustach` [gitlab](https://gitlab.com/jobol/mustach), of [mustache](http://mustache.github.io "main site for mustache")
template specification.


## Using kore mustach

The file **mustach.h** is the main documentation. Look at it.

The current source files are:

- **mustach.c** core implementation of mustache in C
- **mustach.h** header file for core definitions
- **kore_mustach.c** implementation and integration of mustach with kore
- **example/** example usage of this implementation
- **example/gen_partial.sh** tool used to generate partials.c

To use this implementation run
```
make install
```
and link with -lkore_mustach.

Then run
```
./gen_partial.sh
```
to link your assets/* with their respective file names.

## Lambda implementation

This implementation includes support for lambdas.
A lambda name must be prefixed by '()' in the mustache tag.
