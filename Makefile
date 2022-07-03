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
KORE_VERSION := $(strip $(shell kore -v))

HEADERS = kore_mustach.h
CFLAGS+=-Wall -Wmissing-declarations -Wshadow
CFLAGS+=-Wstrict-prototypes -Wmissing-prototypes
CFLAGS+=-Wpointer-arith -Wcast-qual -Wsign-compare
lib_LDFLAGS  = -shared -lm
lib_objs  = mustach.o kore_mustach.o

all: libkore_mustach.so

patch:
	test -f kore_mustach-$(KORE_VERSION).patch && patch -u -p0 < kore_mustach-$(KORE_VERSION).patch

install: all
	install -d $(DESTDIR)$(BINDIR)
	install -d $(DESTDIR)$(LIBDIR)
	install -d $(DESTDIR)$(INCLUDEDIR)/mustach
	install -m0644 $(HEADERS)    $(DESTDIR)$(INCLUDEDIR)/mustach
	install -m0755 libkore_mustach.so $(DESTDIR)$(LIBDIR)/
	+$(MAKE) -C mustach install

uninstall:
	rm -f $(DESTDIR)$(LIBDIR)/libmustach.so*
	rm -f $(DESTDIR)$(LIBDIR)/libkore_mustach.so
	rm -rf $(DESTDIR)$(INCLUDEDIR)/mustach
	+$(MAKE) -C mustach uninstall

libkore_mustach.so: $(lib_objs)
	$(CC) $(LDFLAGS) $(lib_LDFLAGS) -o libkore_mustach.so $(lib_objs)

mustach.o:
	+$(MAKE) -C mustach mustach.o
	ln -sf mustach/mustach.o .

kore_mustach.o: kore_mustach.h

clean:
	rm -f libkore_mustach.so libmustach.so* *.o
	+$(MAKE) -C mustach clean

.PHONY: install uninstall all clean
