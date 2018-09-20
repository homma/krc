// KRC REDUCER

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

void INIT_ARGSPACE(void) {
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

// local function declarations
static void SIZE(LIST E);

// primitive functions
static void FUNCTIONP(LIST E);
static void LISTP(LIST E);
static void STRINGP(LIST E);
static void NUMBERP(LIST E);
static void CHAR(LIST E);
static void SIZE(LIST E);
static void CODE(LIST E);
static void DECODE(LIST E);
static void CONCAT(LIST E);
static void EXPLODE(LIST E);
static void ABORT(LIST E);
static void STARTREAD(LIST E);
static void READ(LIST E);
static void WRITEAP(LIST E);
static void SEQ(LIST E);

// local function delarations
static void PRINTFUNCTION(LIST E);
static bool EQUALVAL(LIST A, LIST B);
static void BADEXP(LIST E);
static void OVERFLOW(LIST E);
static void OBEY(LIST EQNS, LIST E);
static bool ISFUN(LIST X);

static LIST REDUCE(LIST E);
static LIST SUBSTITUTE(LIST ACTUAL, LIST FORMAL, LIST EXP);
static bool BINDS(LIST FORMAL, LIST X);

// DT 2015
static void SHOWCH(unsigned char c);

static void R(char *S, void (*F)(LIST), word N) {
  ATOM A = MKATOM(S);
  LIST EQN = CONS((LIST)A, CONS((LIST)CALL_C, (LIST)F));

  if (!(F == READ)) {
    ENTERSCRIPT(A);
  }

  VAL(A) = CONS(CONS((LIST)N, NIL), CONS(EQN, NIL));
}

void SETUP_PRIMFNS_ETC(void) {

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
  R("function__", FUNCTIONP, 1);
  R("list__", LISTP, 1);
  R("string__", STRINGP, 1);
  R("number__", NUMBERP, 1);
  R("char__", CHAR, 1);
  R("printwidth__", SIZE, 1);
  R("ord__", CODE, 1);
  R("chr__", DECODE, 1);
  R("implode__", CONCAT, 1);
  R("explode__", EXPLODE, 1);
  R("abort__", ABORT, 1);
  R("read__", STARTREAD, 1);
  R("read ", READ, 1);
  R("seq__", SEQ, 2);
  R("write__", WRITEAP, 3);
  BADFILE = MKATOM("<cannot open file:>");
  READFN = MKATOM("read ");
  WRITEFN = MKATOM("write");
  INTERLEAVEFN = MKATOM("interleave");
}

// little routine to avoid s having to be global, just because
// it may need fixing up after an interrupt. this routine does that.
void FIXUP_S(void) {

  // in case interrupt struck while reduce was dissecting a constant
  if (!(S == (LIST)ENDOFSTACK)) {
    HD(S) = (LIST)QUOTE;
  }
}

// return an upper-case copy of a string.
// copy to static area of 80 chars, the same as BCPL
// also to avoid calling strdup which calls malloc() and
// contaminates the garbage collection done with Boehm GC.
char *SCASECONV(char *S) {
  static char T[80 + 1];
  char *p = S, *q = T;

  while (*p) {
    *q++ = caseconv(*p++);
  }

  *q = '\0';
  return T;
}

void INITSTATS() { REDS = 0; }

void OUTSTATS() { fprintf(bcpl_OUTPUT, "reductions = %" W "\n", REDS); }

// the possible values of a reduced expression are:
//  VAL:= CONST | FUNCTION | LIST
//  CONST:= NUM | CONS(QUOTE,ATOM)
//  LIST:= NIL | CONS(COLON_OP,CONS(EXP,EXP))
//  FUNCTION:= NAME | CONS(E1,E2)

void PRINTVAL(LIST E, bool FORMAT) {

  E = REDUCE(E);

  if (E == NIL) {

    if (FORMAT) {
      bcpl_WRITES("[]");
    }

  } else if (ISNUM(E)) {

    bcpl_WRITEN(GETNUM(E));

  } else if (ISCONS(E)) {

    LIST H = HD(E);

    if (H == (LIST)QUOTE) {

      PRINTATOM((ATOM)TL(E), FORMAT);

    } else if (H == (LIST)COLON_OP) {

      if (FORMAT) {
        (*_WRCH)('[');
      }

      E = TL(E);
      do {
        PRINTVAL(HD(E), FORMAT);
        E = TL(E);
        E = REDUCE(E);

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

        BADEXP(CONS((LIST)COLON_OP, CONS((LIST)ETC, E)));
      }

    } else if (ISCONS(H) && HD(H) == (LIST)WRITEFN) {

      TL(H) = REDUCE(TL(H));

      if (!(ISCONS(TL(H)) && HD(TL(H)) == (LIST)QUOTE)) {
        BADEXP(E);
      }

      {
        char *F = PRINTNAME((ATOM)TL(TL(H)));
        FILE *OUT = FINDCHANNEL(F);
        FILE *HOLD = bcpl_OUTPUT;

        if (!(OUT != NULL)) {
          BADEXP(CONS((LIST)BADFILE, TL(H)));
        }

        bcpl_OUTPUT_fp = (OUT);
        PRINTVAL(TL(E), FORMAT);
        bcpl_OUTPUT_fp = (HOLD);
      }

    } else {

      // a partial application or composition
      PRINTFUNCTION(E);
    }
  } else {

    // only possibility here should be name of function
    PRINTFUNCTION(E);
  }
}

void PRINTATOM(ATOM A, bool FORMAT) {

  if (FORMAT) {

    // DT 2015
    int I;
    (*_WRCH)('"');

    for (I = 1; I <= LEN(A); I++) {
      SHOWCH(NAME(A)[I]);
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

static void SHOWCH(unsigned char c) {
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

static void PRINTFUNCTION(LIST E) {

  (*_WRCH)('<');
  PRINTEXP(E, 0);
  (*_WRCH)('>');
}

// unpredictable results if A,B both functions
static bool EQUALVAL(LIST A, LIST B) {
  do {
    A = REDUCE(A);
    B = REDUCE(B);

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
    if (!(EQUALVAL(HD(A), HD(B)))) {
      return false;
    }

    A = TL(A), B = TL(B);
  } while (1);
}

// called for all evaluation errors
static void BADEXP(LIST E) {

  _WRCH = TRUEWRCH;
  CLOSECHANNELS();
  bcpl_WRITES("\n**undefined expression**\n  ");
  PRINTEXP(E, 0);

  // could insert more detailed diagnostics here,
  // depending on nature of HD!E, for example:
  if (ISCONS(E) && (HD(E) == (LIST)COLON_OP || HD(E) == (LIST)APPEND_OP)) {
    bcpl_WRITES("\n  (non-list encountered where list expected)");
  }

  bcpl_WRITES("\n**evaluation abandoned**\n");
  ESCAPETONEXTCOMMAND();
}

// integer overflow handler
static void OVERFLOW(LIST E) {

  _WRCH = TRUEWRCH;
  CLOSECHANNELS();
  bcpl_WRITES("\n**integer overflow**\n  ");
  PRINTEXP(E, 0);
  bcpl_WRITES("\n**evaluation abandoned**\n");
  ESCAPETONEXTCOMMAND();
}

// a kludge
LIST BUILDEXP(LIST CODE) {

  // a bogus piece of graph
  LIST E = CONS(NIL, NIL);
  OBEY(CONS(CONS(NIL, CODE), NIL), E);

  // reset ARG stack
  ARGP = ARG - 1;
  return E;
}

// transform a piece of graph, E, in accordance
// with EQNS - actual params are found in
// *ARG ... *ARGP
// (warning - has side effect of raising ARGP)
static void OBEY(LIST EQNS, LIST E) {

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
        *ARGP = REDUCE(*ARGP);
        if (*ARGP == FALSITY) {
          goto BREAK_DECODE_LOOP;
        }
        if (!(*ARGP == TRUTH)) {
          BADEXP(CONS((LIST)GUARD, *ARGP));
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

        if (!(EQUALVAL(ARG[I], HD(CODE)))) {
          goto BREAK_DECODE_LOOP;
        }

        CODE = TL(CODE);
        break;
      }
      case MATCHARG_C: {
        word I = (word)HD(CODE);
        CODE = TL(CODE);

        if (!(EQUALVAL(ARG[I], ARG[(word)(HD(CODE))]))) {
          goto BREAK_DECODE_LOOP;
        }

        CODE = TL(CODE);
        break;
      }
      case MATCHPAIR_C: {
        LIST *P = ARG + (word)(HD(CODE));
        *P = REDUCE(*P);

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
        fprintf(bcpl_OUTPUT, "IMPOSSIBLE INSTRUCTION <%p> IN \"OBEY\"\n", H);
      }
    } while (1);
    // end of decode loop

  BREAK_DECODE_LOOP:
    EQNS = TL(EQNS);
    ARGP = HOLDARG;
  }

  // end of EQNS loop
  BADEXP(E);
}

static void STRINGP(LIST E) {
  *ARG = REDUCE(*ARG);
  HD(E) = (LIST)INDIR,
  TL(E) = ISCONS(*ARG) && HD(*ARG) == (LIST)QUOTE ? TRUTH : FALSITY;
}

static void NUMBERP(LIST E) {
  *ARG = REDUCE(*ARG);
  HD(E) = (LIST)INDIR, TL(E) = ISNUM(*ARG) ? TRUTH : FALSITY;
}

static void LISTP(LIST E) {
  *ARG = REDUCE(*ARG);
  HD(E) = (LIST)INDIR;
  TL(E) = (*ARG == NIL || (ISCONS(*ARG) && HD(*ARG) == (LIST)COLON_OP))
              ? TRUTH
              : FALSITY;
}

static void FUNCTIONP(LIST E) {
  *ARG = REDUCE(*ARG);
  HD(E) = (LIST)INDIR;
  TL(E) = ISFUN(*ARG) ? TRUTH : FALSITY;
}

static bool ISFUN(LIST X) {
  return ISATOM(X) || (ISCONS(X) && QUOTE != HD(X) && HD(X) != (LIST)COLON_OP);
}

static void CHAR(LIST E) {
  *ARG = REDUCE(*ARG);
  HD(E) = (LIST)INDIR;
  TL(E) = ISCONS(*ARG) && HD(*ARG) == (LIST)QUOTE && LEN((ATOM)TL(*ARG)) == 1
              ? TRUTH
              : FALSITY;
}

static word COUNT;
static void COUNTCH(word CH) { COUNT = COUNT + 1; }

static void SIZE(LIST E) {
  COUNT = 0;
  _WRCH = COUNTCH;
  PRINTVAL(*ARG, false);
  _WRCH = TRUEWRCH;
  HD(E) = (LIST)INDIR, TL(E) = STONUM(COUNT);
}

static void CODE(LIST E) {
  *ARG = REDUCE(*ARG);

  if (!(ISCONS(*ARG) && HD(*ARG) == QUOTE)) {
    BADEXP(E);
  }

  {
    ATOM A = (ATOM)TL(*ARG);

    if (!(LEN(A) == 1)) {
      BADEXP(E);
    }

    HD(E) = (LIST)INDIR, TL(E) = STONUM((word)NAME(A)[1] & 0xff);
  }
}

static void DECODE(LIST E) {
  *ARG = REDUCE(*ARG);

  if (!(ISNUM(*ARG) && 0 <= (word)TL(*ARG) && (word)TL(*ARG) <= 255)) {
    BADEXP(E);
  }

  BUFCH((word)TL(*ARG));
  HD(E) = (LIST)INDIR, TL(E) = CONS((LIST)QUOTE, (LIST)PACKBUFFER());
}

static void CONCAT(LIST E) {
  *ARG = REDUCE(*ARG);
  {
    LIST A = *ARG;

    while (ISCONS(A) && HD(A) == (LIST)COLON_OP) {
      LIST C = REDUCE(HD(TL(A)));

      if (!(ISCONS(C) && HD(C) == (LIST)QUOTE)) {
        BADEXP(E);
      }

      HD(TL(A)) = C;
      TL(TL(A)) = REDUCE(TL(TL(A)));
      A = TL(TL(A));
    }

    if (!(A == NIL)) {
      BADEXP(E);
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

static void EXPLODE(LIST E) {
  *ARG = REDUCE(*ARG);
  if (!(ISCONS(*ARG) && HD(*ARG) == (LIST)QUOTE)) {
    BADEXP(E);
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

static void ABORT(LIST E) {
  FILE *HOLD = bcpl_OUTPUT;
  bcpl_OUTPUT_fp = (stderr);
  bcpl_WRITES("\nprogram error: ");
  PRINTVAL(TL(E), false);
  (*_WRCH)('\n');
  bcpl_OUTPUT_fp = (HOLD);
  ABORTED = true;
  raise(SIGINT);
}

static void STARTREAD(LIST E) {

  *ARG = REDUCE(*ARG);

  if (!(ISCONS(*ARG) && HD(*ARG) == (LIST)QUOTE)) {
    BADEXP(E);
  }

  {
    FILE *IN = bcpl_FINDINPUT(PRINTNAME((ATOM)TL(*ARG)));

    if (!(IN != NULL)) {
      BADEXP(CONS((LIST)BADFILE, *ARG));
    }

    HD(E) = (LIST)READFN, TL(E) = (LIST)IN;
  }
}

static void READ(LIST E) {

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
static void WRITEAP(LIST E) { BADEXP(E); }

// seq a b evaluates a then returns b, added DT 2015
static void SEQ(LIST E) {
  REDUCE(TL(HD(E)));
  HD(E) = (LIST)INDIR;
}

// possibilities for leftmost field of a graph are:
// HEAD:= NAME | NUM | NIL | OPERATOR

static LIST REDUCE(LIST E) {
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

        BADEXP(E);

      } else {
        // undefined name

        // variable

        if (HD(HD(VAL((ATOM)E))) == 0) {
          LIST EQN = HD(TL(VAL((ATOM)E)));

          // memo not set
          if (HD(EQN) == 0) {
            HD(EQN) = BUILDEXP(TL(EQN));
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
            OBEY(EQNS, E);

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
          TL(S) = REDUCE(TL(S));

          if (!(ISNUM(TL(S)) && (M = GETNUM(TL(S))) >= LISTBASE)) {
            HOLDARG = (LIST *)-1;
            goto BREAK_MAIN_LOOP;
          }

          while (M-- > LISTBASE) {
            E = REDUCE(TL(TL(E))); // Clobbers static M

            if (!(ISCONS(E) && HD(E) == (LIST)COLON_OP)) {
              BADEXP(CONS(E, STONUM(M + 1)));
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
            LIST SOURCE = REDUCE(TL(TL(QUALIFIER)));
            LIST FORMAL = HD(TL(QUALIFIER));
            TL(TL(QUALIFIER)) = SOURCE;

            if (SOURCE == NIL) {
              HD(E) = (LIST)INDIR, TL(E) = NIL, E = NIL;
            } else if (ISCONS(SOURCE) && HD(SOURCE) == (LIST)COLON_OP) {

              HD(E) = CONS(
                  (LIST)INTERLEAVEFN,
                  CONS((LIST)ZF_OP, SUBSTITUTE(HD(TL(SOURCE)), FORMAL, REST)));

              TL(E) =
                  CONS((LIST)ZF_OP,
                       CONS(CONS((LIST)GENERATOR, CONS(FORMAL, TL(TL(SOURCE)))),
                            REST));

              //                            ) HD!E,TL!E:=APPEND.OP,
              //                                            CONS(
              //            CONS(ZF.OP,SUBSTITUTE(HD!(TL!SOURCE),FORMAL,REST)),
              //    CONS(ZF.OP,CONS(CONS(GENERATOR,CONS(FORMAL,TL!(TL!SOURCE))),REST))
              //                                                )
            } else {
              BADEXP(E);
            }

          } else {

            // qualifier is guard
            QUALIFIER = REDUCE(QUALIFIER);
            HD(TL(E)) = QUALIFIER;

            if (QUALIFIER == TRUTH) {
              TL(E) = REST;
            } else if (QUALIFIER == FALSITY) {
              HD(E) = (LIST)INDIR, TL(E) = NIL, E = NIL;
            } else {
              BADEXP(CONS((LIST)GUARD, QUALIFIER));
            }
          }

          REDS = REDS + 1;
          continue;
        }
      }
      case DOT_OP:
        if (!(NARGS >= 2)) {
          LIST A = REDUCE(HD(TL(S))), B = REDUCE(TL(TL(S)));

          if (!(ISFUN(A) && ISFUN(B))) {
            BADEXP(CONS(E, CONS(A, B)));
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
        E = EQUALVAL(HD(TL(S)), TL(TL(S))) == (E == (LIST)EQ_OP) ? TRUTH
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
        BADEXP((LIST)SILLYNESS);
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
          A = REDUCE(TL(S));
        } else {
          // DIADIC
          A = REDUCE(HD(TL(S)));

          // strict in 2nd arg ?
          // yes
          if (E >= (LIST)GR_OP) {

            B = REDUCE(E == (LIST)COMMADOTDOT_OP ? HD(TL(TL(S))) : TL(TL(S)));

            if (ISNUM(A) && ISNUM(B)) {

              M = GETNUM(A), N = GETNUM(B);

            } else if (E <= (LIST)LS_OP && ISCONS(A) && ISCONS(B) &&
                       HD(A) == (LIST)QUOTE && (LIST)QUOTE == HD(B)) {
              // relops

              STRINGS = true, SM = (ATOM)TL(A), SN = (ATOM)TL(B);

            } else if (E == (LIST)DOTDOT_OP && ISNUM(A) && B == INFINITY) {

              M = GETNUM(A), N = M;

            } else {

              BADEXP(CONS(E, CONS(A, E == (LIST)COMMADOTDOT_OP
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
            BADEXP(CONS(E, CONS(A, B)));
          }
          break;
        case OR_OP:
          if (A == TRUTH) {
            E = A;
          } else if (A == FALSITY) {
            E = B;
          } else {
            BADEXP(CONS(E, CONS(A, B)));
          }
          break;
        case APPEND_OP:
          if (A == NIL) {
            E = B;
            break;
          }

          if (!(ISCONS(A) && HD(A) == (LIST)COLON_OP)) {
            BADEXP(CONS(E, CONS(A, B)));
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
          LIST C = REDUCE(TL(TL(TL(S))));
          static word P = 0;

          if (ISNUM(C)) {
            P = GETNUM(C);
          } else if (C == INFINITY) {
            P = N1;
          } else {
            BADEXP(CONS(E, CONS(A, CONS(B, C))));
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
            BADEXP(CONS(E, A));
          }
          break;
        case NEG_OP:
          if (!(ISNUM(A))) {
            BADEXP(CONS(E, A));
          }
          E = STONUM(-GETNUM(A));
          break;
        case LENGTH_OP: {
          word L = 0;

          while (ISCONS(A) && HD(A) == (LIST)COLON_OP) {
            A = REDUCE(TL(TL(A))), L = L + 1;
          }

          if (A == NIL) {
            E = STONUM(L);
            break;
          }
          BADEXP(CONS((LIST)COLON_OP, CONS((LIST)ETC, A)));
        }
        case PLUS_OP: {
          word X = M + N;
          if ((M > 0 && N > 0 && X <= 0) || (M < 0 && N < 0 && X >= 0) ||
              (X == -X && X != 0)) {
            // This checks for -(2**31)
            OVERFLOW(CONS((LIST)PLUS_OP, CONS(A, B)));
          }
          E = STONUM(X);
          break;
        }
        case MINUS_OP: {
          word X = M - N;

          if ((M < 0 && N > 0 && X > 0) || (M > 0 && N < 0 && X < 0) ||
              (X == -X && X != 0)) {
            OVERFLOW(CONS((LIST)MINUS_OP, CONS(A, B)));
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
            OVERFLOW(CONS((LIST)TIMES_OP, CONS(A, B)));
          }

          E = STONUM(X);
          break;
        }
        case DIV_OP:
          if (N == 0) {
            BADEXP(CONS((LIST)DIV_OP, CONS(A, B)));
          }

          E = STONUM(M / N);
          break;
        case REM_OP:
          if (N == 0) {
            BADEXP(CONS((LIST)REM_OP, CONS(A, B)));
          }

          E = STONUM(M % N);
          break;
        case EXP_OP:
          if (N < 0) {
            BADEXP(CONS((LIST)EXP_OP, CONS(A, B)));
          }

          {
            word P = 1;
            while (!(N == 0)) {
              word X = P * M;

              // may not catch all cases
              if ((M > 0 && P > 0 && X <= 0) || (M < 0 && P < 0 && X <= 0) ||
                  (M < 0 && P > 0 && X >= 0) || (M > 0 && P < 0 && X >= 0) ||
                  (X == -X && X != 0)) {
                OVERFLOW(CONS((LIST)EXP_OP, CONS(A, B)));
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
          bcpl_WRITES("IMPOSSIBLE OPERATOR IN \"REDUCE\"\n");
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
    BADEXP(E);
  }

  // reset ARG stackframe
  ARG = HOLDARG;

  S = HOLD_S;
  return E;
}

static LIST SUBSTITUTE(LIST ACTUAL, LIST FORMAL, LIST EXP) {

  if (EXP == FORMAL) {
    return ACTUAL;
  } else if (!ISCONS(EXP) || HD(EXP) == (LIST)QUOTE || BINDS(FORMAL, HD(EXP))) {
    return EXP;
  } else {
    LIST H = SUBSTITUTE(ACTUAL, FORMAL, HD(EXP));
    LIST T = SUBSTITUTE(ACTUAL, FORMAL, TL(EXP));
    return H == HD(EXP) && T == TL(EXP) ? EXP : CONS(H, T);
  }
}

static bool BINDS(LIST FORMAL, LIST X) {
  return ISCONS(X) && HD(X) == (LIST)GENERATOR && HD(TL(X)) == FORMAL;
}

// mark elements in the argument stack for preservation by the GC.
// this routine should be called by your BASES() function.
void REDUCER_BASES(void (*F)(LIST *)) {
  LIST *AP;

  for (AP = ARGSPACE; AP <= ARGP; AP++) {
    F(AP);
  }
}
