// list processing package (for 2960/EMAS)    DAT 23/11/79
// warning - much of this code is machine dependent

#include "listlib.h"

//----------------------------------------------------------------------
// The KRC system is Copyright (c) D. A. Turner 1981
// All  rights reserved.  It is distributed as free software under the
// terms in the file "COPYING", which is included in the distribution.
//----------------------------------------------------------------------

#include <string.h>       // for strlen()
#include <ctype.h>        // for toupper()
#include <stdlib.h>       // for malloc()
#include <sys/time.h>     // for <sys/resource.h>
#include <sys/resource.h> // for set/getrlimit()
#include <stdlib.h>       // for getenv()
#include <stdio.h>        // for sscanf()

// #define DEBUG_GC 1

#ifndef HEAPSIZE
#define HEAPSIZE 128000
#endif

// space is the number of list cells in each SEMI-SPACE.
// on 2960/EMAS must be <=128K
int SPACE = HEAPSIZE;

// ATOMSPACE is DICMAX/atomsize, see later
static int DICMAX = 64000;

// max number of atoms which can be stored
// The actual number of atoms is less
// because their names are also stored there
static int ATOMSPACE;

// max no of chars in an atom
#define ATOMSIZE 255

// non-pointer value for the HD of an entry in cons space,
// indicating that it is an integer, stored in the TL field.
#define FULLWORD (NIL - 1)

// impossible value of pointer or integer used as flag during GC.
// just top bit set
#define GONETO ((list)(1ULL << (sizeof(list) * 8 - 1)))

static list CONSBASE;
static list CONSLIMIT;
static list CONSP;
static list OTHERBASE;

static list *STACKBASE;

// static struct atom *ATOMBASE;
static atom ATOMBASE;
static atom ATOMP;
static atom ATOMLIMIT;
static word NOGCS = 0, RECLAIMS = 0;
bool ATGC;
char *USERLIB;

#ifdef INSTRUMENT_KRC_GC
// are we currently in the garbage collector?
bool COLLECTING = false;
#endif

static atom HASHV[128];

// read buffer
static char BUFFER[ATOMSIZE + 1];

// current pointer in BUFFER
static word BUFP = 0;

int ARGC;

// program parameters
char **ARGV;

// forward declarations
static word hash(char *s, int len);
static void GC(void);
static void copy(list *p);
static void copyheads(void);

void main2();

int main(int argc, char **argv) {
  ARGC = argc;
  ARGV = argv;

  // detect when we are run from a #! script on a system that
  // passes all parameters from the #! line as a single one.
  // we get: argv[0]="/path/to/krc
  //         argv[1]="-n -e primes?"
  //         argv[2]="path/to/script"
  //         argv[3..]=args passed to the script

  if (argc > 1 && argv[1][0] == '-' && strchr(argv[1], ' ') != NULL) {

    int nspaces = 0;
    char *cp;

    // allocate space for new ARGV
    for (cp = argv[1] + 1; *cp; cp++) {
      if (*cp == ' ') {
        nspaces++;
      }
    }

    // each space generates one more argument
    ARGV = calloc(argc + nspaces, sizeof(char *));

    if (ARGV == NULL) {
      exit(1);
    }

    // rewrite ARGV splitting up the first arg
    // if we find "-e ", all the rest is a single expression
    ARGV[0] = argv[0];
    ARGC = 1;

    for (cp = argv[1]; *cp;) {
      // plant another argument
      ARGV[ARGC++] = cp;

      // find end of arg
      if (strncasecmp(cp, "-e ", 3) == 0) {
        // after "-e", all the rest is the expr to evaluate
        cp += 2;
        *cp++ = '\0';
        ARGV[ARGC++] = cp;
        break;
      }

      if (strchr(cp, ' ') == NULL) {
        // no more spaces
        break;
      }
      cp = strchr(cp, ' '), *cp++ = '\0';
    }

    // now copy the rest of ARGV: the script name and its args
    for (int i = 2; i < argc; i++) {
      ARGV[ARGC++] = argv[i];
    }
  }

  // terminal output should be unbuffered
  setvbuf(stdout, NULL, _IONBF, 0);

  // more Unix-ey stuff. The stack's soft limit is set to 8192K
  // on some Linux disributions, which makes your long-running KRC
  // program die pointlessly after the millionth prime number.
  // avoid this by upping the soft stack limit to the hard maximum.
  {
    struct rlimit rlim;

    if (getrlimit(RLIMIT_STACK, &rlim) == 0) {
      rlim.rlim_cur = rlim.rlim_max;
      setrlimit(RLIMIT_STACK, &rlim);
    }

    // it says that this can also affect stack growth
    if (getrlimit(RLIMIT_AS, &rlim) == 0) {
      rlim.rlim_cur = rlim.rlim_max;
      setrlimit(RLIMIT_AS, &rlim);
    }
  }

  // handle command line arguments that affect this file
  for (int i = 1; i < ARGC; i++) {
    if (ARGV[i][0] == '-') {
      switch (ARGV[i][1]) {
      case 'g':
        ATGC = true;
        break;
      case 'h':
        if (++i >= ARGC || (SPACE = atoi(ARGV[i])) <= 0) {
          bcpl_writes("krc: -h What?\n");
          exit(0);
        }
        break;
      case 'l':
        // doesn't logically belong in listlib
        if (++i >= ARGC) {
          bcpl_writes("krc: -l What?\n");
          exit(0);
        } else
          USERLIB = ARGV[i];
        break;
      case 'd':
        if (++i >= ARGC || (DICMAX = atoi(ARGV[i])) <= 0) {
          bcpl_writes("krc: -d What?\n");
          exit(0);
        }
        break;
      }
    }
  }

  // taking advantage of the fact that we have virtual memory, we set up
  // two copies of list space in order to be able to do garbage collection
  // by doing a graph copy from one space to the other
  ATOMSPACE = DICMAX / atomsize;
  CONSBASE = (list)malloc(SPACE * sizeof(*CONSBASE));

  if (CONSBASE == (void *)-1) {
    space_error("Not enough memory");
  }

  CONSP = CONSBASE;
  CONSLIMIT = CONSBASE + SPACE;
  OTHERBASE = (list)malloc(SPACE * sizeof(*CONSBASE));

  if (OTHERBASE == (void *)-1) {
    space_error("Not enough memory");
  }

  ATOMBASE = (atom)malloc(ATOMSPACE * sizeof(*ATOMBASE));

  if (ATOMBASE == (void *)-1) {
    space_error("Not enough memory");
  }

  ATOMP = ATOMBASE;
  ATOMLIMIT = ATOMBASE + ATOMSPACE;

  main2();
}

