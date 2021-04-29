DESTDIR ?=
PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
LIBDIR  ?= $(PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include
SOVER = .0
SOVEREV = .0.99

HEADERS  = mustach.h kore_mustach.h
CFLAGS = -fpic -Wall -Wextra
lib_LDFLAGS  = -shared -lm
lib_objs  = mustach.o kore_mustach.o tinyexpr.o

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
kore_mustach.o: mustach.h kore_mustach.h tinyexpr.h
tinyexpr.o:		tinyexpr.h

clean:
	rm -f libkore_mustach.so libmustach.so$(SOVEREV) *.o

.PHONY: install uninstall
