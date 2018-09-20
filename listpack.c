// list processing package (for 2960/EMAS)    DAT 23/11/79
// warning - much of this code is machine dependent
#include "listhdr.h"

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

// MAX NO OF CHARS IN AN ATOM
#define ATOMSIZE 255

// Non-pointer value for the HD of an entry in CONS space,
// indicating that it is an integer, stored in the TL field.
#define FULLWORD (NIL - 1)

// Impossible value of pointer or integer used as flag during GC.
// just top bit set
#define GONETO ((LIST)(1ULL << (sizeof(LIST) * 8 - 1)))

static LIST CONSBASE, CONSLIMIT, CONSP, OTHERBASE;
static LIST *STACKBASE;

// static struct ATOM *ATOMBASE;
static ATOM ATOMBASE;
static ATOM ATOMP, ATOMLIMIT;
static WORD NOGCS = 0, RECLAIMS = 0;
bool ATGC;
char *USERLIB;

#ifdef INSTRUMENT_KRC_GC
// ARE WE CURRENTLY IN THE GARBAGE COLLECTOR?
bool COLLECTING = false;
#endif

static ATOM HASHV[128];

static char BUFFER[ATOMSIZE + 1];
static WORD BUFP = 0;

int ARGC;

//  Program parameters
char **ARGV;

// Forward declarations
static WORD HASH(char *S, int LEN);
static void GC(void);
static void COPY(LIST *P);
static void COPYHEADS(void);

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
          bcpl_WRITES("krc: -h What?\n");
          exit(0);
        }
        break;
      case 'l':
        // doesn't logically belong in listpack
        if (++I >= ARGC) {
          bcpl_WRITES("krc: -l What?\n");
          exit(0);
        } else
          USERLIB = ARGV[I];
        break;
      case 'd':
        if (++I >= ARGC || (DICMAX = atoi(ARGV[I])) <= 0) {
          bcpl_WRITES("krc: -d What?\n");
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
  CONSBASE = (LIST)malloc(SPACE * sizeof(*CONSBASE));
  if (CONSBASE == (void *)-1) {
    SPACE_ERROR("Not enough memory");
  }
  CONSP = CONSBASE, CONSLIMIT = CONSBASE + SPACE;
  OTHERBASE = (LIST)malloc(SPACE * sizeof(*CONSBASE));
  if (OTHERBASE == (void *)-1) {
    SPACE_ERROR("Not enough memory");
  }
  ATOMBASE = (ATOM)malloc(ATOMSPACE * sizeof(*ATOMBASE));
  if (ATOMBASE == (void *)-1) {
    SPACE_ERROR("Not enough memory");
  }
  ATOMP = ATOMBASE;
  ATOMLIMIT = ATOMBASE + ATOMSPACE;

  main2();
}

// A separate function finds STACKBASE, to avoid inclusion of any
// locals, temporaries and stacked stuff belonging to main().
void main2() {
  // marker to find stack base
  LIST N;
  STACKBASE = &N;

  // "GO" is the user's start routine
  GO();
}

WORD HAVEPARAM(WORD CH) {
  WORD I;
  CH = toupper(CH);
  for (I = 1; I < ARGC; I++)
    if (ARGV[I][0] == '-' && toupper(ARGV[I][1]) == toupper(CH)) {
      return true;
    }
  return false;
}

LIST CONS(LIST X, LIST Y) {
  if (CONSP >= (CONSLIMIT - 1)) {
    GC();
  }
  HD(CONSP) = X, TL(CONSP) = Y, CONSP = CONSP + 1;
  return CONSP - 1;
}

#include <setjmp.h>

void GC2(jmp_buf *);
void GC3(jmp_buf *, LIST *STACKEND);

void GC() {
  // Put all registers onto the stack so that any pointers into
  // the CONS space will be updated during the GC and put back
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
  LIST P;
  GC3(envp, &P);
}

