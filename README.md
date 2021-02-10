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
- **example/gen_tmpl.sh** tool used to generate tmpl.c

To use this implementation run
```
make install
```
and link with -lkore_mustach.

Then run
```
./gen_tmpl.sh
```
to link your assets/* with their respective file names.
