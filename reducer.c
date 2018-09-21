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
    int nargs = SPACE / 5;

    ARGSPACE = (list *)malloc(nargs * sizeof(*ARGSPACE));

    if (ARGSPACE == (void *)-1) {
      space_error("Cannot allocate argument stack");
    }

    ARGMAX = ARGSPACE + nargs - 1;
  }

  ARG = ARGSPACE, ARGP = ARG - 1;
}

// sentinel value (impossible pointer)
#define ENDOFSTACK (-4)

list S;

// primitive functions
// renamed to distinguish primitive functions
static void prim_functionp(list e);
static void prim_listp(list e);
static void prim_stringp(list e);
static void prim_numberp(list e);
static void prim_char(list e);
static void prim_size(list e);
static void prim_code(list e);
static void prim_decode(list e);
static void prim_concat(list e);
static void prim_explode(list e);
static void prim_abort(list e);
static void prim_startread(list e);
static void prim_read(list e);
static void prim_writeap(list e);
static void prim_seq(list e);

// local function delarations
static void printfunction(list e);
static bool equalval(list a, list b);
static void badexp(list e);
static void overflow(list e);
static void obey(list eqns, list e);
static bool isfun(list x);

static list reduce(list e);
static list substitute(list actual, list formal, list exp);
static bool binds(list formal, list x);

// DT 2015
static void showch(unsigned char c);

