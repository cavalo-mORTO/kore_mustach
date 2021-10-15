# version
MAJOR := 1
MINOR := 2
REVIS := 0

# installation settings
DESTDIR ?=
PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
LIBDIR  ?= $(PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include

# initial settings
VERSION := $(MAJOR).$(MINOR).$(REVIS)
SOVER := .$(MAJOR)
SOVEREV := .$(MAJOR).$(MINOR)

HEADERS = mustach.h kore_mustach.h
CFLAGS+=-Wall -Wmissing-declarations -Wshadow
CFLAGS+=-Wstrict-prototypes -Wmissing-prototypes
CFLAGS+=-Wpointer-arith -Wcast-qual -Wsign-compare
lib_LDFLAGS  = -shared -lm
lib_objs  = mustach.o kore_mustach.o

all: libmustach.so$(SOVEREV) libkore_mustach.so

install: all
	install -d $(DESTDIR)$(BINDIR)
	install -d $(DESTDIR)$(LIBDIR)
	install -d $(DESTDIR)$(INCLUDEDIR)/mustach
	install -m0644 $(HEADERS)    $(DESTDIR)$(INCLUDEDIR)/mustach
	install -m0755 libmustach.so* $(DESTDIR)$(LIBDIR)/
	ln -sf libmustach.so$(SOVEREV) $(DESTDIR)$(LIBDIR)/libmustach.so$(SOVER)
	ln -sf libmustach.so$(SOVEREV) $(DESTDIR)$(LIBDIR)/libmustach.so
	install -m0755 libkore_mustach.so $(DESTDIR)$(LIBDIR)/

uninstall:
	rm -f $(DESTDIR)$(LIBDIR)/libmustach.so*
	rm -f $(DESTDIR)$(LIBDIR)/libkore_mustach.so
	rm -rf $(DESTDIR)$(INCLUDEDIR)/mustach

libkore_mustach.so: $(lib_objs)
	$(CC) $(LDFLAGS) $(lib_LDFLAGS) -o libkore_mustach.so $(lib_objs)

libmustach.so$(SOVEREV): mustach.o
	$(CC) $(LDFLAGS) $(lib_LDFLAGS) -o libmustach.so$(SOVEREV) mustach.o

mustach.o:      mustach.h
kore_mustach.o: mustach.h kore_mustach.h

clean:
	rm -f libkore_mustach.so libmustach.so* *.o

.PHONY: install uninstall all clean
