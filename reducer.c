// KRC reducer

#include "listlib.h"
#include "compiler.h"
#include "reducer.h"

//----------------------------------------------------------------------
// The KRC system is Copyright (c) D. A. Turner 1981
// All  rights reserved.  It is distributed as free software under the
// terms in the file "COPYING", which is included in the distribution.
//----------------------------------------------------------------------

#include <string.h> // for strlen()
#include <stdlib.h> // for malloc()
#include <ctype.h>  // for isprint()
#include <signal.h> // for raise(), SIGINT

// global variables owned by reducer
LIST MEMORIES = NIL;
word REDS;

// base for list indexing
word LISTBASE = 0;

word ABORTED = false;

static ATOM ETC, SILLYNESS, GUARD, LISTDIFF, BADFILE, READFN, WRITEFN,
    INTERLEAVEFN;

// argument stack. ARGP points to the last cell allocated
static LIST *ARGSPACE = NULL;
static LIST *ARG;
static LIST *ARGMAX;
static LIST *ARGP;

void init_argspace(void) {
  if (ARGSPACE == NULL) {

    // number of LIST cells, in listlib.c
    extern int SPACE;

    // empirically, using edigits,
    // with SPACE/6, the argstack exhausts first.
    // with /5, it runs out of heap first.
    int NARGS = SPACE / 5;

    ARGSPACE = (LIST *)malloc(NARGS * sizeof(*ARGSPACE));

    if (ARGSPACE == (void *)-1) {
      SPACE_ERROR("Cannot allocate argument stack");
    }

    ARGMAX = ARGSPACE + NARGS - 1;
  }
  ARG = ARGSPACE, ARGP = ARG - 1;
}

// sentinel value (impossible pointer)
#define ENDOFSTACK (-4)

LIST S;

// primitive functions
static void functionp(LIST E);
static void listp(LIST E);
static void stringp(LIST E);
static void numberp(LIST E);
static void charp(LIST E);
static void size(LIST E);
static void code(LIST E);
static void decode(LIST E);
static void concat(LIST E);
static void explode(LIST E);
static void abort_(LIST E);
static void startread(LIST E);
static void read(LIST E);
static void writeap(LIST E);
static void seq(LIST E);

// local function delarations
static void printfunction(LIST E);
static bool equalval(LIST A, LIST B);
static void badexp(LIST E);
static void overflow(LIST E);
static void obey(LIST EQNS, LIST E);
static bool isfun(LIST X);

static LIST reduce(LIST E);
static LIST substitute(LIST ACTUAL, LIST FORMAL, LIST EXP);
static bool binds(LIST FORMAL, LIST X);

// DT 2015
static void showch(unsigned char c);

static void R(char *S, void (*F)(LIST), word N) {
  ATOM A = MKATOM(S);
  LIST EQN = CONS((LIST)A, CONS((LIST)CALL_C, (LIST)F));

  if (!(F == read)) {
    ENTERSCRIPT(A);
  }

  VAL(A) = CONS(CONS((LIST)N, NIL), CONS(EQN, NIL));
}

void setup_primfns_etc(void) {

  // S is used inside reduce
  S = (LIST)ENDOFSTACK;

  // miscellaneous initialisations
  ETC = MKATOM("... ");

  SILLYNESS = MKATOM("<unfounded recursion>");
  GUARD = MKATOM("<non truth-value used as guard:>");
  TRUTH = CONS((LIST)QUOTE, (LIST)MKATOM("TRUE"));
  FALSITY = CONS((LIST)QUOTE, (LIST)MKATOM("FALSE"));
  LISTDIFF = MKATOM("listdiff");
  INFINITY = CONS((LIST)QUOTE, (LIST)-3);

  // primitive functions
  R("function__", functionp, 1);
  R("list__", listp, 1);
  R("string__", stringp, 1);
  R("number__", numberp, 1);
  R("char__", charp, 1);
  R("printwidth__", size, 1);
  R("ord__", code, 1);
  R("chr__", decode, 1);
  R("implode__", concat, 1);
  R("explode__", explode, 1);
  R("abort__", abort_, 1);
  R("read__", startread, 1);
  R("read ", read, 1);
  R("seq__", seq, 2);
  R("write__", writeap, 3);
  BADFILE = MKATOM("<cannot open file:>");
  READFN = MKATOM("read ");
  WRITEFN = MKATOM("write");
  INTERLEAVEFN = MKATOM("interleave");
}

// little routine to avoid s having to be global, just because
// it may need fixing up after an interrupt. this routine does that.
void fixup_s(void) {

  // in case interrupt struck while reduce was dissecting a constant
  if (!(S == (LIST)ENDOFSTACK)) {
    HD(S) = (LIST)QUOTE;
  }
}

// return an upper-case copy of a string.
// copy to static area of 80 chars, the same as BCPL
// also to avoid calling strdup which calls malloc() and
// contaminates the garbage collection done with Boehm GC.
char *scaseconv(char *S) {
  static char T[80 + 1];
  char *p = S, *q = T;

  while (*p) {
    *q++ = caseconv(*p++);
  }

  *q = '\0';
  return T;
}