static void R(char *str, void (*fun)(list), word num) {

  // ((atom sym) . (c_call . fun))
  atom a = mkatom(str);
  list eqn = cons((list)a, cons((list)CALL_C, (list)fun));

  if (fun != prim_read) {
    enterscript(a);
  }

  VAL(a) = cons(cons((list)num, NIL), cons(eqn, NIL));
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

// little routine to avoid S having to be global, just because
// it may need fixing up after an interrupt. this routine does that.
void fixup_s(void) {

  // in case interrupt struck while reduce was dissecting a constant
  if (!(S == (list)ENDOFSTACK)) {
    HD(S) = (list)QUOTE;
  }
}

// string case converter
//
// return an upper-case copy of a string.
// copy to static area of 80 chars, the same as BCPL
// also to avoid calling strdup which calls malloc() and
// contaminates the garbage collection done with Boehm GC.
char *scaseconv(char *s) {

  static char t[80 + 1];
  char *p = s, *q = t;

  while (*p) {
    *q++ = caseconv(*p++);
  }

  *q = '\0';
  return t;
}

void initstats() { REDS = 0; }

void outstats() { fprintf(bcpl_OUTPUT, "reductions = %" W "\n", REDS); }

// the possible values of a reduced expression are:
//  VAL      := CONST | FUNCTION | LIST
//  CONST    := NUM | cons(QUOTE, ATOM)
//  LIST     := NIL | cons(COLON_OP, cons(EXP, EXP))
//  FUNCTION := NAME | cons(E1, E2)

void printval(list e, bool format) {

  e = reduce(e);

  if (e == NIL) {

    if (format) {
      bcpl_writes("[]");
    }

  } else if (isnum(e)) {

    bcpl_writen(getnum(e));

  } else if (iscons(e)) {

    list h = HD(e);

    if (h == (list)QUOTE) {

      printatom((atom)TL(e), format);

    } else if (h == (list)COLON_OP) {

      if (format) {
        wrch('[');
      }

      e = TL(e);
      do {
        printval(HD(e), format);
        e = TL(e);
        e = reduce(e);

        if (!(iscons(e))) {
          break;
        }

        if (HD(e) == (list)COLON_OP) {

          if (format) {
            wrch(',');
          }

        } else {
          break;
        }

        e = TL(e);

      } while (1);

      if (e == NIL) {

        if (format) {
          wrch(']');
        }

      } else {

        badexp(cons((list)COLON_OP, cons((list)ETC, e)));
      }

    } else if (iscons(h) && HD(h) == (list)WRITEFN) {

      TL(h) = reduce(TL(h));

      if (!(iscons(TL(h)) && HD(TL(h)) == (list)QUOTE)) {
        badexp(e);
      }

      {
        char *f = NAME((atom)TL(TL(h)));
        FILE *out = findchannel(f);
        FILE *hold = bcpl_OUTPUT;

        if (!(out != NULL)) {
          badexp(cons((list)BADFILE, TL(h)));
        }

        bcpl_OUTPUT_fp = (out);
        printval(TL(e), format);
        bcpl_OUTPUT_fp = (hold);
      }

    } else {

      // a partial application or composition
      printfunction(e);
    }
  } else {

    // only possibility here should be name of function
    printfunction(e);
  }
}

// prints atom
void printatom(atom a, bool format) {

  if (format) {

    // DT 2015
    wrch('"');

    for (int i = 0; i < LEN(a); i++) {
      showch(NAME(a)[i]);
    }

    wrch('"');

  } else {

    for (int i = 0; i < LEN(a); i++) {
      wrch(NAME(a)[i]);
    }
  }
}

static void showch(unsigned char c) {

  switch (c) {
  case '\a':
    wrch('\\');
    wrch('a');
    break;
  case '\b':
    wrch('\\');
    wrch('b');
    break;
  case '\f':
    wrch('\\');
    wrch('f');
    break;
  case '\n':
    wrch('\\');
    wrch('n');
    break;
  case '\r':
    wrch('\\');
    wrch('r');
    break;
  case '\t':
    wrch('\\');
    wrch('t');
    break;
  case '\v':
    wrch('\\');
    wrch('v');
    break;
  case '\\':
    wrch('\\');
    wrch('\\');
    break;
  case '\'':
    wrch('\\');
    wrch('\'');
    break;
  case '\"':
    wrch('\\');
    wrch('\"');
    break;
  default:
    if (iscntrl(c) || c >= 127) {
      printf("\\%03u", c);
    } else {
      wrch(c);
    }
  }
}

static void printfunction(list e) {

  wrch('<');
  printexp(e, 0);
  wrch('>');
}

// unpredictable results if a, b both functions
static bool equalval(list a, list b) {

  do {
    a = reduce(a);
    b = reduce(b);

    if (a == b) {
      return true;
    }

    if (isnum(a) && isnum(b)) {
      return getnum(a) == getnum(b);
    }

    if (!(iscons(a) && iscons(b) && (HD(a) == HD(b)))) {
      return false;
    }

    if (HD(a) == (list)QUOTE || HD(a) == (list)QUOTE_OP) {
      return TL(a) == TL(b);
    }

    if (!(HD(a) == (list)COLON_OP)) {
      // UH ?
      return false;
    }

    a = TL(a), b = TL(b);
    if (!(equalval(HD(a), HD(b)))) {
      return false;
    }

    a = TL(a), b = TL(b);
  } while (1);
}

// called for all evaluation errors
static void badexp(list e) {

  wrch = TRUEWRCH;
  closechannels();
  bcpl_writes("\n**undefined expression**\n  ");
  printexp(e, 0);

  // could insert more detailed diagnostics here,
  // depending on nature of HD(e), for example:
  if (iscons(e) && (HD(e) == (list)COLON_OP || HD(e) == (list)APPEND_OP)) {
    bcpl_writes("\n  (non-list encountered where list expected)");
  }

  bcpl_writes("\n**evaluation abandoned**\n");
  escapetonextcommand();
}

// integer overflow handler
static void overflow(list e) {

  wrch = TRUEWRCH;
  closechannels();
  bcpl_writes("\n**integer overflow**\n  ");
  printexp(e, 0);
  bcpl_writes("\n**evaluation abandoned**\n");
  escapetonextcommand();
}

// a kludge
list buildexp(list code) {

  // a bogus piece of graph
  list e = cons(NIL, NIL);
  obey(cons(cons(NIL, code), NIL), e);

  // reset ARG stack
  ARGP = ARG - 1;
  return e;
}

// transform a piece of graph, e, in accordance with eqns
// - actual params are found in *ARG ... *ARGP
// (warning - has side effect of raising ARGP)
static void obey(list eqns, list e) {

  // eqns loop
  while (!(eqns == NIL)) {

    list code = TL(HD(eqns));
    list *holdarg = ARGP;

    // decode loop
    do {
      list h = HD(code);
      code = TL(code);

      // first, check the only cases that increment ARGP
      switch ((word)h) {
      case LOAD_C:
      case LOADARG_C:
      case FORMLIST_C:
        ARGP = ARGP + 1;
        if (ARGP > ARGMAX) {
          space_error("Arg stack overflow");
        }
      }

      switch ((word)h) {
      case LOAD_C:
        // ARGP=ARGP+1;
        *ARGP = HD(code);
        code = TL(code);
        break;
      case LOADARG_C:
        // ARGP=ARGP+1;
        if (ARGP > ARGMAX) {
          space_error("Arg stack overflow");
        }
        *ARGP = ARG[(word)(HD(code))];
        code = TL(code);
        break;
      case APPLYINFIX_C:
        *ARGP = cons(*(ARGP - 1), *ARGP);
        *(ARGP - 1) = HD(code);
        code = TL(code);
      case APPLY_C:
        ARGP = ARGP - 1;
        if (HD(code) == (list)STOP_C) {
          HD(e) = *ARGP, TL(e) = *(ARGP + 1);
          return;
        }
        *ARGP = cons(*ARGP, *(ARGP + 1));
        break;
      case CONTINUE_INFIX_C:
        *(ARGP - 1) = cons(HD(code), cons(*(ARGP - 1), *ARGP));
        code = TL(code);
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
        for (word i = 1; i <= (word)HD(code); i++) {
          ARGP = ARGP - 1;
          *ARGP = cons((list)COLON_OP, cons(*ARGP, *(ARGP + 1)));
        }
        code = TL(code);
        break;
      case FORMZF_C: {
        list x = cons(*(ARGP - (word)HD(code)), NIL);
        list *p;
        for (p = ARGP; p >= ARGP - (word)HD(code) + 1; p = p - 1) {
          x = cons(*p, x);
        }

        ARGP = ARGP - (word)HD(code);
        *ARGP = cons((list)ZF_OP, x);
        code = TL(code);
        break;
      }
      case CONT_GENERATOR_C:
        for (word i = 1; i <= (word)HD(code); i++) {
          *(ARGP - i) = cons((list)GENERATOR, cons(*(ARGP - i), TL(TL(*ARGP))));
        }

        code = TL(code);
        break;
      case MATCH_C: {
        word i = (word)HD(code);
        code = TL(code);

        if (!(equalval(ARG[i], HD(code)))) {
          goto BREAK_DECODE_LOOP;
        }

        code = TL(code);
        break;
      }
      case MATCHARG_C: {
        word i = (word)HD(code);
        code = TL(code);

        if (!(equalval(ARG[i], ARG[(word)(HD(code))]))) {
          goto BREAK_DECODE_LOOP;
        }

        code = TL(code);
        break;
      }
      case MATCHPAIR_C: {
        list *p = ARG + (word)(HD(code));
        *p = reduce(*p);

        if (!(iscons(*p) && HD(*p) == (list)COLON_OP)) {
          goto BREAK_DECODE_LOOP;
        }

        ARGP = ARGP + 2;
        *(ARGP - 1) = HD(TL(*p)), *ARGP = TL(TL(*p));
        code = TL(code);
        break;
      }
      case LINENO_C:
        // no action
        code = TL(code);
        break;
      case STOP_C:
        HD(e) = (list)INDIR, TL(e) = *ARGP;
        return;
      case CALL_C:
        (*(void (*)())code)(e);
        return;
      default:
        fprintf(bcpl_OUTPUT, "IMPOSSIBLE INSTRUCTION <%p> IN \"obey\"\n", h);
      }
    } while (1);
    // end of decode loop

  BREAK_DECODE_LOOP:
    eqns = TL(eqns);
    ARGP = holdarg;
  }

  // end of eqns loop
  badexp(e);
}

static void prim_functionp(list e) {
  *ARG = reduce(*ARG);
  HD(e) = (list)INDIR;
  TL(e) = isfun(*ARG) ? TRUTH : FALSITY;
}

static void prim_listp(list e) {
  *ARG = reduce(*ARG);
  HD(e) = (list)INDIR;
  TL(e) = (*ARG == NIL || (iscons(*ARG) && HD(*ARG) == (list)COLON_OP))
              ? TRUTH
              : FALSITY;
}

static void prim_stringp(list e) {
  *ARG = reduce(*ARG);
  HD(e) = (list)INDIR,
  TL(e) = iscons(*ARG) && HD(*ARG) == (list)QUOTE ? TRUTH : FALSITY;
}

static void prim_numberp(list e) {
  *ARG = reduce(*ARG);
  HD(e) = (list)INDIR, TL(e) = isnum(*ARG) ? TRUTH : FALSITY;
}

static bool isfun(list x) {
  return isatom(x) || (iscons(x) && QUOTE != HD(x) && HD(x) != (list)COLON_OP);
}

static void prim_char(list e) {
  *ARG = reduce(*ARG);
  HD(e) = (list)INDIR;
  TL(e) = iscons(*ARG) && HD(*ARG) == (list)QUOTE && LEN((atom)TL(*ARG)) == 1
              ? TRUTH
              : FALSITY;
}

static word count;
static void countch(word ch) { count = count + 1; }

static void prim_size(list e) {

  count = 0;
  wrch = countch;
  printval(*ARG, false);
  wrch = TRUEWRCH;
  HD(e) = (list)INDIR, TL(e) = stonum(count);
}

// converts a character to its ascii number
static void prim_code(list e) {

  *ARG = reduce(*ARG);

  if (!(iscons(*ARG) && HD(*ARG) == QUOTE)) {
    badexp(e);
  }

  {
    atom a = (atom)TL(*ARG);

    if (!(LEN(a) == 1)) {
      badexp(e);
    }

    HD(e) = (list)INDIR;
    TL(e) = stonum((word)NAME(a)[0] & 0xff);
  }
}

static void prim_decode(list e) {
  *ARG = reduce(*ARG);

  if (!(isnum(*ARG) && 0 <= (word)TL(*ARG) && (word)TL(*ARG) <= 255)) {
    badexp(e);
  }

  bufch((word)TL(*ARG));
  HD(e) = (list)INDIR, TL(e) = cons((list)QUOTE, (list)packbuffer());
}

static void prim_concat(list e) {

  *ARG = reduce(*ARG);

  {
    list a = *ARG;

    while (iscons(a) && HD(a) == (list)COLON_OP) {
      list c = reduce(HD(TL(a)));

      if (!(iscons(c) && HD(c) == (list)QUOTE)) {
        badexp(e);
      }

      HD(TL(a)) = c;
      TL(TL(a)) = reduce(TL(TL(a)));
      a = TL(TL(a));
    }

    if (!(a == NIL)) {
      badexp(e);
    }

    a = *ARG;
    while (!(a == NIL)) {
      atom n = (atom)TL(HD(TL(a)));

      for (int i = 0; i < LEN(n); i++) {
        bufch(NAME(n)[i]);
      }

      a = TL(TL(a));
    }
    a = (list)packbuffer();
    HD(e) = (list)INDIR,
    TL(e) = a == TL(TRUTH) ? TRUTH
                           : a == TL(FALSITY) ? FALSITY : cons((list)QUOTE, a);
  }
}

static void prim_explode(list e) {

  *ARG = reduce(*ARG);

  if (!(iscons(*ARG) && HD(*ARG) == (list)QUOTE)) {
    badexp(e);
  }

  {
    atom a = (atom)TL(*ARG);
    list x = NIL;

    for (int i = LEN(a); i > 0; i--) {
      bufch(NAME(a)[i - 1]);
      x = cons((list)COLON_OP, cons(cons((list)QUOTE, (list)packbuffer()), x));
    }

    HD(e) = (list)INDIR, TL(e) = x;
  }
}

static void prim_abort(list e) {

  FILE *hold = bcpl_OUTPUT;
  bcpl_OUTPUT_fp = (stderr);

  bcpl_writes("\nprogram error: ");

  printval(TL(e), false);

  wrch('\n');

  bcpl_OUTPUT_fp = (hold);
  ABORTED = true;
  raise(SIGINT);
}

static void prim_startread(list e) {

  *ARG = reduce(*ARG);

  if (!(iscons(*ARG) && HD(*ARG) == (list)QUOTE)) {
    badexp(e);
  }

  {
    FILE *in = bcpl_findinput(NAME((atom)TL(*ARG)));

    if (!(in != NULL)) {
      badexp(cons((list)BADFILE, *ARG));
    }

    HD(e) = (list)READFN, TL(e) = (list)in;
  }
}

static void prim_read(list e) {

  FILE *in = (FILE *)TL(e);
  bcpl_INPUT_fp = (in);
  HD(e) = (list)INDIR, TL(e) = cons((list)READFN, TL(e));

  {
    list *x = &(TL(e));
    word ch = rdch();

    // read one character
    if (ch != EOF) {
      char c = ch;
      *x = cons((list)COLON_OP,
                cons(cons((list)QUOTE, (list)mkatomn(&c, 1)), *x));
      x = &(TL(TL(*x)));
    }

    if (ferror(in)) {
      fprintf(bcpl_OUTPUT, "\n**File read error**\n");
      escapetonextcommand();
    }

    if (ch == EOF) {
      if (bcpl_INPUT_fp != stdin) {
        fclose(bcpl_INPUT_fp);
      };
      *x = NIL;
    }
    bcpl_INPUT_fp = (stdin);
  }
}

// called if write is applied to >2 ARGS
static void prim_writeap(list e) { badexp(e); }

// seq a b evaluates a then returns b, added DT 2015
static void prim_seq(list e) {

  reduce(TL(HD(e)));
  HD(e) = (list)INDIR;
}

// possibilities for leftmost field of a graph are:
// HEAD:= NAME | NUM | NIL | OPERATOR

static list reduce(list e) {

  static word m = 0;
  static word n = 0;
  list hold_s = S;
  word nargs = 0;
  list *holdarg = ARG;

  // if ( &e>STACKLIMIT ) space_error("Arg stack overflow");
  // if ( ARGP>ARGMAX ) space_error("Arg stack overflow");

  S = (list)ENDOFSTACK;
  ARG = ARGP + 1;

  // main loop
  do {

    // find head, reversing pointers en route
    while (iscons(e)) {
      list hold = HD(e);
      nargs = nargs + 1;
      HD(e) = S, S = e, e = hold;
    }

    if (isnum(e) || e == NIL) {
      // unless nargs == 0 do holdarg=(list *)-1;  // flags an error
      goto BREAK_MAIN_LOOP;
    }

    // user defined name
    if (isatom(e)) {

      if (VAL((atom)e) == NIL || TL(VAL((atom)e)) == NIL) {

        badexp(e);

      } else {
        // undefined name

        // variable

        if (HD(HD(VAL((atom)e))) == 0) {
          list eqn = HD(TL(VAL((atom)e)));

          // memo not set
          if (HD(eqn) == 0) {
            HD(eqn) = buildexp(TL(eqn));
            MEMORIES = cons(e, MEMORIES);
          }

          e = HD(eqn);
        } else {

          // can we get cyclic expressions?

          // function

          // hides the static n
          word n = (word)HD(HD(VAL((atom)e)));

          if (n > nargs) {
            // not enough ARGS
            goto BREAK_MAIN_LOOP;
          }

          {
            list eqns = TL(VAL((atom)e));

            for (int i = 0; i <= n - 1; i++) {

              // move back up graph,
              list hold = HD(S);

              // stacking ARGS en route
              ARGP = ARGP + 1;

              if (ARGP > ARGMAX) {
                space_error("Arg stack overflow");
              }

              *ARGP = TL(S);
              HD(S) = e, e = S, S = hold;
            }

            nargs = nargs - n;

            // e now holds a piece of graph to be transformed
            // !ARG ... !ARGP  hold the parameters
            obey(eqns, e);

            // reset ARG stack
            ARGP = ARG - 1;
          }
        }
      }

    } else {
      // operators
      switch ((word)e) {
      case QUOTE:
        if (!(nargs == 1)) {
          holdarg = (list *)-1;
        }
        goto BREAK_MAIN_LOOP;
      case INDIR: {
        list hold = HD(S);
        nargs = nargs - 1;
        e = TL(S), HD(S) = (list)INDIR, S = hold;
        continue;
      }
      case QUOTE_OP:
        if (!(nargs >= 3)) {
          goto BREAK_MAIN_LOOP;
        }

        {
          list op = TL(S);
          list hold = HD(S);
          nargs = nargs - 2;
          HD(S) = e, e = S, S = hold;
          hold = HD(S);
          HD(S) = e, e = S, S = hold;
          TL(S) = cons(TL(e), TL(S)), e = op;
          continue;
        }
      case LISTDIFF_OP:
        e = cons((list)LISTDIFF, HD(TL(S)));
        TL(S) = TL(TL(S));
        continue;
      case COLON_OP:
        if (!(nargs >= 2)) {
          goto BREAK_MAIN_LOOP;
        }

        // list indexing
        nargs = nargs - 2;
        {
          list hold = HD(S);

          // hides static m
          word m;

          HD(S) = (list)COLON_OP, e = S, S = hold;
          TL(S) = reduce(TL(S));

          if (!(isnum(TL(S)) && (m = getnum(TL(S))) >= LISTBASE)) {
            holdarg = (list *)-1;
            goto BREAK_MAIN_LOOP;
          }

          while (m-- > LISTBASE) {
            // clobbers static m
            e = reduce(TL(TL(e)));

            if (!(iscons(e) && HD(e) == (list)COLON_OP)) {
              badexp(cons(e, stonum(m + 1)));
            }
          }

          e = HD(TL(e));
          hold = HD(S);
          HD(S) = (list)INDIR, TL(S) = e, S = hold;
          REDS = REDS + 1;
          continue;
        }
      case ZF_OP: {
        list hold = HD(S);
        nargs = nargs - 1;
        HD(S) = e, e = S, S = hold;

        if (TL(TL(e)) == NIL) {
          HD(e) = (list)COLON_OP;
          TL(e) = cons(HD(TL(e)), NIL);
          continue;
        }

        {
          list qualifier = HD(TL(e));
          list rest = TL(TL(e));

          if (iscons(qualifier) && HD(qualifier) == (list)GENERATOR) {
            list source = reduce(TL(TL(qualifier)));
            list formal = HD(TL(qualifier));
            TL(TL(qualifier)) = source;

            if (source == NIL) {
              HD(e) = (list)INDIR, TL(e) = NIL, e = NIL;
            } else if (iscons(source) && HD(source) == (list)COLON_OP) {

              HD(e) = cons(
                  (list)INTERLEAVEFN,
                  cons((list)ZF_OP, substitute(HD(TL(source)), formal, rest)));

              TL(e) =
                  cons((list)ZF_OP,
                       cons(cons((list)GENERATOR, cons(formal, TL(TL(source)))),
                            rest));

              //                            ) HD!e,TL!e:=APPEND.OP,
              //                                            cons(
              //            cons(ZF.OP,substitute(HD!(TL!source),formal,rest)),
              //    cons(ZF.OP,cons(cons(GENERATOR,cons(formal,TL!(TL!source))),rest))
              //                                                )
            } else {
              badexp(e);
            }

          } else {

            // qualifier is guard
            qualifier = reduce(qualifier);
            HD(TL(e)) = qualifier;

            if (qualifier == TRUTH) {
              TL(e) = rest;
            } else if (qualifier == FALSITY) {
              HD(e) = (list)INDIR, TL(e) = NIL, e = NIL;
            } else {
              badexp(cons((list)GUARD, qualifier));
            }
          }

          REDS = REDS + 1;
          continue;
        }
      }
      case DOT_OP:
        if (!(nargs >= 2)) {
          list a = reduce(HD(TL(S))), b = reduce(TL(TL(S)));

          if (!(isfun(a) && isfun(b))) {
            badexp(cons(e, cons(a, b)));
          }

          goto BREAK_MAIN_LOOP;
        }

        {
          list hold = HD(S);
          nargs = nargs - 1;
          e = HD(TL(S)), TL(hold) = cons(TL(TL(S)), TL(hold));
          HD(S) = (list)DOT_OP, S = hold;
          REDS = REDS + 1;
          continue;
        }
      case EQ_OP:
      case NE_OP:
        e = equalval(HD(TL(S)), TL(TL(S))) == (e == (list)EQ_OP) ? TRUTH
                                                                 : FALSITY;
        // note - could rewrite for fast exit, here and in
        // other cases where result of reduction is atomic
        {
          list hold = HD(S);
          nargs = nargs - 1;
          HD(S) = (list)INDIR, TL(S) = e, S = hold;
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
        list a = NIL, b = NIL;
        bool strings = false;

        // The values of m and n when strings == true
        atom sm, sn;

        if ((word)e >= LENGTH_OP) {
          // monadic
          a = reduce(TL(S));
        } else {
          // diadic
          a = reduce(HD(TL(S)));

          // strict in 2nd arg ?
          // yes
          if (e >= (list)GR_OP) {

            b = reduce(e == (list)COMMADOTDOT_OP ? HD(TL(TL(S))) : TL(TL(S)));

            if (isnum(a) && isnum(b)) {

              m = getnum(a), n = getnum(b);

            } else if (e <= (list)LS_OP && iscons(a) && iscons(b) &&
                       HD(a) == (list)QUOTE && (list)QUOTE == HD(b)) {
              // relops

              strings = true, sm = (atom)TL(a), sn = (atom)TL(b);

            } else if (e == (list)DOTDOT_OP && isnum(a) && b == INFINITY) {

              m = getnum(a), n = m;

            } else {

              badexp(cons(e, cons(a, e == (list)COMMADOTDOT_OP
                                         ? cons(b, TL(TL(TL(S))))
                                         : b)));
            }
          }
          // no
          else {
            b = TL(TL(S));
          }
        }
        switch ((word)e) {
        case AND_OP:
          if (a == FALSITY) {
            e = a;
          } else if (a == TRUTH) {
            e = b;
          } else {
            badexp(cons(e, cons(a, b)));
          }
          break;
        case OR_OP:
          if (a == TRUTH) {
            e = a;
          } else if (a == FALSITY) {
            e = b;
          } else {
            badexp(cons(e, cons(a, b)));
          }
          break;
        case APPEND_OP:
          if (a == NIL) {
            e = b;
            break;
          }

          if (!(iscons(a) && HD(a) == (list)COLON_OP)) {
            badexp(cons(e, cons(a, b)));
          }

          e = (list)COLON_OP;
          TL(TL(S)) = cons((list)APPEND_OP, cons(TL(TL(a)), b));
          HD(TL(S)) = HD(TL(a));
          REDS = REDS + 1;
          continue;
        case DOTDOT_OP:
          if (m > n) {
            e = NIL;
            break;
          }
          e = (list)COLON_OP;
          TL(TL(S)) = cons((list)DOTDOT_OP, cons(stonum(m + 1), b));
          REDS = REDS + 1;
          continue;
        case COMMADOTDOT_OP: {
          // reduce clobbers m,n
          word m1 = m, n1 = n;
          list c = reduce(TL(TL(TL(S))));
          static word p = 0;

          if (isnum(c)) {
            p = getnum(c);
          } else if (c == INFINITY) {
            p = n1;
          } else {
            badexp(cons(e, cons(a, cons(b, c))));
          }

          if ((n1 - m1) * (p - m1) < 0) {
            e = NIL;
            break;
          }
          e = (list)COLON_OP;
          HD(TL(TL(S))) = stonum(n1 + n1 - m1);
          TL(TL(S)) = cons((list)COMMADOTDOT_OP, cons(b, TL(TL(S))));
          REDS = REDS + 1;
          continue;
        }
        case NOT_OP:
          if (a == TRUTH) {
            e = FALSITY;
          } else if (a == FALSITY) {
            e = TRUTH;
          } else {
            badexp(cons(e, a));
          }
          break;
        case NEG_OP:
          if (!(isnum(a))) {
            badexp(cons(e, a));
          }
          e = stonum(-getnum(a));
          break;
        case LENGTH_OP: {
          word l = 0;

          while (iscons(a) && HD(a) == (list)COLON_OP) {
            a = reduce(TL(TL(a))), l = l + 1;
          }

          if (a == NIL) {
            e = stonum(l);
            break;
          }
          badexp(cons((list)COLON_OP, cons((list)ETC, a)));
        }
        case PLUS_OP: {
          word x = m + n;
          if ((m > 0 && n > 0 && x <= 0) || (m < 0 && n < 0 && x >= 0) ||
              (x == -x && x != 0)) {
            // this checks for -(2**31)
            overflow(cons((list)PLUS_OP, cons(a, b)));
          }
          e = stonum(x);
          break;
        }
        case MINUS_OP: {
          word x = m - n;

          if ((m < 0 && n > 0 && x > 0) || (m > 0 && n < 0 && x < 0) ||
              (x == -x && x != 0)) {
            overflow(cons((list)MINUS_OP, cons(a, b)));
          }

          e = stonum(x);
          break;
        }
        case TIMES_OP: {
          word x = m * n;

          // may not catch all cases
          if ((m > 0 && n > 0 && x <= 0) || (m < 0 && n < 0 && x <= 0) ||
              (m < 0 && n > 0 && x >= 0) || (m > 0 && n < 0 && x >= 0) ||
              (x == -x && x != 0)) {
            overflow(cons((list)TIMES_OP, cons(a, b)));
          }

          e = stonum(x);
          break;
        }
        case DIV_OP:
          if (n == 0) {
            badexp(cons((list)DIV_OP, cons(a, b)));
          }

          e = stonum(m / n);
          break;
        case REM_OP:
          if (n == 0) {
            badexp(cons((list)REM_OP, cons(a, b)));
          }

          e = stonum(m % n);
          break;
        case EXP_OP:
          if (n < 0) {
            badexp(cons((list)EXP_OP, cons(a, b)));
          }

          {
            word p = 1;
            while (!(n == 0)) {
              word x = p * m;

              // may not catch all cases
              if ((m > 0 && p > 0 && x <= 0) || (m < 0 && p < 0 && x <= 0) ||
                  (m < 0 && p > 0 && x >= 0) || (m > 0 && p < 0 && x >= 0) ||
                  (x == -x && x != 0)) {
                overflow(cons((list)EXP_OP, cons(a, b)));
              }

              p = x, n = n - 1;
            }
            e = stonum(p);
            break;
          }
        case GR_OP:
          e = (strings ? alfa_ls(sn, sm) : m > n) ? TRUTH : FALSITY;
          break;
        case GE_OP:
          e = (strings ? alfa_ls(sn, sm) || sn == sm : m >= n) ? TRUTH
                                                               : FALSITY;
          break;
        case LE_OP:
          e = (strings ? alfa_ls(sm, sn) || sm == sn : m <= n) ? TRUTH
                                                               : FALSITY;
          break;
        case LS_OP:
          e = (strings ? alfa_ls(sm, sn) : m < n) ? TRUTH : FALSITY;
          break;
        default:
          bcpl_writes("IMPOSSIBLE OPERATOR IN \"reduce\"\n");
        }
        // end of switch

        {
          list hold = HD(S);
          nargs = nargs - 1;
          HD(S) = (list)INDIR, TL(S) = e, S = hold;
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

    list hold = HD(S);
    HD(S) = e, e = S, S = hold;
  }

  if (holdarg == (list *)-1) {
    badexp(e);
  }

  // reset ARG stackframe
  ARG = holdarg;

  S = hold_s;
  return e;
}

static list substitute(list actual, list formal, list exp) {

  if (exp == formal) {
    return actual;
  } else if (!iscons(exp) || HD(exp) == (list)QUOTE || binds(formal, HD(exp))) {
    return exp;
  } else {
    list h = substitute(actual, formal, HD(exp));
    list t = substitute(actual, formal, TL(exp));
    return h == HD(exp) && t == TL(exp) ? exp : cons(h, t);
  }
}

static bool binds(list formal, list x) {
  return iscons(x) && HD(x) == (list)GENERATOR && HD(TL(x)) == formal;
}

// mark elements in the argument stack for preservation by the GC.
// this routine should be called by your bases() function.
void reducer_bases(void (*f)(list *)) {
  list *ap;

  for (ap = ARGSPACE; ap <= ARGP; ap++) {
    f(ap);
  }
}
