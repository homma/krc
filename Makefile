# makefile for Kent Recursive Calculator
# (BCPL version translated into C)

PREFIX=/usr/local

BINDIR=$(PREFIX)/bin
LIBDIR="$(PREFIX)/lib/krc"
MANDIR=$(PREFIX)/share/man/man1

# To use alternate compilers, just go
#	CC=clang make clean all

# To set the default number of cells, use
#	HEAPSIZE=1000000 make clean all
# KRC will take 2*2*sizeof(pointer) times this amount of RAM:
# 16MB on a 32-bit machine, 32MB on a 64-bit machine.
# If you have a desktop system, aim for half the physical RAM.

HEAPSIZE?=128000		# Default heap size if unspecified

# -fno-omit-frame-pointer is necessary when optimizing with gcc or clang
# otherwise it makes an extra register available to functions which is
# NOT saved by setjmp/longjmp, so the garbage collector fails to update
# its contents, causing heap corruption.

CFLAGS+=-g -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast \
	-O2 -fno-omit-frame-pointer \
 	-DLINENOISE \
	-DLIBDIR='$(LIBDIR)' -DHEAPSIZE=$(HEAPSIZE)

SRCS= main.c reducer.c compiler.c lex.c listlib.c iolib.c \
      listlib.h comphdr.h reducer.h common.h common.h iolib.h \
      linenoise.h linenoise.c
OBJS= main.o reducer.o compiler.o lex.o listlib.o iolib.o \
      linenoise.o

krc: $(OBJS)
	@$(CC) $(CFLAGS) -o $@ $(OBJS)

listlib.o: listlib.h common.h iolib.h
lex.o:      comphdr.h listlib.h common.h iolib.h
compiler.o: comphdr.h listlib.h common.h iolib.h
reducer.o:  reducer.h comphdr.h listlib.h common.h iolib.h
main.o:     reducer.h comphdr.h listlib.h common.h iolib.h revision

linenoise.o: Makefile
main.o: Makefile
listlib.o: listlib.h common.h iolib.h

.c.o:
	@$(CC) $(CFLAGS) -c $<

.c.s:
	@$(CC) $(CFLAGS) -S $<

install: krc krclib/prelude krclib/lib1981 doc/krc.1
	install -d -m 755 $(BINDIR) $(LIBDIR) $(LIBDIR)/help $(MANDIR)
	install -s -m 755 krc $(BINDIR)
	install -c -m 644 krclib/prelude $(LIBDIR)
	install -c -m 644 krclib/lib1981 $(LIBDIR)
#	install -c -m 644 krclib/help/* $(LIBDIR)/help
	cp -P krclib/help/* $(LIBDIR)/help  #krclib/help contains symbolic links
	install -c -m 644 doc/krc.1 $(MANDIR)

uninstall: 
	rm -rf $(BINDIR)/krc $(LIBDIR) $(MANDIR)/krc.1

clean:
	@rm -f *.o *.s core
	make -C doc $@