void initstats() { REDS = 0; }

void outstats() { fprintf(bcpl_OUTPUT, "reductions = %" W "\n", REDS); }

// the possible values of a reduced expression are:
//  VAL:= CONST | FUNCTION | LIST
//  CONST:= NUM | CONS(QUOTE,ATOM)
//  LIST:= NIL | CONS(COLON_OP,CONS(EXP,EXP))
//  FUNCTION:= NAME | CONS(E1,E2)

void printval(LIST E, bool FORMAT) {

  E = reduce(E);

  if (E == NIL) {

    if (FORMAT) {
      bcpl_WRITES("[]");
    }

  } else if (ISNUM(E)) {

    bcpl_WRITEN(GETNUM(E));

  } else if (ISCONS(E)) {

    LIST H = HD(E);

    if (H == (LIST)QUOTE) {

      printatom((ATOM)TL(E), FORMAT);

    } else if (H == (LIST)COLON_OP) {

      if (FORMAT) {
        (*_WRCH)('[');
      }

      E = TL(E);
      do {
        printval(HD(E), FORMAT);
        E = TL(E);
        E = reduce(E);

        if (!(ISCONS(E))) {
          break;
        }

        if (HD(E) == (LIST)COLON_OP) {

          if (FORMAT) {
            (*_WRCH)(',');
          }

        } else {
          break;
        }

        E = TL(E);

      } while (1);

      if (E == NIL) {

        if (FORMAT) {
          (*_WRCH)(']');
        }

      } else {

        badexp(CONS((LIST)COLON_OP, CONS((LIST)ETC, E)));
      }

    } else if (ISCONS(H) && HD(H) == (LIST)WRITEFN) {

      TL(H) = reduce(TL(H));

      if (!(ISCONS(TL(H)) && HD(TL(H)) == (LIST)QUOTE)) {
        badexp(E);
      }

      {
        char *F = PRINTNAME((ATOM)TL(TL(H)));
        FILE *OUT = FINDCHANNEL(F);
        FILE *HOLD = bcpl_OUTPUT;

        if (!(OUT != NULL)) {
          badexp(CONS((LIST)BADFILE, TL(H)));
        }

        bcpl_OUTPUT_fp = (OUT);
        printval(TL(E), FORMAT);
        bcpl_OUTPUT_fp = (HOLD);
      }

    } else {

      // a partial application or composition
      printfunction(E);
    }
  } else {

    // only possibility here should be name of function
    printfunction(E);
  }
}

void printatom(ATOM A, bool FORMAT) {

  if (FORMAT) {

    // DT 2015
    int I;
    (*_WRCH)('"');

    for (I = 1; I <= LEN(A); I++) {
      showch(NAME(A)[I]);
    }

    (*_WRCH)('"');

  } else {

    // output the BCPL string preserving nuls
    int I;
    for (I = 1; I <= LEN(A); I++) {
      (*_WRCH)(NAME(A)[I]);
    }
  }
}

static void showch(unsigned char c) {
  switch (c) {
  case '\a':
    (*_WRCH)('\\');
    (*_WRCH)('a');
    break;
  case '\b':
    (*_WRCH)('\\');
    (*_WRCH)('b');
    break;
  case '\f':
    (*_WRCH)('\\');
    (*_WRCH)('f');
    break;
  case '\n':
    (*_WRCH)('\\');
    (*_WRCH)('n');
    break;
  case '\r':
    (*_WRCH)('\\');
    (*_WRCH)('r');
    break;
  case '\t':
    (*_WRCH)('\\');
    (*_WRCH)('t');
    break;
  case '\v':
    (*_WRCH)('\\');
    (*_WRCH)('v');
    break;
  case '\\':
    (*_WRCH)('\\');
    (*_WRCH)('\\');
    break;
  case '\'':
    (*_WRCH)('\\');
    (*_WRCH)('\'');
    break;
  case '\"':
    (*_WRCH)('\\');
    (*_WRCH)('\"');
    break;
  default:
    if (iscntrl(c) || c >= 127) {
      printf("\\%03u", c);
    } else {
      (*_WRCH)(c);
    }
  }
}

static void printfunction(LIST E) {

  (*_WRCH)('<');
  printexp(E, 0);
  (*_WRCH)('>');
}

// unpredictable results if A,B both functions
static bool equalval(LIST A, LIST B) {
  do {
    A = reduce(A);
    B = reduce(B);

    if (A == B) {
      return true;
    }

    if (ISNUM(A) && ISNUM(B)) {
      return GETNUM(A) == GETNUM(B);
    }

    if (!(ISCONS(A) && ISCONS(B) && (HD(A) == HD(B)))) {
      return false;
    }

    if (HD(A) == (LIST)QUOTE || HD(A) == (LIST)QUOTE_OP) {
      return TL(A) == TL(B);
    }

    if (!(HD(A) == (LIST)COLON_OP)) {
      // UH ?
      return false;
    }

    A = TL(A), B = TL(B);
    if (!(equalval(HD(A), HD(B)))) {
      return false;
    }

    A = TL(A), B = TL(B);
  } while (1);
}