// a separate function finds STACKBASE, to avoid inclusion of any locals,
// temporaries and stacked stuff belonging to main().
void main2() {

  // marker to find stack base
  list n;
  STACKBASE = &n;

  // "GO" is the user's start routine
  GO();
}

/*** unused
word haveparam(word ch) {

  ch = toupper(ch);

  for (word i = 1; i < ARGC; i++) {
    if (ARGV[i][0] == '-' && toupper(ARGV[i][1]) == toupper(ch)) {
      return true;
    }
  }

  return false;

}
***/

// creates a list cell with x and y for its fields in CONSBASE
list cons(list x, list y) {

  if (CONSP >= (CONSLIMIT - 1)) {
    GC();
  }

  HD(CONSP) = x;
  TL(CONSP) = y;
  CONSP = CONSP + 1;

  return CONSP - 1;
}

#include <setjmp.h>

void GC2(jmp_buf *);
void GC3(jmp_buf *, list *stackend);

void GC() {
  // Put all registers onto the stack so that any pointers into
  // the cons space will be updated during the GC and put back
  // in the registers when GC3() returns here with longjmp.
  jmp_buf env;
  if (setjmp(env) == 0) {
    GC2(&env);
  }
}

// not static to avoid inlining
void GC2(jmp_buf *envp) {
  // get the address of the end of the stack
  // including the jmp_buf containing the registers but
  // excluding anything that the real GC() might push onto the stack
  // for its own purposes.
  list p;
  GC3(envp, &p);
}