// garbage collector - does a graph copy into the other semi-space
// not static to avoid inlining
void GC3(jmp_buf *envp, LIST *STACKEND) {
  // examine every pointer on the stack
  // P is a pointer to pointer, so incrementing it
  // moved it up by the size of one pointer.
  LIST *P;

  // In MAIN.c
  extern void HOLD_INTERRUPTS(), RELEASE_INTERRUPTS();

#ifdef DEBUG_GC
  int LASTUSED = 0;
  fprintf(bcpl_OUTPUT, "\n<");

#define SHOW(name)                                                             \
  do {                                                                         \
    fprintf(bcpl_OUTPUT, name ":%d", (int)(CONSP - OTHERBASE) - LASTUSED);     \
    LASTUSED = CONSP - OTHERBASE;                                              \
    COPYHEADS();                                                               \
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
  HOLD_INTERRUPTS();
  NOGCS = NOGCS + 1;
  if (ATGC) {
    bcpl_WRITES("<gc called>\n");
  }
  CONSP = OTHERBASE;
  // user's static variables etc.
  BASES(COPY);
  SHOW("bases");
  {
    WORD I;
    for (I = 0; I < 128; I++) {
      // val fields of atoms
      ATOM A = HASHV[I];
      while (!(A == 0)) {
        COPY((LIST *)&(VAL(A)));
        A = LINK(A);
      }
    }
  }
  SHOW("atoms");

  // runtime detection of stack growth direction
  if (STACKBASE < STACKEND) {
    // stack grow upwards
    for (P = STACKBASE + 1; P < STACKEND; P++) {
      if (CONSBASE <= (LIST)*P && (LIST)*P < CONSLIMIT) {
        // an aligned address in listspace
        if (((char *)*P - (char *)CONSBASE) % sizeof(struct LIST) == 0) {
          COPY(P);
        }

        if (((char *)*P - (char *)CONSBASE) % sizeof(struct LIST) ==
            sizeof(struct LIST *)) {
          // Pointer to a tail cell, which also needs updating
          *P = (LIST)((LIST *)*P - 1);
          COPY(P);
          *P = (LIST)((LIST *)*P + 1);
        }
      }
    }
  } else {
    // stack grows downwards
    for (P = STACKBASE - 1; P > STACKEND; P--) {
      if (CONSBASE <= (LIST)*P && (LIST)*P < CONSLIMIT) {
        if (((char *)*P - (char *)CONSBASE) % sizeof(struct LIST) == 0) {
          // an aligned address in listspace
          COPY(P);
        }
        if (((char *)*P - (char *)CONSBASE) % sizeof(struct LIST) ==
            sizeof(struct LIST *)) {
          // Pointer to a tail cells, which also needs updating
          *P = (LIST)((LIST *)*P - 1);
          COPY(P);
          *P = (LIST)((LIST *)*P + 1);
        }
      }
      if (P == (LIST *)(envp + 1)) {
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
        P = (LIST *)((char *)(&((*envp)->__jmpbuf)) +
                     sizeof((*envp)->__jmpbuf));
#endif
      }
    }
  }
  SHOW("regs");
#ifdef DEBUG_GC
  fprintf(bcpl_OUTPUT, ">\n");
#endif

  COPYHEADS();
  // now swap semi-spaces
  {
    LIST HOLD = CONSBASE;
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
    bcpl_WRITES("<");
    bcpl_WRITEN((WORD)(CONSP - CONSBASE));
    bcpl_WRITES(" cells in use>\n");
  }
#endif
  RELEASE_INTERRUPTS();

  // abandon job if space utilisation exceeds 90%
  if (CONSP - CONSBASE > (9 * SPACE) / 10) {
    SPACE_ERROR("Space exhausted");
  }

#ifdef INSTRUMENT_KRC_GC
  COLLECTING = false;
#endif
  longjmp(*envp, 1);
}

// p is the address of a list field
static void COPY(LIST *P) {
  //   do $( bcpl_WRITES("COPYING ")
  //         PRINTOB(*P)
  //         (*_WRCH)('\n')  $) <>
  while (CONSBASE <= *P && *P < CONSLIMIT) {
    if (HD(*P) == GONETO) {
      *P = TL(*P);
      return;
    }
    LIST X = HD(*P);
    LIST Y = TL(*P);
    LIST Z = CONSP;
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

static void COPYHEADS() {
  LIST Z = OTHERBASE;
  while (!(Z == CONSP)) {
    COPY(&(HD(Z)));
    Z = Z + 1;
  }
}

WORD ISCONS(LIST X)
#ifdef INSTRUMENT_KRC_GC
{
  if (CONSBASE <= X && X < CONSLIMIT) {
    if (((char *)X - (char *)CONSLIMIT) % sizeof(struct LIST) != 0) {
      fprintf(bcpl_OUTPUT, "\nMisaligned pointer %p in ISCONS\n", X);
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

WORD ISATOM(LIST X) { return ATOMBASE <= (ATOM)X && (ATOM)X < ATOMP; }

WORD ISNUM(LIST X)
#ifdef INSTRUMENT_KRC_GC
{
  if (CONSBASE <= X && X < CONSLIMIT) {
    if (((char *)X - (char *)CONSLIMIT) % sizeof(struct LIST) != 0) {
      fprintf(bcpl_OUTPUT, "\nMisaligned pointer %p in ISNUM\n", X);
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

// GCC warning expected
LIST STONUM(WORD N) { return CONS(FULLWORD, (LIST)N); }

// GCC warning expected
WORD GETNUM(LIST X) { return (WORD)(TL(X)); }

// make an ATOM from a C string
ATOM MKATOM(char *S) { return MKATOMN(S, strlen(S)); }

// make an ATOM which might contain NULs
ATOM MKATOMN(char *S, int LEN) {
  ATOM *BUCKET = &(HASHV[HASH(S, LEN)]);
  ATOM *P = BUCKET;
  // N is size of string counted as the number of pointers it occupies
  WORD N;

  // SEARCH THE APPROPRIATE BUCKET
  while (!(*P == 0)) {
    if (LEN == LEN(*P) && memcmp(S, PRINTNAME(*P), (size_t)LEN) == 0) {
      return (ATOM)*P;
    }
    P = &(LINK(*P));
  }

  // CREATE NEW ATOM
  // +1 for the BCPL size, +1 for the \0, then round up to element size
  N = (1 + LEN + 1 + (sizeof(WORD *)) - 1) / sizeof(WORD *);
  if ((WORD **)ATOMP + OFFSET + N > (WORD **)ATOMLIMIT) {
    bcpl_WRITES("<string space exhausted>\n");
    exit(0);
  }
  *P = ATOMP, LINK(ATOMP) = 0, VAL(ATOMP) = NIL;
  NAME(ATOMP)
  [0] = LEN,
 memcpy(NAME(ATOMP) + 1, S, (size_t)LEN), NAME(ATOMP)[LEN + 1] = '\0';
  ATOMP = (ATOM)((WORD **)ATOMP + OFFSET + N);
  return *P;
}

// takes a name and returns a value in 0..127
static WORD HASH(char *S, int LEN) {
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

void BUFCH(WORD CH) {
  if (BUFP >= ATOMSIZE) {
    SPACE_ERROR("Atom too big");
  }
  BUFFER[BUFP++] = CH;
}

ATOM PACKBUFFER() {
  ATOM RESULT = MKATOMN(BUFFER, BUFP);
  BUFP = 0;
  return RESULT;
}

// does string A sort before string B?
bool ALFA_LS(ATOM A, ATOM B) // A,B ARE ATOMS
{
  return strcmp(PRINTNAME(A), PRINTNAME(B)) < 0;
}

static void GCSTATS() {
  fprintf(bcpl_OUTPUT, "Cells claimed = %d, no of gc's = %d",
          (int)(RECLAIMS + (CONSP - CONSBASE) / 2), (int)NOGCS);
}

void RESETGCSTATS() { NOGCS = 0, RECLAIMS = -(CONSP - CONSBASE); }

void FORCE_GC() {
  // to compensate for calling too early
  RECLAIMS = RECLAIMS - (CONSLIMIT - CONSP);
  if (ATGC) {
    fprintf(bcpl_OUTPUT, "Max cells available = %d\n", SPACE);
  }
  GC();
}

void REPORTDIC() {
  fprintf(bcpl_OUTPUT, "string space = %ld bytes",
          (long)(ATOMSPACE * atomsize));
  fprintf(bcpl_OUTPUT, ", used %ld\n", (long)((ATOMP - ATOMBASE) * atomsize));
}

void LISTPM() {
  WORD EMPTY = 0;
  WORD I;
  bcpl_WRITES("\n LIST POST MORTEM\n");
  GCSTATS();
  fprintf(bcpl_OUTPUT, ", current cells = %d\n", (int)((CONSP - CONSBASE) / 2));
  if (BUFP > 0) {
    bcpl_WRITES("Buffer: ");
    for (I = 0; I < BUFP; I++) {
      (*_WRCH)(BUFFER[I]);
    }
    (*_WRCH)('\n');
  }
  bcpl_WRITES("Atom buckets:\n");
  for (I = 0; I < 128; I++)
    if (HASHV[I] != 0) {
      ATOM P = HASHV[I];
      fprintf(bcpl_OUTPUT, "%d :\t", (int)I);
      while (!(P == 0)) {
        bcpl_WRITES(PRINTNAME(P));
        if (!(VAL(P) == NIL)) {
          bcpl_WRITES(" = ");
          PRINTOB(VAL(P));
        }
        P = LINK(P);
        if (P != 0)
          bcpl_WRITES("\n\t");
      }
      (*_WRCH)('\n');
    } else
      EMPTY = EMPTY + 1;
  fprintf(bcpl_OUTPUT, "Empty buckets = %d\n", (int)EMPTY);
}

WORD LENGTH(LIST X) {
  WORD N = 0;
  while (!(X == NIL))
    X = TL(X), N = N + 1;
  return N;
}

WORD MEMBER(LIST X, LIST A) {
  while (!(X == NIL || HD(X) == A))
    X = TL(X);
  return X != NIL;
}

LIST APPEND(LIST X, LIST Y) { return SHUNT(SHUNT(X, NIL), Y); }

LIST REVERSE(LIST X) { return SHUNT(X, NIL); }

LIST SHUNT(LIST X, LIST Y) {
  while (!(X == NIL)) {
    Y = CONS(HD(X), Y);
    X = TL(X);
  }
  return Y;
}

// destructively removes a from x (if present)
LIST SUB1(LIST X, ATOM A) {
  if (X == NIL) {
    return NIL;
  }

  if (HD(X) == (LIST)A) {
    return TL(X);
  }

  {
    LIST *P = &(TL(X));
    while (!((*P == NIL) || HD(*P) == (LIST)A)) {
      P = &(TL(*P));
    }

    if (!(*P == NIL)) {
      *P = TL(*P);
    }

    return X;
  }
}

WORD EQUAL(LIST X, LIST Y) {
  do {
    if (X == Y) {
      return true;
    }

    if (ISNUM(X) && ISNUM(Y)) {
      return GETNUM(X) == GETNUM(Y);
    }

    if (!(ISCONS(X) && ISCONS(Y) && EQUAL(HD(X), HD(Y)))) {
      return false;
    }

    X = TL(X), Y = TL(Y);
  } while (1);
}

LIST ELEM(LIST X, WORD N) {
  while (!(N == 1)) {
    X = TL(X), N = N - 1;
  }
  return HD(X);
}

// or ATOM
void PRINTOB(LIST X) {
  if (X == NIL) {
    bcpl_WRITES("NIL");
  } else if (ISATOM(X)) {
    fprintf(bcpl_OUTPUT, "\"%s\"", PRINTNAME((ATOM)X));
  } else if (ISNUM(X)) {
    bcpl_WRITEN(GETNUM(X));
  } else if (ISCONS(X)) {
    (*_WRCH)('(');
    while (ISCONS(X)) {
      PRINTOB(HD(X));
      (*_WRCH)('.');
      X = TL(X);
    }
    PRINTOB(X);
    (*_WRCH)(')');
  } else
    fprintf(bcpl_OUTPUT, "<%p>", X);
}

#ifdef INSTRUMENT_KRC_GC
// debugging function: ensure that P is a valid pointer into cons space
// and bomb if not.
LIST ISOKCONS(LIST P) {
  LIST Q;
  if (COLLECTING)
    return P;

  if (CONSBASE <= P && P < CONSLIMIT) {
    // (only even addresses in listspace count)
    if (((char *)P - (char *)CONSBASE) % sizeof(struct LIST) == 0) {
      return P;
    } else {
      fprintf(bcpl_OUTPUT, "\nHD() or TL() called on ODD address %p\n", P);
    }
  } else {
    fprintf(bcpl_OUTPUT, "\nHD() or TL() called on %p not in CONS space\n", P);
  }
  // cause segfault in caller
  return (LIST)0;
}
#endif