// called for all evaluation errors
static void badexp(LIST E) {

  _WRCH = TRUEWRCH;
  CLOSECHANNELS();
  bcpl_WRITES("\n**undefined expression**\n  ");
  printexp(E, 0);

  // could insert more detailed diagnostics here,
  // depending on nature of HD!E, for example:
  if (ISCONS(E) && (HD(E) == (LIST)COLON_OP || HD(E) == (LIST)APPEND_OP)) {
    bcpl_WRITES("\n  (non-list encountered where list expected)");
  }

  bcpl_WRITES("\n**evaluation abandoned**\n");
  ESCAPETONEXTCOMMAND();
}

// integer overflow handler
static void overflow(LIST E) {

  _WRCH = TRUEWRCH;
  CLOSECHANNELS();
  bcpl_WRITES("\n**integer overflow**\n  ");
  printexp(E, 0);
  bcpl_WRITES("\n**evaluation abandoned**\n");
  ESCAPETONEXTCOMMAND();
}

// a kludge
LIST buildexp(LIST CODE) {

  // a bogus piece of graph
  LIST E = CONS(NIL, NIL);
  obey(CONS(CONS(NIL, CODE), NIL), E);

  // reset ARG stack
  ARGP = ARG - 1;
  return E;
}

// transform a piece of graph, E, in accordance
// with EQNS - actual params are found in
// *ARG ... *ARGP
// (warning - has side effect of raising ARGP)
static void obey(LIST EQNS, LIST E) {

  // EQNS loop
  while (!(EQNS == NIL)) {

    LIST CODE = TL(HD(EQNS));
    LIST *HOLDARG = ARGP;
    word I;

    // decode loop
    do {
      LIST H = HD(CODE);
      CODE = TL(CODE);

      // first, check the only cases that increment ARGP
      switch ((word)H) {
      case LOAD_C:
      case LOADARG_C:
      case FORMLIST_C:
        ARGP = ARGP + 1;
        if (ARGP > ARGMAX) {
          SPACE_ERROR("Arg stack overflow");
        }
      }

      switch ((word)H) {
      case LOAD_C:
        // ARGP=ARGP+1;
        *ARGP = HD(CODE);
        CODE = TL(CODE);
        break;
      case LOADARG_C:
        // ARGP=ARGP+1;
        if (ARGP > ARGMAX) {
          SPACE_ERROR("Arg stack overflow");
        }
        *ARGP = ARG[(word)(HD(CODE))];
        CODE = TL(CODE);
        break;
      case APPLYINFIX_C:
        *ARGP = CONS(*(ARGP - 1), *ARGP);
        *(ARGP - 1) = HD(CODE);
        CODE = TL(CODE);
      case APPLY_C:
        ARGP = ARGP - 1;
        if (HD(CODE) == (LIST)STOP_C) {
          HD(E) = *ARGP, TL(E) = *(ARGP + 1);
          return;
        }
        *ARGP = CONS(*ARGP, *(ARGP + 1));
        break;
      case CONTINUE_INFIX_C:
        *(ARGP - 1) = CONS(HD(CODE), CONS(*(ARGP - 1), *ARGP));
        CODE = TL(CODE);
        break;
      case IF_C:
        *ARGP = reduce(*ARGP);
        if (*ARGP == FALSITY) {
          goto BREAK_DECODE_LOOP;
        }
        if (!(*ARGP == TRUTH)) {
          badexp(CONS((LIST)GUARD, *ARGP));
        }
        break;
      case FORMLIST_C:
        // ARGP=ARGP+1;
        *ARGP = NIL;
        for (I = 1; I <= (word)HD(CODE); I++) {
          ARGP = ARGP - 1;
          *ARGP = CONS((LIST)COLON_OP, CONS(*ARGP, *(ARGP + 1)));
        }
        CODE = TL(CODE);
        break;
      case FORMZF_C: {
        LIST X = CONS(*(ARGP - (word)HD(CODE)), NIL);
        LIST *P;
        for (P = ARGP; P >= ARGP - (word)HD(CODE) + 1; P = P - 1) {
          X = CONS(*P, X);
        }

        ARGP = ARGP - (word)HD(CODE);
        *ARGP = CONS((LIST)ZF_OP, X);
        CODE = TL(CODE);
        break;
      }
      case CONT_GENERATOR_C:
        for (I = 1; I <= (word)HD(CODE); I++) {
          *(ARGP - I) = CONS((LIST)GENERATOR, CONS(*(ARGP - I), TL(TL(*ARGP))));
        }

        CODE = TL(CODE);
        break;
      case MATCH_C: {
        word I = (word)HD(CODE);
        CODE = TL(CODE);

        if (!(equalval(ARG[I], HD(CODE)))) {
          goto BREAK_DECODE_LOOP;
        }

        CODE = TL(CODE);
        break;
      }
      case MATCHARG_C: {
        word I = (word)HD(CODE);
        CODE = TL(CODE);

        if (!(equalval(ARG[I], ARG[(word)(HD(CODE))]))) {
          goto BREAK_DECODE_LOOP;
        }

        CODE = TL(CODE);
        break;
      }
      case MATCHPAIR_C: {
        LIST *P = ARG + (word)(HD(CODE));
        *P = reduce(*P);

        if (!(ISCONS(*P) && HD(*P) == (LIST)COLON_OP)) {
          goto BREAK_DECODE_LOOP;
        }

        ARGP = ARGP + 2;
        *(ARGP - 1) = HD(TL(*P)), *ARGP = TL(TL(*P));
        CODE = TL(CODE);
        break;
      }
      case LINENO_C:
        // no action
        CODE = TL(CODE);
        break;
      case STOP_C:
        HD(E) = (LIST)INDIR, TL(E) = *ARGP;
        return;
      case CALL_C:
        (*(void (*)())CODE)(E);
        return;
      default:
        fprintf(bcpl_OUTPUT, "IMPOSSIBLE INSTRUCTION <%p> IN \"obey\"\n", H);
      }
    } while (1);
    // end of decode loop

  BREAK_DECODE_LOOP:
    EQNS = TL(EQNS);
    ARGP = HOLDARG;
  }

  // end of EQNS loop
  badexp(E);
}

