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

static list CONSBASE, CONSLIMIT, CONSP, OTHERBASE;
static list *STACKBASE;

// static struct atom *ATOMBASE;
static atom ATOMBASE;
static atom ATOMP, ATOMLIMIT;
static word NOGCS = 0, RECLAIMS = 0;
bool ATGC;
char *USERLIB;

#ifdef INSTRUMENT_KRC_GC
// are we currently in the garbage collector?
bool COLLECTING = false;
#endif

static atom HASHV[128];

static char BUFFER[ATOMSIZE + 1];
static word BUFP = 0;

int ARGC;

// program parameters
char **ARGV;

// forward declarations
static word hash(char *S, int LEN);
static void GC(void);
static void copy(list *P);
static void copyheads(void);

void main2();

int main(int argc, char **argv) {
  int I;
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
    for (I = 2; I < argc; I++) {
      ARGV[ARGC++] = argv[I];
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
  for (I = 1; I < ARGC; I++) {
    if (ARGV[I][0] == '-') {
      switch (ARGV[I][1]) {
      case 'g':
        ATGC = true;
        break;
      case 'h':
        if (++I >= ARGC || (SPACE = atoi(ARGV[I])) <= 0) {
          bcpl_writes("krc: -h What?\n");
          exit(0);
        }
        break;
      case 'l':
        // doesn't logically belong in listlib
        if (++I >= ARGC) {
          bcpl_writes("krc: -l What?\n");
          exit(0);
        } else
          USERLIB = ARGV[I];
        break;
      case 'd':
        if (++I >= ARGC || (DICMAX = atoi(ARGV[I])) <= 0) {
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
  CONSP = CONSBASE, CONSLIMIT = CONSBASE + SPACE;
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
  list N;
  STACKBASE = &N;

  // "GO" is the user's start routine
  GO();
}

/*** unused
word haveparam(word CH) {

  word I;
  CH = toupper(CH);
  for (I = 1; I < ARGC; I++)
    if (ARGV[I][0] == '-' && toupper(ARGV[I][1]) == toupper(CH)) {
      return true;
    }
  return false;

}
***/

// creates a list cell with X and Y for its fields
list cons(list X, list Y) {

  if (CONSP >= (CONSLIMIT - 1)) {
    GC();
  }
  HD(CONSP) = X, TL(CONSP) = Y, CONSP = CONSP + 1;
  return CONSP - 1;
}

#include <setjmp.h>

void GC2(jmp_buf *);
void GC3(jmp_buf *, list *STACKEND);

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
  list P;
  GC3(envp, &P);
}

// garbage collector - does a graph copy into the other semi-space
// not static to avoid inlining
void GC3(jmp_buf *envp, list *STACKEND) {

  // examine every pointer on the stack
  // P is a pointer to pointer, so incrementing it
  // moved it up by the size of one pointer.
  list *P;

  // In main.c
  extern void hold_interrupts(), release_interrupts();

#ifdef DEBUG_GC
  int LASTUSED = 0;
  fprintf(bcpl_OUTPUT, "\n<");

#define SHOW(name)                                                             \
  do {                                                                         \
    fprintf(bcpl_OUTPUT, name ":%d", (int)(CONSP - OTHERBASE) - LASTUSED);     \
    LASTUSED = CONSP - OTHERBASE;                                              \
    copyheads();                                                               \
    fprintf(bcpl_OUTPUT, "+%d ", (int)(CONSP - OTHERBASE) - LASTUSED);         \
    LASTUSED = CONSP - OTHERBASE;                                              \
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
    word I;
    for (I = 0; I < 128; I++) {
      // val fields of atoms
      atom A = HASHV[I];
      while (!(A == 0)) {
        copy((list *)&(VAL(A)));
        A = LINK(A);
      }
    }
  }
  SHOW("atoms");

  // runtime detection of stack growth direction
  if (STACKBASE < STACKEND) {
    // stack grow upwards
    for (P = STACKBASE + 1; P < STACKEND; P++) {
      if (CONSBASE <= (list)*P && (list)*P < CONSLIMIT) {
        // an aligned address in listspace
        if (((char *)*P - (char *)CONSBASE) % sizeof(struct list) == 0) {
          copy(P);
        }

        if (((char *)*P - (char *)CONSBASE) % sizeof(struct list) ==
            sizeof(struct list *)) {
          // pointer to a tail cell, which also needs updating
          *P = (list)((list *)*P - 1);
          copy(P);
          *P = (list)((list *)*P + 1);
        }
      }
    }
  } else {
    // stack grows downwards
    for (P = STACKBASE - 1; P > STACKEND; P--) {
      if (CONSBASE <= (list)*P && (list)*P < CONSLIMIT) {
        if (((char *)*P - (char *)CONSBASE) % sizeof(struct list) == 0) {
          // an aligned address in listspace
          copy(P);
        }
        if (((char *)*P - (char *)CONSBASE) % sizeof(struct list) ==
            sizeof(struct list *)) {
          // Pointer to a tail cells, which also needs updating
          *P = (list)((list *)*P - 1);
          copy(P);
          *P = (list)((list *)*P + 1);
        }
      }
      if (P == (list *)(envp + 1)) {
        SHOW("stack");
#ifdef __GLIBC__
        // The jmp_buf has 128 bytes to save the signal mask, which
        // are not set and provide a window onto an area of the
        // stack which can contain old pointers to now unused parts
        // of CONSSPACE. Apart from copying old junk pointlessly,
        // it can makes the interpreter unable to recover from
        // an out-of-space condition when the junk happens to be
        // > 90% of the available space.
        // Here we make P hop over this nasty window to take it to
        // straight to the machine registers at the start of the
        // buffer.
        P = (list *)((char *)(&((*envp)->__jmpbuf)) +
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
    list HOLD = CONSBASE;
    CONSBASE = OTHERBASE, CONSLIMIT = OTHERBASE + SPACE, OTHERBASE = HOLD;
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
static void copy(list *P) {
  //   do $( bcpl_writes("copying ")
  //         printobj(*P)
  //         (*_WRCH)('\n')  $) <>
  while (CONSBASE <= *P && *P < CONSLIMIT) {
    if (HD(*P) == GONETO) {
      *P = TL(*P);
      return;
    }
    list X = HD(*P);
    list Y = TL(*P);
    list Z = CONSP;
    HD(*P) = GONETO;
    TL(*P) = Z;
    *P = Z;
    HD(Z) = X, TL(Z) = Y;
    CONSP = CONSP + 1;
    if (X == FULLWORD) {
      return;
    }
    P = &(TL(Z));
  }
}

static void copyheads() {

  list Z = OTHERBASE;
  while (!(Z == CONSP)) {
    copy(&(HD(Z)));
    Z = Z + 1;
  }
}

word iscons(list X)
#ifdef INSTRUMENT_KRC_GC
{
  if (CONSBASE <= X && X < CONSLIMIT) {
    if (((char *)X - (char *)CONSLIMIT) % sizeof(struct list) != 0) {
      fprintf(bcpl_OUTPUT, "\nMisaligned pointer %p in iscons\n", X);
      return false;
    }
    return HD(X) != FULLWORD;
  }
  return false;
}
#else
{
  return CONSBASE <= X && X < CONSLIMIT ? HD(X) != FULLWORD : false;
}
#endif

word isatom(list X) { return ATOMBASE <= (atom)X && (atom)X < ATOMP; }

word isnum(list X)
#ifdef INSTRUMENT_KRC_GC
{
  if (CONSBASE <= X && X < CONSLIMIT) {
    if (((char *)X - (char *)CONSLIMIT) % sizeof(struct list) != 0) {
      fprintf(bcpl_OUTPUT, "\nMisaligned pointer %p in isnum\n", X);
      return false;
    }
    return HD(X) == FULLWORD;
  }
  return false;
}
#else
{
  return CONSBASE <= X && X < CONSLIMIT ? HD(X) == FULLWORD : false;
}
#endif

// "stonum(N)" stores away the number N as a list object
// "getnum(X)" gets it out again.
// the numbers stored and recovered by mknum and getnum are 32 bit integers
// - take care not to leave them on the stack.

// GCC warning expected
list stonum(word N) { return cons(FULLWORD, (list)N); }

// GCC warning expected
word getnum(list X) { return (word)(TL(X)); }

// make an atom from a C string
// atoms are stored uniquely,
// mkatom uses a hashing algorithm to accomplish this efficiently.
atom mkatom(char *S) { return mkatomn(S, strlen(S)); }

// make an atom which might contain NULs
atom mkatomn(char *S, int LEN) {

  atom *BUCKET = &(HASHV[hash(S, LEN)]);
  atom *P = BUCKET;

  // N is size of string counted as the number of pointers it occupies
  word N;

  // search the appropriate bucket
  while (!(*P == 0)) {
    if (LEN == LEN(*P) && memcmp(S, PRINTNAME(*P), (size_t)LEN) == 0) {
      return (atom)*P;
    }
    P = &(LINK(*P));
  }

  // create new atom
  // +1 for len, +1 for the \0, then round up to element size
  N = (1 + LEN + 1 + (sizeof(word *)) - 1) / sizeof(word *);

  if ((word **)ATOMP + OFFSET + N > (word **)ATOMLIMIT) {
    bcpl_writes("<string space exhausted>\n");
    exit(0);
  }

  // *P = ATOMP, LINK(ATOMP) = 0, VAL(ATOMP) = NIL;
  // (NAME(ATOMP))[0] = LEN,
  // memcpy(NAME(ATOMP) + 1, S, (size_t)LEN),
  // NAME(ATOMP)[LEN + 1] = '\0';

  *P = ATOMP;
  LINK(ATOMP) = 0;
  VAL(ATOMP) = NIL;
  LENA(ATOMP) = LEN;
  memcpy(NAME(ATOMP), S, (size_t)LEN);
  NAME(ATOMP)[LEN] = '\0';

  ATOMP = (atom)((word **)ATOMP + OFFSET + N);

  return *P;
}

// takes a name and returns a value in 0..127
static word hash(char *S, int LEN) {
  int H = LEN;

  if (LEN && S[0]) {

    H = H + S[0] * 37;
    LEN = LEN - 1;

    if (LEN && S[1]) {

      H = H + S[1];
      LEN = LEN - 1;

      if (LEN && S[2]) {

        H = H + S[2];
        LEN = LEN - 1;

        if (LEN && S[3])

          H = H + S[3];
      }
    }
  }

  return H & 0x7F;
}

// puts the character ch into a buffer,
void bufch(word ch) {
  if (BUFP >= ATOMSIZE) {
    space_error("Atom too big");
  }
  BUFFER[BUFP++] = ch;
}

// empties the buffer and returns an atom formed from the characters
// which had been placed in it(by calling "mkatom")
atom packbuffer() {
  atom RESULT = mkatomn(BUFFER, BUFP);
  BUFP = 0;
  return RESULT;
}

// does string A sort before string B?

// tests atoms for alphabetical order
// A, B are atoms
bool alfa_ls(atom A, atom B) { return strcmp(PRINTNAME(A), PRINTNAME(B)) < 0; }

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

void listpm() {

  word EMPTY = 0;
  word I;

  bcpl_writes("\n LIST POST MORTEM\n");
  gcstats();
  fprintf(bcpl_OUTPUT, ", current cells = %d\n", (int)((CONSP - CONSBASE) / 2));

  if (BUFP > 0) {
    bcpl_writes("Buffer: ");
    for (I = 0; I < BUFP; I++) {
      (*_WRCH)(BUFFER[I]);
    }
    (*_WRCH)('\n');
  }

  bcpl_writes("Atom buckets:\n");

  for (I = 0; I < 128; I++) {

    if (HASHV[I] != 0) {

      atom P = HASHV[I];
      fprintf(bcpl_OUTPUT, "%d :\t", (int)I);

      while (!(P == 0)) {
        bcpl_writes(PRINTNAME(P));

        if (!(VAL(P) == NIL)) {
          bcpl_writes(" = ");
          printobj(VAL(P));
        }

        P = LINK(P);

        if (P != 0) {
          bcpl_writes("\n\t");
        }
      }

      (*_WRCH)('\n');

    } else {

      EMPTY = EMPTY + 1;
    }
  }

  fprintf(bcpl_OUTPUT, "Empty buckets = %d\n", (int)EMPTY);
}

// gives the length of list X
word length(list X) {

  word N = 0;

  while (!(X == NIL)) {
    X = TL(X), N = N + 1;
  }

  return N;
}

// says if "A" is = an element of X
word member(list X, list A) {

  while (!(X == NIL || HD(X) == A)) {
    X = TL(X);
  }

  return X != NIL;
}

// appends (a copy of) list X to the front of list Y
list append(list X, list Y) { return shunt(shunt(X, NIL), Y); }

// reverses the list X
list reverse(list X) { return shunt(X, NIL); }

// appends reverse(X) to the list Y
list shunt(list X, list Y) {

  while (!(X == NIL)) {
    Y = cons(HD(X), Y);
    X = TL(X);
  }
  return Y;
}

// destructively removes a from x (if present)
list sub1(list X, atom A) {
  if (X == NIL) {
    return NIL;
  }

  if (HD(X) == (list)A) {
    return TL(X);
  }

  {
    list *P = &(TL(X));
    while (!((*P == NIL) || HD(*P) == (list)A)) {
      P = &(TL(*P));
    }

    if (!(*P == NIL)) {
      *P = TL(*P);
    }

    return X;
  }
}

// determines if list objects X and Y are isomorphic
word equal(list X, list Y) {

  do {
    if (X == Y) {
      return true;
    }

    if (isnum(X) && isnum(Y)) {
      return getnum(X) == getnum(Y);
    }

    if (!(iscons(X) && iscons(Y) && equal(HD(X), HD(Y)))) {
      return false;
    }

    X = TL(X), Y = TL(Y);
  } while (1);
}

// returns the n'th element of list X
list elem(list X, word N) {
  while (!(N == 1)) {
    X = TL(X), N = N - 1;
  }
  return HD(X);
}

// prints an arbitrary list object X
// renamed from printob
void printobj(list X) {
  // list X or atom

  if (X == NIL) {

    bcpl_writes("NIL");

  } else if (isatom(X)) {

    fprintf(bcpl_OUTPUT, "\"%s\"", PRINTNAME((atom)X));

  } else if (isnum(X)) {

    bcpl_writen(getnum(X));

  } else if (iscons(X)) {

    (*_WRCH)('(');
    while (iscons(X)) {
      printobj(HD(X));
      (*_WRCH)('.');
      X = TL(X);
    }
    printobj(X);
    (*_WRCH)(')');

  } else {

    fprintf(bcpl_OUTPUT, "<%p>", X);
  }
}

#ifdef INSTRUMENT_KRC_GC
// debugging function: ensure that P is a valid pointer into cons space
// and bomb if not.
// "is OK cons"
list isokcons(list P) {
  list Q;
  if (COLLECTING)
    return P;

  if (CONSBASE <= P && P < CONSLIMIT) {
    // (only even addresses in listspace count)
    if (((char *)P - (char *)CONSBASE) % sizeof(struct list) == 0) {
      return P;
    } else {
      fprintf(bcpl_OUTPUT, "\nHD() or TL() called on ODD address %p\n", P);
    }
  } else {
    fprintf(bcpl_OUTPUT, "\nHD() or TL() called on %p not in CONS space\n", P);
  }
  // cause segfault in caller
  return (list)0;
}
#endif
