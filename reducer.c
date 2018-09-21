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
list MEMORIES = NIL;
word REDS;

// base for list indexing
word LISTBASE = 0;

word ABORTED = false;

static atom ETC, SILLYNESS, GUARD, LISTDIFF, BADFILE, READFN, WRITEFN,
    INTERLEAVEFN;

// argument stack. ARGP points to the last cell allocated
static list *ARGSPACE = NULL;
static list *ARG;
static list *ARGMAX;
static list *ARGP;

void init_argspace(void) {
  if (ARGSPACE == NULL) {

    // number of list cells, in listlib.c
    extern int SPACE;

    // empirically, using edigits,
    // with SPACE/6, the argstack exhausts first.
    // with /5, it runs out of heap first.
    int NARGS = SPACE / 5;

    ARGSPACE = (list *)malloc(NARGS * sizeof(*ARGSPACE));

    if (ARGSPACE == (void *)-1) {
      space_error("Cannot allocate argument stack");
    }

    ARGMAX = ARGSPACE + NARGS - 1;
  }
  ARG = ARGSPACE, ARGP = ARG - 1;
}

// sentinel value (impossible pointer)
#define ENDOFSTACK (-4)

list S;

// primitive functions
// renamed to distinguish primitive functions
static void prim_functionp(list E);
static void prim_listp(list E);
static void prim_stringp(list E);
static void prim_numberp(list E);
static void prim_char(list E);
static void prim_size(list E);
static void prim_code(list E);
static void prim_decode(list E);
static void prim_concat(list E);
static void prim_explode(list E);
static void prim_abort(list E);
static void prim_startread(list E);
static void prim_read(list E);
static void prim_writeap(list E);
static void prim_seq(list E);

// local function delarations
static void printfunction(list E);
static bool equalval(list A, list B);
static void badexp(list E);
static void overflow(list E);
static void obey(list EQNS, list E);
static bool isfun(list X);

static list reduce(list E);
static list substitute(list ACTUAL, list FORMAL, list EXP);
static bool binds(list FORMAL, list X);

// DT 2015
static void showch(unsigned char c);

static void R(char *S, void (*F)(list), word N) {

  // ((atom sym) . (c_call . fun))
  atom A = mkatom(S);
  list EQN = cons((list)A, cons((list)CALL_C, (list)F));

  if (!(F == prim_read)) {
    enterscript(A);
  }

  VAL(A) = cons(cons((list)N, NIL), cons(EQN, NIL));
}

void setup_primfns_etc(void) {

  // S is used inside reduce
  S = (list)ENDOFSTACK;

  // miscellaneous initialisations
  ETC = mkatom("... ");

  SILLYNESS = mkatom("<unfounded recursion>");
  GUARD = mkatom("<non truth-value used as guard:>");
  TRUTH = cons((list)QUOTE, (list)mkatom("TRUE"));
  FALSITY = cons((list)QUOTE, (list)mkatom("FALSE"));
  LISTDIFF = mkatom("listdiff");
  INFINITY = cons((list)QUOTE, (list)-3);

  // primitive functions
  R("function__", prim_functionp, 1);
  R("list__", prim_listp, 1);
  R("string__", prim_stringp, 1);
  R("number__", prim_numberp, 1);
  R("char__", prim_char, 1);
  R("printwidth__", prim_size, 1);
  R("ord__", prim_code, 1);
  R("chr__", prim_decode, 1);
  R("implode__", prim_concat, 1);
  R("explode__", prim_explode, 1);
  R("abort__", prim_abort, 1);
  R("read__", prim_startread, 1);
  R("read ", prim_read, 1);
  R("seq__", prim_seq, 2);
  R("write__", prim_writeap, 3);
  BADFILE = mkatom("<cannot open file:>");
  READFN = mkatom("read ");
  WRITEFN = mkatom("write");
  INTERLEAVEFN = mkatom("interleave");
}