static void stringp(LIST E) {
  *ARG = reduce(*ARG);
  HD(E) = (LIST)INDIR,
  TL(E) = ISCONS(*ARG) && HD(*ARG) == (LIST)QUOTE ? TRUTH : FALSITY;
}

static void numberp(LIST E) {
  *ARG = reduce(*ARG);
  HD(E) = (LIST)INDIR, TL(E) = ISNUM(*ARG) ? TRUTH : FALSITY;
}

static void listp(LIST E) {
  *ARG = reduce(*ARG);
  HD(E) = (LIST)INDIR;
  TL(E) = (*ARG == NIL || (ISCONS(*ARG) && HD(*ARG) == (LIST)COLON_OP))
              ? TRUTH
              : FALSITY;
}

static void functionp(LIST E) {
  *ARG = reduce(*ARG);
  HD(E) = (LIST)INDIR;
  TL(E) = isfun(*ARG) ? TRUTH : FALSITY;
}

static bool isfun(LIST X) {
  return ISATOM(X) || (ISCONS(X) && QUOTE != HD(X) && HD(X) != (LIST)COLON_OP);
}

// renamed from char to avoid conflict with char type
static void charp(LIST E) {
  *ARG = reduce(*ARG);
  HD(E) = (LIST)INDIR;
  TL(E) = ISCONS(*ARG) && HD(*ARG) == (LIST)QUOTE && LEN((ATOM)TL(*ARG)) == 1
              ? TRUTH
              : FALSITY;
}

static word COUNT;
static void COUNTCH(word CH) { COUNT = COUNT + 1; }

static void size(LIST E) {
  COUNT = 0;
  _WRCH = COUNTCH;
  printval(*ARG, false);
  _WRCH = TRUEWRCH;
  HD(E) = (LIST)INDIR, TL(E) = STONUM(COUNT);
}

static void code(LIST E) {
  *ARG = reduce(*ARG);

  if (!(ISCONS(*ARG) && HD(*ARG) == QUOTE)) {
    badexp(E);
  }

  {
    ATOM A = (ATOM)TL(*ARG);

    if (!(LEN(A) == 1)) {
      badexp(E);
    }

    HD(E) = (LIST)INDIR, TL(E) = STONUM((word)NAME(A)[1] & 0xff);
  }
}

static void decode(LIST E) {
  *ARG = reduce(*ARG);

  if (!(ISNUM(*ARG) && 0 <= (word)TL(*ARG) && (word)TL(*ARG) <= 255)) {
    badexp(E);
  }

  BUFCH((word)TL(*ARG));
  HD(E) = (LIST)INDIR, TL(E) = CONS((LIST)QUOTE, (LIST)PACKBUFFER());
}

static void concat(LIST E) {

  *ARG = reduce(*ARG);

  {
    LIST A = *ARG;

    while (ISCONS(A) && HD(A) == (LIST)COLON_OP) {
      LIST C = reduce(HD(TL(A)));

      if (!(ISCONS(C) && HD(C) == (LIST)QUOTE)) {
        badexp(E);
      }

      HD(TL(A)) = C;
      TL(TL(A)) = reduce(TL(TL(A)));
      A = TL(TL(A));
    }

    if (!(A == NIL)) {
      badexp(E);
    }

    A = *ARG;
    while (!(A == NIL)) {
      ATOM N = (ATOM)TL(HD(TL(A)));
      int I;

      for (I = 1; I <= LEN(N); I++) {
        BUFCH(NAME(N)[I]);
      }

      A = TL(TL(A));
    }
    A = (LIST)PACKBUFFER();
    HD(E) = (LIST)INDIR,
    TL(E) = A == TL(TRUTH) ? TRUTH
                           : A == TL(FALSITY) ? FALSITY : CONS((LIST)QUOTE, A);
  }
}