// garbage collector - does a graph copy into the other semi-space
// not static to avoid inlining
void GC3(jmp_buf *envp, list *stackend) {

  // examine every pointer on the stack p is a pointer to pointer,
  // so incrementing it moved it up by the size of one pointer.
  list *p;

  // In main.c
  extern void hold_interrupts(), release_interrupts();

#ifdef DEBUG_GC
  int lastused = 0;
  fprintf(bcpl_OUTPUT, "\n<");

#define SHOW(name)                                                             \
  do {                                                                         \
    fprintf(bcpl_OUTPUT, name ":%d", (int)(CONSP - OTHERBASE) - lastused);     \
    lastused = CONSP - OTHERBASE;                                              \
    copyheads();                                                               \
    fprintf(bcpl_OUTPUT, "+%d ", (int)(CONSP - OTHERBASE) - lastused);         \
    lastused = CONSP - OTHERBASE;                                              \
  } while (0)
#else
#define SHOW(name)                                                             \
  do {                                                                         \
  } while (0)
#endif

#ifdef INSTRUMENT_KRC_GC
  COLLECTING = true;
#endif
  hold_interrupts();
  NOGCS = NOGCS + 1;

  if (ATGC) {
    bcpl_writes("<gc called>\n");
  }

  CONSP = OTHERBASE;

  // user's static variables etc.
  bases(copy);

  SHOW("bases");
  {
    for (word i = 0; i < 128; i++) {
      // val fields of atoms
      atom a = HASHV[i];
      while (a != 0) {
        copy((list *)&(VAL(a)));
        a = LINK(a);
      }
    }
  }
  SHOW("atoms");

  // runtime detection of stack growth direction
  if (STACKBASE < stackend) {
    // stack grow upwards
    for (p = STACKBASE + 1; p < stackend; p++) {
      if (CONSBASE <= (list)*p && (list)*p < CONSLIMIT) {
        // an aligned address in listspace
        if (((char *)*p - (char *)CONSBASE) % sizeof(struct list) == 0) {
          copy(p);
        }

        if (((char *)*p - (char *)CONSBASE) % sizeof(struct list) ==
            sizeof(struct list *)) {
          // pointer to a tail cell, which also needs updating
          *p = (list)((list *)*p - 1);
          copy(p);
          *p = (list)((list *)*p + 1);
        }
      }
    }
  } else {
    // stack grows downwards
    for (p = STACKBASE - 1; p > stackend; p--) {
      if (CONSBASE <= (list)*p && (list)*p < CONSLIMIT) {
        if (((char *)*p - (char *)CONSBASE) % sizeof(struct list) == 0) {
          // an aligned address in listspace
          copy(p);
        }
        if (((char *)*p - (char *)CONSBASE) % sizeof(struct list) ==
            sizeof(struct list *)) {
          // pointer to a tail cells, which also needs updating
          *p = (list)((list *)*p - 1);
          copy(p);
          *p = (list)((list *)*p + 1);
        }
      }
      if (p == (list *)(envp + 1)) {
        SHOW("stack");
#ifdef __GLIBC__
        // The jmp_buf has 128 bytes to save the signal mask, which
        // are not set and provide a window onto an area of the
        // stack which can contain old pointers to now unused parts
        // of CONSSPACE. Apart from copying old junk pointlessly,
        // it can makes the interpreter unable to recover from
        // an out-of-space condition when the junk happens to be
        // > 90% of the available space.
        // Here we make p hop over this nasty window to take it to
        // straight to the machine registers at the start of the
        // buffer.
        p = (list *)((char *)(&((*envp)->__jmpbuf)) +
                     sizeof((*envp)->__jmpbuf));
#endif
      }
    }
  }
  SHOW("regs");
#ifdef DEBUG_GC
  fprintf(bcpl_OUTPUT, ">\n");
#endif

  copyheads();

  // now swap semi-spaces
  {
    list hold = CONSBASE;
    CONSBASE = OTHERBASE;
    CONSLIMIT = OTHERBASE + SPACE;
    OTHERBASE = hold;
  }

  RECLAIMS = RECLAIMS + (CONSLIMIT - CONSP);
#if 0
      if( ATGC ) {
        fprintf(bcpl_OUTPUT, "<%d cells in use>\n",(int)(CONSP-CONSBASE));
      }
#else
  // don't call printf, as if leaves unaligned pointers into
  // cons space on the stack.
  if (ATGC) {
    bcpl_writes("<");
    bcpl_writen((word)(CONSP - CONSBASE));
    bcpl_writes(" cells in use>\n");
  }
#endif
  release_interrupts();

  // abandon job if space utilisation exceeds 90%
  if (CONSP - CONSBASE > (9 * SPACE) / 10) {
    space_error("Space exhausted");
  }

#ifdef INSTRUMENT_KRC_GC
  COLLECTING = false;
#endif
  longjmp(*envp, 1);
}

// p is the address of a list field
static void copy(list *p) {

  //   do $( bcpl_writes("copying ")
  //         printobj(*P)
  //         wrch('\n')  $) <>

  while (CONSBASE <= *p && *p < CONSLIMIT) {

    if (HD(*p) == GONETO) {
      *p = TL(*p);
      return;
    }

    list x = HD(*p);
    list y = TL(*p);
    list z = CONSP;
    HD(*p) = GONETO;
    TL(*p) = z;
    *p = z;
    HD(z) = x;
    TL(z) = y;
    CONSP = CONSP + 1;

    if (x == FULLWORD) {
      return;
    }

    p = &(TL(z));
  }
}

static void copyheads() {

  list z = OTHERBASE;
  while (!(z == CONSP)) {
    copy(&(HD(z)));
    z = z + 1;
  }
}