// little routine to avoid s having to be global, just because
// it may need fixing up after an interrupt. this routine does that.
void fixup_s(void) {

  // in case interrupt struck while reduce was dissecting a constant
  if (!(S == (list)ENDOFSTACK)) {
    HD(S) = (list)QUOTE;
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
//  CONST:= NUM | cons(QUOTE, ATOM)
//  LIST:= NIL | cons(COLON_OP,cons(EXP, EXP))
//  FUNCTION:= NAME | cons(E1, E2)

void printval(list E, bool FORMAT) {

  E = reduce(E);

  if (E == NIL) {

    if (FORMAT) {
      bcpl_writes("[]");
    }

  } else if (isnum(E)) {

    bcpl_writen(getnum(E));

  } else if (iscons(E)) {

    list H = HD(E);

    if (H == (list)QUOTE) {

      printatom((atom)TL(E), FORMAT);

    } else if (H == (list)COLON_OP) {

      if (FORMAT) {
        (*_WRCH)('[');
      }

      E = TL(E);
      do {
        printval(HD(E), FORMAT);
        E = TL(E);
        E = reduce(E);

        if (!(iscons(E))) {
          break;
        }

        if (HD(E) == (list)COLON_OP) {

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

        badexp(cons((list)COLON_OP, cons((list)ETC, E)));
      }

    } else if (iscons(H) && HD(H) == (list)WRITEFN) {

      TL(H) = reduce(TL(H));

      if (!(iscons(TL(H)) && HD(TL(H)) == (list)QUOTE)) {
        badexp(E);
      }

      {
        char *F = PRINTNAME((atom)TL(TL(H)));
        FILE *OUT = findchannel(F);
        FILE *HOLD = bcpl_OUTPUT;

        if (!(OUT != NULL)) {
          badexp(cons((list)BADFILE, TL(H)));
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

void printatom(atom A, bool FORMAT) {

  if (FORMAT) {

    // DT 2015
    int I;
    (*_WRCH)('"');

    for (I = 0; I < LEN(A); I++) {
      showch(NAME(A)[I]);
    }

    (*_WRCH)('"');

  } else {

    int I;
    for (I = 0; I < LEN(A); I++) {
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

static void printfunction(list E) {

  (*_WRCH)('<');
  printexp(E, 0);
  (*_WRCH)('>');
}

// unpredictable results if A,B both functions
static bool equalval(list A, list B) {
  do {
    A = reduce(A);
    B = reduce(B);

    if (A == B) {
      return true;
    }

    if (isnum(A) && isnum(B)) {
      return getnum(A) == getnum(B);
    }

    if (!(iscons(A) && iscons(B) && (HD(A) == HD(B)))) {
      return false;
    }

    if (HD(A) == (list)QUOTE || HD(A) == (list)QUOTE_OP) {
      return TL(A) == TL(B);
    }

    if (!(HD(A) == (list)COLON_OP)) {
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
static void badexp(list E) {

  _WRCH = TRUEWRCH;
  closechannels();
  bcpl_writes("\n**undefined expression**\n  ");
  printexp(E, 0);

  // could insert more detailed diagnostics here,
  // depending on nature of HD!E, for example:
  if (iscons(E) && (HD(E) == (list)COLON_OP || HD(E) == (list)APPEND_OP)) {
    bcpl_writes("\n  (non-list encountered where list expected)");
  }

  bcpl_writes("\n**evaluation abandoned**\n");
  escapetonextcommand();
}

// integer overflow handler
static void overflow(list E) {

  _WRCH = TRUEWRCH;
  closechannels();
  bcpl_writes("\n**integer overflow**\n  ");
  printexp(E, 0);
  bcpl_writes("\n**evaluation abandoned**\n");
  escapetonextcommand();
}

// a kludge
list buildexp(list CODE) {

  // a bogus piece of graph
  list E = cons(NIL, NIL);
  obey(cons(cons(NIL, CODE), NIL), E);

  // reset ARG stack
  ARGP = ARG - 1;
  return E;
}

// transform a piece of graph, E, in accordance
// with EQNS - actual params are found in
// *ARG ... *ARGP
// (warning - has side effect of raising ARGP)
static void obey(list EQNS, list E) {

  // EQNS loop
  while (!(EQNS == NIL)) {

    list CODE = TL(HD(EQNS));
    list *HOLDARG = ARGP;
    word I;

    // decode loop
    do {
      list H = HD(CODE);
      CODE = TL(CODE);

      // first, check the only cases that increment ARGP
      switch ((word)H) {
      case LOAD_C:
      case LOADARG_C:
      case FORMLIST_C:
        ARGP = ARGP + 1;
        if (ARGP > ARGMAX) {
          space_error("Arg stack overflow");
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
          space_error("Arg stack overflow");
        }
        *ARGP = ARG[(word)(HD(CODE))];
        CODE = TL(CODE);
        break;
      case APPLYINFIX_C:
        *ARGP = cons(*(ARGP - 1), *ARGP);
        *(ARGP - 1) = HD(CODE);
        CODE = TL(CODE);
      case APPLY_C:
        ARGP = ARGP - 1;
        if (HD(CODE) == (list)STOP_C) {
          HD(E) = *ARGP, TL(E) = *(ARGP + 1);
          return;
        }
        *ARGP = cons(*ARGP, *(ARGP + 1));
        break;
      case CONTINUE_INFIX_C:
        *(ARGP - 1) = cons(HD(CODE), cons(*(ARGP - 1), *ARGP));
        CODE = TL(CODE);
        break;
      case IF_C:
        *ARGP = reduce(*ARGP);
        if (*ARGP == FALSITY) {
          goto BREAK_DECODE_LOOP;
        }
        if (!(*ARGP == TRUTH)) {
          badexp(cons((list)GUARD, *ARGP));
        }
        break;
      case FORMLIST_C:
        // ARGP=ARGP+1;
        *ARGP = NIL;
        for (I = 1; I <= (word)HD(CODE); I++) {
          ARGP = ARGP - 1;
          *ARGP = cons((list)COLON_OP, cons(*ARGP, *(ARGP + 1)));
        }
        CODE = TL(CODE);
        break;
      case FORMZF_C: {
        list X = cons(*(ARGP - (word)HD(CODE)), NIL);
        list *P;
        for (P = ARGP; P >= ARGP - (word)HD(CODE) + 1; P = P - 1) {
          X = cons(*P, X);
        }

        ARGP = ARGP - (word)HD(CODE);
        *ARGP = cons((list)ZF_OP, X);
        CODE = TL(CODE);
        break;
      }
      case CONT_GENERATOR_C:
        for (I = 1; I <= (word)HD(CODE); I++) {
          *(ARGP - I) = cons((list)GENERATOR, cons(*(ARGP - I), TL(TL(*ARGP))));
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
        list *P = ARG + (word)(HD(CODE));
        *P = reduce(*P);

        if (!(iscons(*P) && HD(*P) == (list)COLON_OP)) {
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
        HD(E) = (list)INDIR, TL(E) = *ARGP;
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

static void prim_functionp(list E) {
  *ARG = reduce(*ARG);
  HD(E) = (list)INDIR;
  TL(E) = isfun(*ARG) ? TRUTH : FALSITY;
}

static void prim_listp(list E) {
  *ARG = reduce(*ARG);
  HD(E) = (list)INDIR;
  TL(E) = (*ARG == NIL || (iscons(*ARG) && HD(*ARG) == (list)COLON_OP))
              ? TRUTH
              : FALSITY;
}

static void prim_stringp(list E) {
  *ARG = reduce(*ARG);
  HD(E) = (list)INDIR,
  TL(E) = iscons(*ARG) && HD(*ARG) == (list)QUOTE ? TRUTH : FALSITY;
}

static void prim_numberp(list E) {
  *ARG = reduce(*ARG);
  HD(E) = (list)INDIR, TL(E) = isnum(*ARG) ? TRUTH : FALSITY;
}

static bool isfun(list X) {
  return isatom(X) || (iscons(X) && QUOTE != HD(X) && HD(X) != (list)COLON_OP);
}

static void prim_char(list E) {
  *ARG = reduce(*ARG);
  HD(E) = (list)INDIR;
  TL(E) = iscons(*ARG) && HD(*ARG) == (list)QUOTE && LEN((atom)TL(*ARG)) == 1
              ? TRUTH
              : FALSITY;
}

static word count;
static void countch(word CH) { count = count + 1; }

static void prim_size(list E) {

  count = 0;
  _WRCH = countch;
  printval(*ARG, false);
  _WRCH = TRUEWRCH;
  HD(E) = (list)INDIR, TL(E) = stonum(count);
}

// converts a character to its ascii number
static void prim_code(list E) {

  *ARG = reduce(*ARG);

  if (!(iscons(*ARG) && HD(*ARG) == QUOTE)) {
    badexp(E);
  }

  {
    atom A = (atom)TL(*ARG);

    if (!(LEN(A) == 1)) {
      badexp(E);
    }

    HD(E) = (list)INDIR;
    TL(E) = stonum((word)NAME(A)[0] & 0xff);
  }
}

static void prim_decode(list E) {
  *ARG = reduce(*ARG);

  if (!(isnum(*ARG) && 0 <= (word)TL(*ARG) && (word)TL(*ARG) <= 255)) {
    badexp(E);
  }

  bufch((word)TL(*ARG));
  HD(E) = (list)INDIR, TL(E) = cons((list)QUOTE, (list)packbuffer());
}

static void prim_concat(list E) {

  *ARG = reduce(*ARG);

  {
    list A = *ARG;

    while (iscons(A) && HD(A) == (list)COLON_OP) {
      list C = reduce(HD(TL(A)));

      if (!(iscons(C) && HD(C) == (list)QUOTE)) {
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
      atom N = (atom)TL(HD(TL(A)));
      int I;

      for (I = 0; I < LEN(N); I++) {
        bufch(NAME(N)[I]);
      }

      A = TL(TL(A));
    }
    A = (list)packbuffer();
    HD(E) = (list)INDIR,
    TL(E) = A == TL(TRUTH) ? TRUTH
                           : A == TL(FALSITY) ? FALSITY : cons((list)QUOTE, A);
  }
}

static void prim_explode(list E) {

  *ARG = reduce(*ARG);

  if (!(iscons(*ARG) && HD(*ARG) == (list)QUOTE)) {
    badexp(E);
  }

  {
    atom A = (atom)TL(*ARG);
    list X = NIL;

    int I;
    for (I = LEN(A); I > 0; I--) {
      bufch(NAME(A)[I - 1]);
      X = cons((list)COLON_OP, cons(cons((list)QUOTE, (list)packbuffer()), X));
    }

    HD(E) = (list)INDIR, TL(E) = X;
  }
}

static void prim_abort(list E) {

  FILE *HOLD = bcpl_OUTPUT;
  bcpl_OUTPUT_fp = (stderr);

  bcpl_writes("\nprogram error: ");

  printval(TL(E), false);

  (*_WRCH)('\n');

  bcpl_OUTPUT_fp = (HOLD);
  ABORTED = true;
  raise(SIGINT);
}

static void prim_startread(list E) {

  *ARG = reduce(*ARG);

  if (!(iscons(*ARG) && HD(*ARG) == (list)QUOTE)) {
    badexp(E);
  }

  {
    FILE *IN = bcpl_findinput(PRINTNAME((atom)TL(*ARG)));

    if (!(IN != NULL)) {
      badexp(cons((list)BADFILE, *ARG));
    }

    HD(E) = (list)READFN, TL(E) = (list)IN;
  }
}

static void prim_read(list E) {

  FILE *IN = (FILE *)TL(E);
  bcpl_INPUT_fp = (IN);
  HD(E) = (list)INDIR, TL(E) = cons((list)READFN, TL(E));

  {
    list *X = &(TL(E));
    word C = (*_RDCH)();

    // read one character
    if (C != EOF) {
      char c = C;
      *X = cons((list)COLON_OP,
                cons(cons((list)QUOTE, (list)mkatomn(&c, 1)), *X));
      X = &(TL(TL(*X)));
    }

    if (ferror(IN)) {
      fprintf(bcpl_OUTPUT, "\n**File read error**\n");
      escapetonextcommand();
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
static void prim_writeap(list E) { badexp(E); }

// seq a b evaluates a then returns b, added DT 2015
static void prim_seq(list E) {

  reduce(TL(HD(E)));
  HD(E) = (list)INDIR;
}

// possibilities for leftmost field of a graph are:
// HEAD:= NAME | NUM | NIL | OPERATOR

static list reduce(list E) {
  static word M = 0;
  static word N = 0;
  list HOLD_S = S;
  word NARGS = 0;
  list *HOLDARG = ARG;

  // if ( &E>STACKLIMIT ) space_error("Arg stack overflow");
  // if ( ARGP>ARGMAX ) space_error("Arg stack overflow");

  S = (list)ENDOFSTACK;
  ARG = ARGP + 1;

  // main loop
  do {

    // find head, reversing pointers en route
    while (iscons(E)) {
      list HOLD = HD(E);
      NARGS = NARGS + 1;
      HD(E) = S, S = E, E = HOLD;
    }

    if (isnum(E) || E == NIL) {
      // unless NARGS==0 do HOLDARG=(list *)-1;  //flags an error
      goto BREAK_MAIN_LOOP;
    }

    // user defined name
    if (isatom(E)) {

      if (VAL((atom)E) == NIL || TL(VAL((atom)E)) == NIL) {

        badexp(E);

      } else {
        // undefined name

        // variable

        if (HD(HD(VAL((atom)E))) == 0) {
          list EQN = HD(TL(VAL((atom)E)));

          // memo not set
          if (HD(EQN) == 0) {
            HD(EQN) = buildexp(TL(EQN));
            MEMORIES = cons(E, MEMORIES);
          }

          E = HD(EQN);
        } else {

          // can we get cyclic expressions?

          // function

          // hides the static N
          word N = (word)HD(HD(VAL((atom)E)));

          if (N > NARGS) {
            // not enough ARGS
            goto BREAK_MAIN_LOOP;
          }

          {
            list EQNS = TL(VAL((atom)E));

            word I;
            for (I = 0; I <= N - 1; I++) {

              // move back up GRAPH,
              list HOLD = HD(S);

              // stacking ARGS en route
              ARGP = ARGP + 1;

              if (ARGP > ARGMAX) {
                space_error("Arg stack overflow");
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
          HOLDARG = (list *)-1;
        }
        goto BREAK_MAIN_LOOP;
      case INDIR: {
        list HOLD = HD(S);
        NARGS = NARGS - 1;
        E = TL(S), HD(S) = (list)INDIR, S = HOLD;
        continue;
      }
      case QUOTE_OP:
        if (!(NARGS >= 3)) {
          goto BREAK_MAIN_LOOP;
        }

        {
          list OP = TL(S);
          list HOLD = HD(S);
          NARGS = NARGS - 2;
          HD(S) = E, E = S, S = HOLD;
          HOLD = HD(S);
          HD(S) = E, E = S, S = HOLD;
          TL(S) = cons(TL(E), TL(S)), E = OP;
          continue;
        }
      case LISTDIFF_OP:
        E = cons((list)LISTDIFF, HD(TL(S)));
        TL(S) = TL(TL(S));
        continue;
      case COLON_OP:
        if (!(NARGS >= 2)) {
          goto BREAK_MAIN_LOOP;
        }

        // list indexing
        NARGS = NARGS - 2;
        {
          list HOLD = HD(S);

          // hides static M
          word M;

          HD(S) = (list)COLON_OP, E = S, S = HOLD;
          TL(S) = reduce(TL(S));

          if (!(isnum(TL(S)) && (M = getnum(TL(S))) >= LISTBASE)) {
            HOLDARG = (list *)-1;
            goto BREAK_MAIN_LOOP;
          }

          while (M-- > LISTBASE) {
            // clobbers static M
            E = reduce(TL(TL(E)));

            if (!(iscons(E) && HD(E) == (list)COLON_OP)) {
              badexp(cons(E, stonum(M + 1)));
            }
          }

          E = HD(TL(E));
          HOLD = HD(S);
          HD(S) = (list)INDIR, TL(S) = E, S = HOLD;
          REDS = REDS + 1;
          continue;
        }
      case ZF_OP: {
        list HOLD = HD(S);
        NARGS = NARGS - 1;
        HD(S) = E, E = S, S = HOLD;

        if (TL(TL(E)) == NIL) {
          HD(E) = (list)COLON_OP, TL(E) = cons(HD(TL(E)), NIL);
          continue;
        }

        {
          list QUALIFIER = HD(TL(E));
          list REST = TL(TL(E));

          if (iscons(QUALIFIER) && HD(QUALIFIER) == (list)GENERATOR) {
            list SOURCE = reduce(TL(TL(QUALIFIER)));
            list FORMAL = HD(TL(QUALIFIER));
            TL(TL(QUALIFIER)) = SOURCE;

            if (SOURCE == NIL) {
              HD(E) = (list)INDIR, TL(E) = NIL, E = NIL;
            } else if (iscons(SOURCE) && HD(SOURCE) == (list)COLON_OP) {

              HD(E) = cons(
                  (list)INTERLEAVEFN,
                  cons((list)ZF_OP, substitute(HD(TL(SOURCE)), FORMAL, REST)));

              TL(E) =
                  cons((list)ZF_OP,
                       cons(cons((list)GENERATOR, cons(FORMAL, TL(TL(SOURCE)))),
                            REST));

              //                            ) HD!E,TL!E:=APPEND.OP,
              //                                            cons(
              //            cons(ZF.OP,substitute(HD!(TL!SOURCE),FORMAL,REST)),
              //    cons(ZF.OP,cons(cons(GENERATOR,cons(FORMAL,TL!(TL!SOURCE))),REST))
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
              HD(E) = (list)INDIR, TL(E) = NIL, E = NIL;
            } else {
              badexp(cons((list)GUARD, QUALIFIER));
            }
          }

          REDS = REDS + 1;
          continue;
        }
      }
      case DOT_OP:
        if (!(NARGS >= 2)) {
          list A = reduce(HD(TL(S))), B = reduce(TL(TL(S)));

          if (!(isfun(A) && isfun(B))) {
            badexp(cons(E, cons(A, B)));
          }

          goto BREAK_MAIN_LOOP;
        }

        {
          list HOLD = HD(S);
          NARGS = NARGS - 1;
          E = HD(TL(S)), TL(HOLD) = cons(TL(TL(S)), TL(HOLD));
          HD(S) = (list)DOT_OP, S = HOLD;
          REDS = REDS + 1;
          continue;
        }
      case EQ_OP:
      case NE_OP:
        E = equalval(HD(TL(S)), TL(TL(S))) == (E == (list)EQ_OP) ? TRUTH
                                                                 : FALSITY;
        // note - could rewrite for fast exit, here and in
        // other cases where result of reduction is atomic
        {
          list HOLD = HD(S);
          NARGS = NARGS - 1;
          HD(S) = (list)INDIR, TL(S) = E, S = HOLD;
          REDS = REDS + 1;
          continue;
        }
      case ENDOFSTACK:
        badexp((list)SILLYNESS);
      // occurs if we try to evaluate an exp we are already inside
      default:
        break;
      }
      // end of switch

      {
        // strict operators
        list A = NIL, B = NIL;
        bool STRINGS = false;

        // The values of M and N when STRINGS == true
        atom SM, SN;

        if ((word)E >= LENGTH_OP) {
          // monadic
          A = reduce(TL(S));
        } else {
          // DIADIC
          A = reduce(HD(TL(S)));

          // strict in 2nd arg ?
          // yes
          if (E >= (list)GR_OP) {

            B = reduce(E == (list)COMMADOTDOT_OP ? HD(TL(TL(S))) : TL(TL(S)));

            if (isnum(A) && isnum(B)) {

              M = getnum(A), N = getnum(B);

            } else if (E <= (list)LS_OP && iscons(A) && iscons(B) &&
                       HD(A) == (list)QUOTE && (list)QUOTE == HD(B)) {
              // relops

              STRINGS = true, SM = (atom)TL(A), SN = (atom)TL(B);

            } else if (E == (list)DOTDOT_OP && isnum(A) && B == INFINITY) {

              M = getnum(A), N = M;

            } else {

              badexp(cons(E, cons(A, E == (list)COMMADOTDOT_OP
                                         ? cons(B, TL(TL(TL(S))))
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
            badexp(cons(E, cons(A, B)));
          }
          break;
        case OR_OP:
          if (A == TRUTH) {
            E = A;
          } else if (A == FALSITY) {
            E = B;
          } else {
            badexp(cons(E, cons(A, B)));
          }
          break;
        case APPEND_OP:
          if (A == NIL) {
            E = B;
            break;
          }

          if (!(iscons(A) && HD(A) == (list)COLON_OP)) {
            badexp(cons(E, cons(A, B)));
          }

          E = (list)COLON_OP;
          TL(TL(S)) = cons((list)APPEND_OP, cons(TL(TL(A)), B));
          HD(TL(S)) = HD(TL(A));
          REDS = REDS + 1;
          continue;
        case DOTDOT_OP:
          if (M > N) {
            E = NIL;
            break;
          }
          E = (list)COLON_OP;
          TL(TL(S)) = cons((list)DOTDOT_OP, cons(stonum(M + 1), B));
          REDS = REDS + 1;
          continue;
        case COMMADOTDOT_OP: {
          // reduce clobbers M,N
          word M1 = M, N1 = N;
          list C = reduce(TL(TL(TL(S))));
          static word P = 0;

          if (isnum(C)) {
            P = getnum(C);
          } else if (C == INFINITY) {
            P = N1;
          } else {
            badexp(cons(E, cons(A, cons(B, C))));
          }

          if ((N1 - M1) * (P - M1) < 0) {
            E = NIL;
            break;
          }
          E = (list)COLON_OP;
          HD(TL(TL(S))) = stonum(N1 + N1 - M1);
          TL(TL(S)) = cons((list)COMMADOTDOT_OP, cons(B, TL(TL(S))));
          REDS = REDS + 1;
          continue;
        }
        case NOT_OP:
          if (A == TRUTH) {
            E = FALSITY;
          } else if (A == FALSITY) {
            E = TRUTH;
          } else {
            badexp(cons(E, A));
          }
          break;
        case NEG_OP:
          if (!(isnum(A))) {
            badexp(cons(E, A));
          }
          E = stonum(-getnum(A));
          break;
        case LENGTH_OP: {
          word L = 0;

          while (iscons(A) && HD(A) == (list)COLON_OP) {
            A = reduce(TL(TL(A))), L = L + 1;
          }

          if (A == NIL) {
            E = stonum(L);
            break;
          }
          badexp(cons((list)COLON_OP, cons((list)ETC, A)));
        }
        case PLUS_OP: {
          word X = M + N;
          if ((M > 0 && N > 0 && X <= 0) || (M < 0 && N < 0 && X >= 0) ||
              (X == -X && X != 0)) {
            // This checks for -(2**31)
            overflow(cons((list)PLUS_OP, cons(A, B)));
          }
          E = stonum(X);
          break;
        }
        case MINUS_OP: {
          word X = M - N;

          if ((M < 0 && N > 0 && X > 0) || (M > 0 && N < 0 && X < 0) ||
              (X == -X && X != 0)) {
            overflow(cons((list)MINUS_OP, cons(A, B)));
          }

          E = stonum(X);
          break;
        }
        case TIMES_OP: {
          word X = M * N;

          // may not catch all cases
          if ((M > 0 && N > 0 && X <= 0) || (M < 0 && N < 0 && X <= 0) ||
              (M < 0 && N > 0 && X >= 0) || (M > 0 && N < 0 && X >= 0) ||
              (X == -X && X != 0)) {
            overflow(cons((list)TIMES_OP, cons(A, B)));
          }

          E = stonum(X);
          break;
        }
        case DIV_OP:
          if (N == 0) {
            badexp(cons((list)DIV_OP, cons(A, B)));
          }

          E = stonum(M / N);
          break;
        case REM_OP:
          if (N == 0) {
            badexp(cons((list)REM_OP, cons(A, B)));
          }

          E = stonum(M % N);
          break;
        case EXP_OP:
          if (N < 0) {
            badexp(cons((list)EXP_OP, cons(A, B)));
          }

          {
            word P = 1;
            while (!(N == 0)) {
              word X = P * M;

              // may not catch all cases
              if ((M > 0 && P > 0 && X <= 0) || (M < 0 && P < 0 && X <= 0) ||
                  (M < 0 && P > 0 && X >= 0) || (M > 0 && P < 0 && X >= 0) ||
                  (X == -X && X != 0)) {
                overflow(cons((list)EXP_OP, cons(A, B)));
              }

              P = X, N = N - 1;
            }
            E = stonum(P);
            break;
          }
        case GR_OP:
          E = (STRINGS ? alfa_ls(SN, SM) : M > N) ? TRUTH : FALSITY;
          break;
        case GE_OP:
          E = (STRINGS ? alfa_ls(SN, SM) || SN == SM : M >= N) ? TRUTH
                                                               : FALSITY;
          break;
        case LE_OP:
          E = (STRINGS ? alfa_ls(SM, SN) || SM == SN : M <= N) ? TRUTH
                                                               : FALSITY;
          break;
        case LS_OP:
          E = (STRINGS ? alfa_ls(SM, SN) : M < N) ? TRUTH : FALSITY;
          break;
        default:
          bcpl_writes("IMPOSSIBLE OPERATOR IN \"reduce\"\n");
        }
        // end of switch

        {
          list HOLD = HD(S);
          NARGS = NARGS - 1;
          HD(S) = (list)INDIR, TL(S) = E, S = HOLD;
        }
      }
    }
    // end of operators

    REDS = REDS + 1;

  } while (1);
  // end of main loop

BREAK_MAIN_LOOP:
  while (!(S == (list)ENDOFSTACK)) {
    // unreverse reversed pointers

    list HOLD = HD(S);
    HD(S) = E, E = S, S = HOLD;
  }

  if (HOLDARG == (list *)-1) {
    badexp(E);
  }

  // reset ARG stackframe
  ARG = HOLDARG;

  S = HOLD_S;
  return E;
}

static list substitute(list ACTUAL, list FORMAL, list EXP) {

  if (EXP == FORMAL) {
    return ACTUAL;
  } else if (!iscons(EXP) || HD(EXP) == (list)QUOTE || binds(FORMAL, HD(EXP))) {
    return EXP;
  } else {
    list H = substitute(ACTUAL, FORMAL, HD(EXP));
    list T = substitute(ACTUAL, FORMAL, TL(EXP));
    return H == HD(EXP) && T == TL(EXP) ? EXP : cons(H, T);
  }
}

static bool binds(list FORMAL, list X) {
  return iscons(X) && HD(X) == (list)GENERATOR && HD(TL(X)) == FORMAL;
}

// mark elements in the argument stack for preservation by the GC.
// this routine should be called by your bases() function.
void reducer_bases(void (*F)(list *)) {
  list *AP;

  for (AP = ARGSPACE; AP <= ARGP; AP++) {
    F(AP);
  }
}