static void explode(LIST E) {

  *ARG = reduce(*ARG);

  if (!(ISCONS(*ARG) && HD(*ARG) == (LIST)QUOTE)) {
    badexp(E);
  }

  {
    ATOM A = (ATOM)TL(*ARG);
    LIST X = NIL;

    int I;
    for (I = NAME(A)[0]; I > 0; I--) {
      BUFCH(NAME(A)[I]);
      X = CONS((LIST)COLON_OP, CONS(CONS((LIST)QUOTE, (LIST)PACKBUFFER()), X));
    }

    HD(E) = (LIST)INDIR, TL(E) = X;
  }
}

// renamed from abort to avoid duplication of abort(3)
static void abort_(LIST E) {

  FILE *HOLD = bcpl_OUTPUT;
  bcpl_OUTPUT_fp = (stderr);

  bcpl_WRITES("\nprogram error: ");

  printval(TL(E), false);

  (*_WRCH)('\n');

  bcpl_OUTPUT_fp = (HOLD);
  ABORTED = true;
  raise(SIGINT);
}

static void startread(LIST E) {

  *ARG = reduce(*ARG);

  if (!(ISCONS(*ARG) && HD(*ARG) == (LIST)QUOTE)) {
    badexp(E);
  }

  {
    FILE *IN = bcpl_FINDINPUT(PRINTNAME((ATOM)TL(*ARG)));

    if (!(IN != NULL)) {
      badexp(CONS((LIST)BADFILE, *ARG));
    }

    HD(E) = (LIST)READFN, TL(E) = (LIST)IN;
  }
}

// caution: a name duplication of read(2)
static void read(LIST E) {

  FILE *IN = (FILE *)TL(E);
  bcpl_INPUT_fp = (IN);
  HD(E) = (LIST)INDIR, TL(E) = CONS((LIST)READFN, TL(E));

  {
    LIST *X = &(TL(E));
    word C = (*_RDCH)();

    // read one character
    if (C != EOF) {
      char c = C;
      *X = CONS((LIST)COLON_OP,
                CONS(CONS((LIST)QUOTE, (LIST)MKATOMN(&c, 1)), *X));
      X = &(TL(TL(*X)));
    }

    if (ferror(IN)) {
      fprintf(bcpl_OUTPUT, "\n**File read error**\n");
      ESCAPETONEXTCOMMAND();
    }

    if (C == EOF) {
      if (bcpl_INPUT_fp != stdin) {
        fclose(bcpl_INPUT_fp);
      };
      *X = NIL;
    }
    bcpl_INPUT_fp = (stdin);
  }
}

// called if write is applied to >2 ARGS
static void writeap(LIST E) { badexp(E); }

// seq a b evaluates a then returns b, added DT 2015
static void seq(LIST E) {

  reduce(TL(HD(E)));
  HD(E) = (LIST)INDIR;
}

// possibilities for leftmost field of a graph are:
// HEAD:= NAME | NUM | NIL | OPERATOR