word iscons(list x)
#ifdef INSTRUMENT_KRC_GC
{
  if (CONSBASE <= x && x < CONSLIMIT) {
    if (((char *)x - (char *)CONSLIMIT) % sizeof(struct list) != 0) {
      fprintf(bcpl_OUTPUT, "\nMisaligned pointer %p in iscons\n", x);
      return false;
    }
    return HD(x) != FULLWORD;
  }
  return false;
}
#else
{
  return CONSBASE <= x && x < CONSLIMIT ? HD(x) != FULLWORD : false;
}
#endif

word isatom(list x) { return ATOMBASE <= (atom)x && (atom)x < ATOMP; }

word isnum(list x)
#ifdef INSTRUMENT_KRC_GC
{
  if (CONSBASE <= x && x < CONSLIMIT) {
    if (((char *)x - (char *)CONSLIMIT) % sizeof(struct list) != 0) {
      fprintf(bcpl_OUTPUT, "\nMisaligned pointer %p in isnum\n", x);
      return false;
    }
    return HD(x) == FULLWORD;
  }
  return false;
}
#else
{
  return CONSBASE <= x && x < CONSLIMIT ? HD(x) == FULLWORD : false;
}
#endif

// "stonum(n)" stores away the number n as a list object
// "getnum(x)" gets it out again.
// the numbers stored and recovered by mknum and getnum are 32 bit integers
// - take care not to leave them on the stack.

// GCC warning expected
list stonum(word n) { return cons(FULLWORD, (list)n); }

// GCC warning expected
word getnum(list x) { return (word)(TL(x)); }

// make an atom from a C string
// atoms are stored uniquely,
// mkatom uses a hashing algorithm to accomplish this efficiently.
atom mkatom(char *s) { return mkatomn(s, strlen(s)); }

// make an atom which might contain NULs
atom mkatomn(char *s, int len) {

  atom *bucket = &(HASHV[hash(s, len)]);
  atom *p = bucket;

  // n is size of string counted as the number of pointers it occupies
  word n;

  // search the appropriate bucket
  while (*p != 0) {
    if (len == LEN(*p) && memcmp(s, NAME(*p), (size_t)len) == 0) {
      return (atom)*p;
    }
    p = &(LINK(*p));
  }

  // create new atom
  // +1 for len, +1 for the \0, then round up to element size
  n = (1 + len + 1 + (sizeof(word *)) - 1) / sizeof(word *);

  if ((word **)ATOMP + OFFSET + n > (word **)ATOMLIMIT) {
    bcpl_writes("<string space exhausted>\n");
    exit(0);
  }

  *p = ATOMP;
  LINK(ATOMP) = 0;
  VAL(ATOMP) = NIL;
  LENA(ATOMP) = len;
  memcpy(NAME(ATOMP), s, (size_t)len);
  NAME(ATOMP)[len] = '\0';

  ATOMP = (atom)((word **)ATOMP + OFFSET + n);

  return *p;
}

// takes a name and returns a value in 0..127
static word hash(char *s, int len) {
  int h = len;

  if (len && s[0]) {

    h = h + s[0] * 37;
    len = len - 1;

    if (len && s[1]) {

      h = h + s[1];
      len = len - 1;

      if (len && s[2]) {

        h = h + s[2];
        len = len - 1;

        if (len && s[3])

          h = h + s[3];
      }
    }
  }

  return h & 0x7F;
}

// puts the character ch into a buffer,
void bufch(word ch) {

  if (BUFP >= ATOMSIZE) {
    space_error("Atom too big");
  }

  BUFFER[BUFP++] = ch;
}

// empties the buffer and returns an atom formed from the characters
// which had been placed in it (by calling "mkatom")
atom packbuffer() {

  atom result = mkatomn(BUFFER, BUFP);
  BUFP = 0;
  return result;
}

// tests atoms for alphabetical order
// does string a sort before string b?
bool alfa_ls(atom a, atom b) { return strcmp(NAME(a), NAME(b)) < 0; }

static void gcstats() {

  fprintf(bcpl_OUTPUT, "Cells claimed = %d, no of gc's = %d",
          (int)(RECLAIMS + (CONSP - CONSBASE) / 2), (int)NOGCS);
}

void resetgcstats() { NOGCS = 0, RECLAIMS = -(CONSP - CONSBASE); }

// forces a garbage collection
void force_gc() {

  // to compensate for calling too early
  RECLAIMS = RECLAIMS - (CONSLIMIT - CONSP);

  if (ATGC) {
    fprintf(bcpl_OUTPUT, "Max cells available = %d\n", SPACE);
  }
  GC();
}

void reportdic() {

  fprintf(bcpl_OUTPUT, "string space = %ld bytes",
          (long)(ATOMSPACE * atomsize));
  fprintf(bcpl_OUTPUT, ", used %ld\n", (long)((ATOMP - ATOMBASE) * atomsize));
}

// list post mortem
void listpm() {

  word empty = 0;

  bcpl_writes("\n LIST POST MORTEM\n");
  gcstats();
  fprintf(bcpl_OUTPUT, ", current cells = %d\n", (int)((CONSP - CONSBASE) / 2));

  if (BUFP > 0) {
    bcpl_writes("Buffer: ");
    for (word i = 0; i < BUFP; i++) {
      wrch(BUFFER[i]);
    }
    wrch('\n');
  }

  bcpl_writes("Atom buckets:\n");

  for (word i = 0; i < 128; i++) {

    if (HASHV[i] != 0) {

      atom p = HASHV[i];
      fprintf(bcpl_OUTPUT, "%d :\t", (int)i);

      while (!(p == 0)) {
        bcpl_writes(NAME(p));

        if (!(VAL(p) == NIL)) {
          bcpl_writes(" = ");
          printobj(VAL(p));
        }

        p = LINK(p);

        if (p != 0) {
          bcpl_writes("\n\t");
        }
      }

      wrch('\n');

    } else {

      empty = empty + 1;
    }
  }

  fprintf(bcpl_OUTPUT, "Empty buckets = %d\n", (int)empty);
}

// gives the length of list x
word length(list x) {

  word n = 0;

  while (!(x == NIL)) {
    x = TL(x);
    n = n + 1;
  }

  return n;
}

// says if a is = an element of x
word member(list x, list a) {

  while (!(x == NIL || HD(x) == a)) {
    x = TL(x);
  }

  return x != NIL;
}

// appends (a copy of) list x to the front of list y
list append(list x, list y) { return shunt(shunt(x, NIL), y); }

// reverses the list x
list reverse(list x) { return shunt(x, NIL); }

// appends reverse(x) to the list y
list shunt(list x, list y) {

  while (!(x == NIL)) {
    y = cons(HD(x), y);
    x = TL(x);
  }
  return y;
}

// destructively removes a from x (if present)
list sub1(list x, atom a) {

  if (x == NIL) {
    return NIL;
  }

  if (HD(x) == (list)a) {
    return TL(x);
  }

  {
    list *p = &(TL(x));
    while (!((*p == NIL) || HD(*p) == (list)a)) {
      p = &(TL(*p));
    }

    if (!(*p == NIL)) {
      *p = TL(*p);
    }

    return x;
  }
}

// determines if list objects x and y are isomorphic
word equal(list x, list y) {

  do {
    if (x == y) {
      return true;
    }

    if (isnum(x) && isnum(y)) {
      return getnum(x) == getnum(y);
    }

    if (!(iscons(x) && iscons(y) && equal(HD(x), HD(y)))) {
      return false;
    }

    x = TL(x), y = TL(y);
  } while (1);
}

// returns the n'th element of list x
list elem(list x, word n) {

  while (!(n == 1)) {
    x = TL(x);
    n = n - 1;
  }
  return HD(x);
}

// prints an arbitrary list object x
//
// renamed from printob
void printobj(list x) {
  // list x or atom

  if (x == NIL) {

    bcpl_writes("NIL");

  } else if (isatom(x)) {

    fprintf(bcpl_OUTPUT, "\"%s\"", NAME((atom)x));

  } else if (isnum(x)) {

    bcpl_writen(getnum(x));

  } else if (iscons(x)) {

    wrch('(');

    while (iscons(x)) {
      printobj(HD(x));
      wrch('.');
      x = TL(x);
    }

    printobj(x);
    wrch(')');

  } else {

    fprintf(bcpl_OUTPUT, "<%p>", x);
  }
}

#ifdef INSTRUMENT_KRC_GC
// debugging function: ensure that P is a valid pointer into cons space
// and bomb if not.
// "is OK cons"
list isokcons(list p) {

  list q;
  if (COLLECTING) {
    return p;
  }

  if (CONSBASE <= p && p < CONSLIMIT) {

    // (only even addresses in listspace count)
    if (((char *)p - (char *)CONSBASE) % sizeof(struct list) == 0) {
      return p;
    } else {
      fprintf(bcpl_OUTPUT, "\nHD() or TL() called on ODD address %p\n", p);
    }
  } else {
    fprintf(bcpl_OUTPUT, "\nHD() or TL() called on %p not in CONS space\n", p);
  }

  // cause segfault in caller
  return (list)0;
}
#endif