static LIST reduce(LIST E) {
  static word M = 0;
  static word N = 0;
  LIST HOLD_S = S;
  word NARGS = 0;
  LIST *HOLDARG = ARG;

  // if ( &E>STACKLIMIT ) SPACE_ERROR("Arg stack overflow");
  // if ( ARGP>ARGMAX ) SPACE_ERROR("Arg stack overflow");

  S = (LIST)ENDOFSTACK;
  ARG = ARGP + 1;

  // main loop
  do {

    // find head, reversing pointers en route
    while (ISCONS(E)) {
      LIST HOLD = HD(E);
      NARGS = NARGS + 1;
      HD(E) = S, S = E, E = HOLD;
    }

    if (ISNUM(E) || E == NIL) {
      // unless NARGS==0 do HOLDARG=(LIST *)-1;  //flags an error
      goto BREAK_MAIN_LOOP;
    }

    // user defined name
    if (ISATOM(E)) {

      if (VAL((ATOM)E) == NIL || TL(VAL((ATOM)E)) == NIL) {

        badexp(E);

      } else {
        // undefined name

        // variable

        if (HD(HD(VAL((ATOM)E))) == 0) {
          LIST EQN = HD(TL(VAL((ATOM)E)));

          // memo not set
          if (HD(EQN) == 0) {
            HD(EQN) = buildexp(TL(EQN));
            MEMORIES = CONS(E, MEMORIES);
          }

          E = HD(EQN);
        } else {

          // can we get cyclic expressions?

          // function

          // hides the static N
          word N = (word)HD(HD(VAL((ATOM)E)));

          if (N > NARGS) {
            // not enough ARGS
            goto BREAK_MAIN_LOOP;
          }

          {
            LIST EQNS = TL(VAL((ATOM)E));

            word I;
            for (I = 0; I <= N - 1; I++) {

              // move back up GRAPH,
              LIST HOLD = HD(S);

              // stacking ARGS en route
              ARGP = ARGP + 1;

              if (ARGP > ARGMAX) {
                SPACE_ERROR("Arg stack overflow");
              }

              *ARGP = TL(S);
              HD(S) = E, E = S, S = HOLD;
            }

            NARGS = NARGS - N;

            // E now holds a piece of graph to be transformed
            // !ARG ... !ARGP  hold the parameters
            obey(EQNS, E);

            // reset ARG stack
            ARGP = ARG - 1;
          }
        }
      }

    } else {
      // operators
      switch ((word)E) {
      case QUOTE:
        if (!(NARGS == 1)) {
          HOLDARG = (LIST *)-1;
        }
        goto BREAK_MAIN_LOOP;
      case INDIR: {
        LIST HOLD = HD(S);
        NARGS = NARGS - 1;
        E = TL(S), HD(S) = (LIST)INDIR, S = HOLD;
        continue;
      }
      case QUOTE_OP:
        if (!(NARGS >= 3)) {
          goto BREAK_MAIN_LOOP;
        }

        {
          LIST OP = TL(S);
          LIST HOLD = HD(S);
          NARGS = NARGS - 2;
          HD(S) = E, E = S, S = HOLD;
          HOLD = HD(S);
          HD(S) = E, E = S, S = HOLD;
          TL(S) = CONS(TL(E), TL(S)), E = OP;
          continue;
        }
      case LISTDIFF_OP:
        E = CONS((LIST)LISTDIFF, HD(TL(S)));
        TL(S) = TL(TL(S));
        continue;
      case COLON_OP:
        if (!(NARGS >= 2)) {
          goto BREAK_MAIN_LOOP;
        }

        // list indexing
        NARGS = NARGS - 2;
        {
          LIST HOLD = HD(S);

          // hides static M
          word M;

          HD(S) = (LIST)COLON_OP, E = S, S = HOLD;
          TL(S) = reduce(TL(S));

          if (!(ISNUM(TL(S)) && (M = GETNUM(TL(S))) >= LISTBASE)) {
            HOLDARG = (LIST *)-1;
            goto BREAK_MAIN_LOOP;
          }

          while (M-- > LISTBASE) {
            // clobbers static M
            E = reduce(TL(TL(E)));

            if (!(ISCONS(E) && HD(E) == (LIST)COLON_OP)) {
              badexp(CONS(E, STONUM(M + 1)));
            }
          }

          E = HD(TL(E));
          HOLD = HD(S);
          HD(S) = (LIST)INDIR, TL(S) = E, S = HOLD;
          REDS = REDS + 1;
          continue;
        }
      case ZF_OP: {
        LIST HOLD = HD(S);
        NARGS = NARGS - 1;
        HD(S) = E, E = S, S = HOLD;

        if (TL(TL(E)) == NIL) {
          HD(E) = (LIST)COLON_OP, TL(E) = CONS(HD(TL(E)), NIL);
          continue;
        }

        {
          LIST QUALIFIER = HD(TL(E));
          LIST REST = TL(TL(E));

          if (ISCONS(QUALIFIER) && HD(QUALIFIER) == (LIST)GENERATOR) {
            LIST SOURCE = reduce(TL(TL(QUALIFIER)));
            LIST FORMAL = HD(TL(QUALIFIER));
            TL(TL(QUALIFIER)) = SOURCE;

            if (SOURCE == NIL) {
              HD(E) = (LIST)INDIR, TL(E) = NIL, E = NIL;
            } else if (ISCONS(SOURCE) && HD(SOURCE) == (LIST)COLON_OP) {

              HD(E) = CONS(
                  (LIST)INTERLEAVEFN,
                  CONS((LIST)ZF_OP, substitute(HD(TL(SOURCE)), FORMAL, REST)));

              TL(E) =
                  CONS((LIST)ZF_OP,
                       CONS(CONS((LIST)GENERATOR, CONS(FORMAL, TL(TL(SOURCE)))),
                            REST));

              //                            ) HD!E,TL!E:=APPEND.OP,
              //                                            CONS(
              //            CONS(ZF.OP,substitute(HD!(TL!SOURCE),FORMAL,REST)),
              //    CONS(ZF.OP,CONS(CONS(GENERATOR,CONS(FORMAL,TL!(TL!SOURCE))),REST))
              //                                                )
            } else {
              badexp(E);
            }

          } else {

            // qualifier is guard
            QUALIFIER = reduce(QUALIFIER);
            HD(TL(E)) = QUALIFIER;

            if (QUALIFIER == TRUTH) {
              TL(E) = REST;
            } else if (QUALIFIER == FALSITY) {
              HD(E) = (LIST)INDIR, TL(E) = NIL, E = NIL;
            } else {
              badexp(CONS((LIST)GUARD, QUALIFIER));
            }
          }

          REDS = REDS + 1;
          continue;
        }
      }
      case DOT_OP:
        if (!(NARGS >= 2)) {
          LIST A = reduce(HD(TL(S))), B = reduce(TL(TL(S)));

          if (!(isfun(A) && isfun(B))) {
            badexp(CONS(E, CONS(A, B)));
          }

          goto BREAK_MAIN_LOOP;
        }

        {
          LIST HOLD = HD(S);
          NARGS = NARGS - 1;
          E = HD(TL(S)), TL(HOLD) = CONS(TL(TL(S)), TL(HOLD));
          HD(S) = (LIST)DOT_OP, S = HOLD;
          REDS = REDS + 1;
          continue;
        }
      case EQ_OP:
      case NE_OP:
        E = equalval(HD(TL(S)), TL(TL(S))) == (E == (LIST)EQ_OP) ? TRUTH
                                                                 : FALSITY;
        // note - could rewrite for fast exit, here and in
        // other cases where result of reduction is atomic
        {
          LIST HOLD = HD(S);
          NARGS = NARGS - 1;
          HD(S) = (LIST)INDIR, TL(S) = E, S = HOLD;
          REDS = REDS + 1;
          continue;
        }
      case ENDOFSTACK:
        badexp((LIST)SILLYNESS);
      // occurs if we try to evaluate an exp we are already inside
      default:
        break;
      }
      // end of switch

      {
        // strict operators
        LIST A = NIL, B = NIL;
        bool STRINGS = false;

        // The values of M and N when STRINGS == true
        ATOM SM, SN;

        if ((word)E >= LENGTH_OP) {
          // monadic
          A = reduce(TL(S));
        } else {
          // DIADIC
          A = reduce(HD(TL(S)));

          // strict in 2nd arg ?
          // yes
          if (E >= (LIST)GR_OP) {

            B = reduce(E == (LIST)COMMADOTDOT_OP ? HD(TL(TL(S))) : TL(TL(S)));

            if (ISNUM(A) && ISNUM(B)) {

              M = GETNUM(A), N = GETNUM(B);

            } else if (E <= (LIST)LS_OP && ISCONS(A) && ISCONS(B) &&
                       HD(A) == (LIST)QUOTE && (LIST)QUOTE == HD(B)) {
              // relops

              STRINGS = true, SM = (ATOM)TL(A), SN = (ATOM)TL(B);

            } else if (E == (LIST)DOTDOT_OP && ISNUM(A) && B == INFINITY) {

              M = GETNUM(A), N = M;

            } else {

              badexp(CONS(E, CONS(A, E == (LIST)COMMADOTDOT_OP
                                         ? CONS(B, TL(TL(TL(S))))
                                         : B)));
            }
          }
          // no
          else {
            B = TL(TL(S));
          }
        }
        switch ((word)E) {
        case AND_OP:
          if (A == FALSITY) {
            E = A;
          } else if (A == TRUTH) {
            E = B;
          } else {
            badexp(CONS(E, CONS(A, B)));
          }
          break;
        case OR_OP:
          if (A == TRUTH) {
            E = A;
          } else if (A == FALSITY) {
            E = B;
          } else {
            badexp(CONS(E, CONS(A, B)));
          }
          break;
        case APPEND_OP:
          if (A == NIL) {
            E = B;
            break;
          }

          if (!(ISCONS(A) && HD(A) == (LIST)COLON_OP)) {
            badexp(CONS(E, CONS(A, B)));
          }

          E = (LIST)COLON_OP;
          TL(TL(S)) = CONS((LIST)APPEND_OP, CONS(TL(TL(A)), B));
          HD(TL(S)) = HD(TL(A));
          REDS = REDS + 1;
          continue;
        case DOTDOT_OP:
          if (M > N) {
            E = NIL;
            break;
          }
          E = (LIST)COLON_OP;
          TL(TL(S)) = CONS((LIST)DOTDOT_OP, CONS(STONUM(M + 1), B));
          REDS = REDS + 1;
          continue;
        case COMMADOTDOT_OP: {
          // reduce clobbers M,N
          word M1 = M, N1 = N;
          LIST C = reduce(TL(TL(TL(S))));
          static word P = 0;

          if (ISNUM(C)) {
            P = GETNUM(C);
          } else if (C == INFINITY) {
            P = N1;
          } else {
            badexp(CONS(E, CONS(A, CONS(B, C))));
          }

          if ((N1 - M1) * (P - M1) < 0) {
            E = NIL;
            break;
          }
          E = (LIST)COLON_OP;
          HD(TL(TL(S))) = STONUM(N1 + N1 - M1);
          TL(TL(S)) = CONS((LIST)COMMADOTDOT_OP, CONS(B, TL(TL(S))));
          REDS = REDS + 1;
          continue;
        }
        case NOT_OP:
          if (A == TRUTH) {
            E = FALSITY;
          } else if (A == FALSITY) {
            E = TRUTH;
          } else {
            badexp(CONS(E, A));
          }
          break;
        case NEG_OP:
          if (!(ISNUM(A))) {
            badexp(CONS(E, A));
          }
          E = STONUM(-GETNUM(A));
          break;
        case LENGTH_OP: {
          word L = 0;

          while (ISCONS(A) && HD(A) == (LIST)COLON_OP) {
            A = reduce(TL(TL(A))), L = L + 1;
          }

          if (A == NIL) {
            E = STONUM(L);
            break;
          }
          badexp(CONS((LIST)COLON_OP, CONS((LIST)ETC, A)));
        }
        case PLUS_OP: {
          word X = M + N;
          if ((M > 0 && N > 0 && X <= 0) || (M < 0 && N < 0 && X >= 0) ||
              (X == -X && X != 0)) {
            // This checks for -(2**31)
            overflow(CONS((LIST)PLUS_OP, CONS(A, B)));
          }
          E = STONUM(X);
          break;
        }
        case MINUS_OP: {
          word X = M - N;

          if ((M < 0 && N > 0 && X > 0) || (M > 0 && N < 0 && X < 0) ||
              (X == -X && X != 0)) {
            overflow(CONS((LIST)MINUS_OP, CONS(A, B)));
          }

          E = STONUM(X);
          break;
        }
        case TIMES_OP: {
          word X = M * N;

          // may not catch all cases
          if ((M > 0 && N > 0 && X <= 0) || (M < 0 && N < 0 && X <= 0) ||
              (M < 0 && N > 0 && X >= 0) || (M > 0 && N < 0 && X >= 0) ||
              (X == -X && X != 0)) {
            overflow(CONS((LIST)TIMES_OP, CONS(A, B)));
          }

          E = STONUM(X);
          break;
        }
        case DIV_OP:
          if (N == 0) {
            badexp(CONS((LIST)DIV_OP, CONS(A, B)));
          }

          E = STONUM(M / N);
          break;
        case REM_OP:
          if (N == 0) {
            badexp(CONS((LIST)REM_OP, CONS(A, B)));
          }

          E = STONUM(M % N);
          break;
        case EXP_OP:
          if (N < 0) {
            badexp(CONS((LIST)EXP_OP, CONS(A, B)));
          }

          {
            word P = 1;
            while (!(N == 0)) {
              word X = P * M;

              // may not catch all cases
              if ((M > 0 && P > 0 && X <= 0) || (M < 0 && P < 0 && X <= 0) ||
                  (M < 0 && P > 0 && X >= 0) || (M > 0 && P < 0 && X >= 0) ||
                  (X == -X && X != 0)) {
                overflow(CONS((LIST)EXP_OP, CONS(A, B)));
              }

              P = X, N = N - 1;
            }
            E = STONUM(P);
            break;
          }
        case GR_OP:
          E = (STRINGS ? ALFA_LS(SN, SM) : M > N) ? TRUTH : FALSITY;
          break;
        case GE_OP:
          E = (STRINGS ? ALFA_LS(SN, SM) || SN == SM : M >= N) ? TRUTH
                                                               : FALSITY;
          break;
        case LE_OP:
          E = (STRINGS ? ALFA_LS(SM, SN) || SM == SN : M <= N) ? TRUTH
                                                               : FALSITY;
          break;
        case LS_OP:
          E = (STRINGS ? ALFA_LS(SM, SN) : M < N) ? TRUTH : FALSITY;
          break;
        default:
          bcpl_WRITES("IMPOSSIBLE OPERATOR IN \"reduce\"\n");
        }
        // end of switch

        {
          LIST HOLD = HD(S);
          NARGS = NARGS - 1;
          HD(S) = (LIST)INDIR, TL(S) = E, S = HOLD;
        }
      }
    }
    // end of operators

    REDS = REDS + 1;

  } while (1);
  // end of main loop

BREAK_MAIN_LOOP:
  while (!(S == (LIST)ENDOFSTACK)) {
    // unreverse reversed pointers

    LIST HOLD = HD(S);
    HD(S) = E, E = S, S = HOLD;
  }

  if (HOLDARG == (LIST *)-1) {
    badexp(E);
  }

  // reset ARG stackframe
  ARG = HOLDARG;

  S = HOLD_S;
  return E;
}

static LIST substitute(LIST ACTUAL, LIST FORMAL, LIST EXP) {

  if (EXP == FORMAL) {
    return ACTUAL;
  } else if (!ISCONS(EXP) || HD(EXP) == (LIST)QUOTE || binds(FORMAL, HD(EXP))) {
    return EXP;
  } else {
    LIST H = substitute(ACTUAL, FORMAL, HD(EXP));
    LIST T = substitute(ACTUAL, FORMAL, TL(EXP));
    return H == HD(EXP) && T == TL(EXP) ? EXP : CONS(H, T);
  }
}

static bool binds(LIST FORMAL, LIST X) {
  return ISCONS(X) && HD(X) == (LIST)GENERATOR && HD(TL(X)) == FORMAL;
}

// mark elements in the argument stack for preservation by the GC.
// this routine should be called by your BASES() function.
void reducer_bases(void (*F)(LIST *)) {
  LIST *AP;

  for (AP = ARGSPACE; AP <= ARGP; AP++) {
    F(AP);
  }
}
