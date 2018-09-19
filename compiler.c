// KRC compiler

// Note: What is now '{' here was '{ ' in the BCPL.

#include "common.h"
#include "iolib.h"
#include "listhdr.h"
#include "comphdr.h"

//----------------------------------------------------------------------
// The KRC system is Copyright (c) D. A. Turner 1981
// All  rights reserved.  It is distributed as free software under the
// terms in the file "COPYING", which is included in the distribution.
//----------------------------------------------------------------------

// local function declarations
static BOOL ISOP(LIST X);
static BOOL ISINFIX(LIST X);
static BOOL ISRELOP(LIST X);
static WORD DIPRIO(OPERATOR OP);
static OPERATOR MKINFIX(TOKEN T);
static void PRINTZF_EXP(LIST X);
static BOOL ISLISTEXP(LIST E);
static BOOL ISRELATION(LIST X);
static BOOL ISRELATION_BEGINNING(LIST A, LIST X);
static WORD LEFTPREC(OPERATOR OP);
static WORD RIGHTPREC(OPERATOR OP);
static BOOL ROTATE(LIST E);
static BOOL PARMY(LIST X);
static LIST REST(LIST C);
static LIST SUBTRACT(LIST X, LIST Y);
static void EXPR(WORD N);
static BOOL STARTFORMAL(TOKEN T);
static BOOL STARTSIMPLE(TOKEN T);
static void COMBN(void);
static void SIMPLE(void);
static void COMPILENAME(ATOM N);
static WORD QUALIFIER(void);
static void PERFORM_ALPHA_CONVERSIONS();
static BOOL ISGENERATOR(LIST T);
static void ALPHA_CONVERT(LIST VAR, LIST P);
static LIST SKIPCHUNK(LIST P);
static void CONV1(LIST T, LIST VAR, LIST VAR1);
static LIST FORMAL(void);
static LIST INTERNALISE(LIST VAL);
static LIST PATTERN(void);
static void COMPILELHS(LIST LHS, WORD NARGS);
static void COMPILEFORMAL(LIST X, WORD I);
static void PLANT0(INSTRUCTION OP);
static void PLANT1(INSTRUCTION OP, LIST A);
static void PLANT2(INSTRUCTION OP, LIST A, LIST B);
static LIST COLLECTCODE(void);

// global variables
void (*TRUEWRCH)(WORD C) = bcpl_WRCH;
LIST LASTLHS = NIL;
LIST TRUTH, FALSITY, INFINITY;

// SETUP_INFIXES() - interesting elements start at [1]
// The indices correspond to the OPERATOR values in comphdr.h
static TOKEN INFIXNAMEVEC[] = {
    (TOKEN)0,   (TOKEN)':', PLUSPLUS_SY, DASHDASH_SY, (TOKEN)'|',
    (TOKEN)'&', (TOKEN)'>', GE_SY,       NE_SY,
    EQ_SY, // WAS (TOKEN) '=', CHANGED DT MAY 2015
    LE_SY,      (TOKEN)'<', (TOKEN)'+',  (TOKEN)'-',  (TOKEN)'*',
    (TOKEN)'/', (TOKEN)'%', STARSTAR_SY, (TOKEN)'.',
};
static WORD INFIXPRIOVEC[] = {0, 0, 0, 0, 1, 2, 3, 3, 3, 3,
                              3, 3, 4, 4, 5, 5, 5, 6, 6};

// bases for garbage collection
// store for opcodes and ther params, which may be operators,
// various CONStructs or the addresses of C functions.
static LIST CODEV = NIL;

// appears to be a store for formal parameters
static LIST ENV[100];

static WORD ENVP;

void INIT_CODEV() {
  ENVP = -1;
  CODEV = NIL;
}

static BOOL ISOP(LIST X) {
  return X == (LIST)ALPHA || X == (LIST)INDIR ||
         ((LIST)QUOTE <= X && X <= (LIST)QUOTE_OP);
}

static BOOL ISINFIX(LIST X) { return (LIST)COLON_OP <= X && X <= (LIST)DOT_OP; }

static BOOL ISRELOP(LIST X) { return (LIST)GR_OP <= X && X <= (LIST)LS_OP; }

// return the priority of an operator from its index in INFIX*
static WORD DIPRIO(OPERATOR OP) { return OP == -1 ? -1 : INFIXPRIOVEC[OP]; }

// takes a token , returns an operator
// else -1 if t not the name of an infix
static OPERATOR MKINFIX(TOKEN T) {
  WORD I = 1;
  if (T == (TOKEN)'=') {
    // legacy, accept "=" for "=="
    return EQ_OP;
  }

  while (!(I > DOT_OP || INFIXNAMEVEC[I] == T)) {
    I = I + 1;
  }

  if (I > DOT_OP) {
    return -1;
  }

  return I;
}

// n is the priority level
void PRINTEXP(LIST E, WORD N) {
  if (E == NIL) {
    bcpl_WRITES("[]");
  } else if (ISATOM(E)) {
    bcpl_WRITES(PRINTNAME((ATOM)E));
  } else if (ISNUM(E)) {
    WORD X = GETNUM(E);
    if (X < 0 && N > 5) {
      (*_WRCH)('(');
      bcpl_WRITEN(X);
      (*_WRCH)(')');
    } else {
      bcpl_WRITEN(X);
    }
  } else {
    if (!(ISCONS(E))) {
      if (E == (LIST)NOT_OP) {
        bcpl_WRITES("'\\'");
      } else if (E == (LIST)LENGTH_OP) {
        bcpl_WRITES("'#'");
      } else {
        fprintf(bcpl_OUTPUT, "<internal value:%p>", E);
      }
      return;
    }
    {
      // maybe could be OPERATOR
      LIST OP = HD(E);
      if (!ISOP(OP) && N <= 7) {
        PRINTEXP(OP, 7);
        (*_WRCH)(' ');
        PRINTEXP(TL(E), 8);
      } else if (OP == (LIST)QUOTE) {
        PRINTATOM((ATOM)TL(E), TRUE);
      } else if (OP == (LIST)INDIR || OP == (LIST)ALPHA) {
        PRINTEXP(TL(E), N);
      } else if (OP == (LIST)DOTDOT_OP || OP == (LIST)COMMADOTDOT_OP) {
        (*_WRCH)('[');
        E = TL(E);
        PRINTEXP(HD(E), 0);
        if (OP == (LIST)COMMADOTDOT_OP) {
          (*_WRCH)(',');
          E = TL(E);
          PRINTEXP(HD(E), 0);
        }
        bcpl_WRITES("..");
        if (!(TL(E) == INFINITY)) {
          PRINTEXP(TL(E), 0);
        }
        (*_WRCH)(']');
      } else if (OP == (LIST)ZF_OP) {
        (*_WRCH)('{');
        PRINTZF_EXP(TL(E));
        (*_WRCH)('}');
      } else if (OP == (LIST)NOT_OP && N <= 3) {
        (*_WRCH)('\\');
        PRINTEXP(TL(E), 3);
      } else if (OP == (LIST)NEG_OP && N <= 5) {
        (*_WRCH)('-');
        PRINTEXP(TL(E), 5);
      } else if (OP == (LIST)LENGTH_OP && N <= 7) {
        (*_WRCH)('#');
        PRINTEXP(TL(E), 7);
      } else if (OP == (LIST)QUOTE_OP) {
        (*_WRCH)('\'');
        if (TL(E) == (LIST)LENGTH_OP) {
          (*_WRCH)('#');
        } else if (TL(E) == (LIST)NOT_OP) {
          (*_WRCH)('\\');
        } else {
          writetoken(INFIXNAMEVEC[(WORD)TL(E)]);
        }
        (*_WRCH)('\'');
      } else if (ISLISTEXP(E)) {
        (*_WRCH)('[');
        while (!(E == NIL)) {
          PRINTEXP(HD(TL(E)), 0);
          if (!(TL(TL(E)) == NIL)) {
            (*_WRCH)(',');
          }
          E = TL(TL(E));
        }
        (*_WRCH)(']');
      } else if (OP == (LIST)AND_OP && N <= 3 && ROTATE(E) &&
                 ISRELATION(HD(TL(E))) &&
                 ISRELATION_BEGINNING(TL(TL(HD(TL(E)))), TL(TL(E)))) {
        // continued relations
        PRINTEXP(HD(TL(HD(TL(E)))), 4);
        (*_WRCH)(' ');
        writetoken(INFIXNAMEVEC[(WORD)HD(HD(TL(E)))]);
        (*_WRCH)(' ');
        PRINTEXP(TL(TL(E)), 2);
      } else if (ISINFIX(OP) && INFIXPRIOVEC[(WORD)OP] >= N) {
        PRINTEXP(HD(TL(E)), LEFTPREC((OPERATOR)OP));
        if (!(OP == (LIST)COLON_OP)) {
          // DOT.OP should be spaced, DT 2015
          (*_WRCH)(' ');
        }
        writetoken(INFIXNAMEVEC[(WORD)OP]);
        if (!(OP == (LIST)COLON_OP)) {
          (*_WRCH)(' ');
        }
        PRINTEXP(TL(TL(E)), RIGHTPREC((OPERATOR)OP));
      } else {
        (*_WRCH)('(');
        PRINTEXP(E, 0);
        (*_WRCH)(')');
      }
    }
  }
}

static void PRINTZF_EXP(LIST X) {
  LIST Y = X;
  while (!(TL(Y) == NIL)) {
    Y = TL(Y);
  }

  // body
  PRINTEXP(HD(Y), 0);

  // print "such that" as bar if a generator directly follows
  if (ISCONS(HD(X)) && HD(HD(X)) == (LIST)GENERATOR) {
    (*_WRCH)('|');
  } else {
    (*_WRCH)(';');
  }
  while (!(TL(X) == NIL)) {
    LIST QUALIFIER = HD(X);

    if (ISCONS(QUALIFIER) && HD(QUALIFIER) == (LIST)GENERATOR) {
      PRINTEXP(HD(TL(QUALIFIER)), 0);

      // deals with repeated generators
      while (ISCONS(TL(X)) &&
#ifdef INSTRUMENT_KRC_GC
             ISCONS(HD(TL(X))) &&
#endif
             HD(HD(TL(X))) == (LIST)GENERATOR &&
             EQUAL(TL(TL(HD(TL(X)))), TL(TL(QUALIFIER)))) {
        X = TL(X);
        QUALIFIER = HD(X);
        (*_WRCH)(',');
        PRINTEXP(HD(TL(QUALIFIER)), 0);
      }
      bcpl_WRITES("<-");
      PRINTEXP(TL(TL(QUALIFIER)), 0);
    } else {
      PRINTEXP(QUALIFIER, 0);
    }
    X = TL(X);
    if (!(TL(X) == NIL)) {
      (*_WRCH)(';');
    }
  }
}

static BOOL ISLISTEXP(LIST E) {
  while (ISCONS(E) && HD(E) == (LIST)COLON_OP) {
    LIST E1 = TL(TL(E));

    while (ISCONS(E1) && HD(E1) == (LIST)INDIR) {
      E1 = TL(E1);
    }

    TL(TL(E)) = E1;
    E = E1;
  }
  return E == NIL;
}

static BOOL ISRELATION(LIST X) { return ISCONS(X) && ISRELOP(HD(X)); }

static BOOL ISRELATION_BEGINNING(LIST A, LIST X) {
  return (ISRELATION(X) && EQUAL(HD(TL(X)), A)) ||
         (ISCONS(X) && HD(X) == (LIST)AND_OP &&
          ISRELATION_BEGINNING(A, HD(TL(X))));
}

static WORD LEFTPREC(OPERATOR OP) {
  return OP == COLON_OP || OP == APPEND_OP || OP == LISTDIFF_OP ||
                 OP == AND_OP || OP == OR_OP || OP == EXP_OP ||
                 ISRELOP((LIST)OP)
             ? INFIXPRIOVEC[OP] + 1
             : INFIXPRIOVEC[OP];
}

// relops are non-associative
// colon, append, and, or are right-associative
// all other infixes are left-associative

static WORD RIGHTPREC(OPERATOR OP) {
  return OP == COLON_OP || OP == APPEND_OP || OP == LISTDIFF_OP ||
                 OP == AND_OP || OP == OR_OP || OP == EXP_OP
             ? INFIXPRIOVEC[OP]
             : INFIXPRIOVEC[OP] + 1;
}

// puts nested and's into rightist form to ensure
// detection of continued relations
static BOOL ROTATE(LIST E) {
  while (ISCONS(HD(TL(E))) && HD(HD(TL(E))) == (LIST)AND_OP) {
    LIST X = TL(HD(TL(E))), C = TL(TL(E));
    LIST A = HD(X), B = TL(X);
    HD(TL(E)) = A, TL(TL(E)) = CONS((LIST)AND_OP, CONS(B, C));
  }
  return TRUE;
}

// decompiler

// the val field of each user defined name
// contains - cons(cons(nargs,comment),<list of eqns>)
void DISPLAY(ATOM ID, BOOL WITHNOS, BOOL DOUBLESPACING) {
  if (VAL(ID) == NIL) {
    fprintf(bcpl_OUTPUT, "\"%s\" - not defined\n", PRINTNAME(ID));
    return;
  }
  {
    LIST X = HD(VAL(ID)), EQNS = TL(VAL(ID));
    WORD NARGS = (WORD)(HD(X));
    LIST COMMENT = TL(X);
    WORD N = LENGTH(EQNS), I;
    LASTLHS = NIL;
    if (!(COMMENT == NIL)) {
      LIST C = COMMENT;
      fprintf(bcpl_OUTPUT, "    %s :-", PRINTNAME(ID));
      while (!(C == NIL)) {
        bcpl_WRITES(PRINTNAME((ATOM)HD(C)));
        C = TL(C);
        if (!(C == NIL)) {
          (*_WRCH)('\n');
          if (DOUBLESPACING) {
            (*_WRCH)('\n');
          }
        }
      }
      bcpl_WRITES(";\n");
      if (DOUBLESPACING) {
        (*_WRCH)('\n');
      }
    }
    if (COMMENT != NIL && N == 1 && HD(TL(HD(EQNS))) == (LIST)CALL_C) {
      return;
    }
    for (I = 1; I <= N; I++) {

      if (WITHNOS && (N > 1 || COMMENT != NIL)) {
        fprintf(bcpl_OUTPUT, "%2" W ") ", I);
      } else {
        bcpl_WRITES("    ");
      }

      REMOVELINENO(HD(EQNS));
      DISPLAYEQN(ID, NARGS, HD(EQNS));
      if (DOUBLESPACING) {
        (*_WRCH)('\n');
      }
      EQNS = TL(EQNS);
    }
  }
}

static void SHCH(WORD CH) { TRUEWRCH(' '); }

// equation decoder
void DISPLAYEQN(ATOM ID, WORD NARGS, LIST EQN) {
  LIST LHS = HD(EQN), CODE = TL(EQN);

  if (NARGS == 0) {
    bcpl_WRITES(PRINTNAME(ID));
    LASTLHS = (LIST)ID;
  } else {

    if (EQUAL(LHS, LASTLHS)) {
      _WRCH = SHCH;
    } else {
      LASTLHS = LHS;
    }
    PRINTEXP(LHS, 0);
    _WRCH = TRUEWRCH;
  }

  bcpl_WRITES(" = ");

  if (HD(CODE) == (LIST)CALL_C) {
    bcpl_WRITES("<primitive function>");
  } else {
    DISPLAYRHS(LHS, NARGS, CODE);
  }

  (*_WRCH)('\n');
}

void DISPLAYRHS(LIST LHS, WORD NARGS, LIST CODE) {
  LIST V[100];
  WORD I = NARGS, J;
  BOOL IF_FLAG = FALSE;

  // unpack formal parameters into v
  while (I > 0) {
    I = I - 1;
    V[I] = TL(LHS);
    LHS = HD(LHS);
  }

  I = NARGS - 1;

  do {
    switch ((WORD)(HD(CODE))) {
    case LOAD_C:
      CODE = TL(CODE);
      I = I + 1;
      V[I] = HD(CODE);
      break;
    case LOADARG_C:
      CODE = TL(CODE);
      I = I + 1;
      V[I] = V[(WORD)(HD(CODE))];
      break;
    case APPLY_C:
      I = I - 1;
      V[I] = CONS(V[I], V[I + 1]);
      break;
    case APPLYINFIX_C:
      CODE = TL(CODE);
      I = I - 1;
      V[I] = CONS(HD(CODE), CONS(V[I], V[I + 1]));
      break;
    case CONTINUE_INFIX_C:
      CODE = TL(CODE);
      V[I - 1] = CONS(HD(CODE), CONS(V[I - 1], V[I]));
      // note that 2nd arg is left in place above
      // new expression
      break;
    case IF_C:
      IF_FLAG = TRUE;
      break;
    case FORMLIST_C:
      CODE = TL(CODE);
      I = I + 1;
      V[I] = NIL;
      for (J = 1; J <= (WORD)(HD(CODE)); J++) {
        I = I - 1;
        V[I] = CONS((LIST)COLON_OP, CONS(V[I], V[I + 1]));
      }
      break;
    case FORMZF_C:
      CODE = TL(CODE);
      I = I - (WORD)(HD(CODE));
      V[I] = CONS(V[I], NIL);
      for (J = (WORD)(HD(CODE)); J >= 1; J = J - 1)
        V[I] = CONS(V[I + J], V[I]);
      V[I] = CONS((LIST)ZF_OP, V[I]);
      break;
    case CONT_GENERATOR_C:
      CODE = TL(CODE);
      for (J = 1; J <= (WORD)(HD(CODE)); J++)
        V[I - J] = CONS((LIST)GENERATOR, CONS(V[I - J], TL(TL(V[I]))));
      break;
    case MATCH_C:
    case MATCHARG_C:
      CODE = TL(CODE);
      CODE = TL(CODE);
      break;
    case MATCHPAIR_C:
      CODE = TL(CODE);
      {
        LIST X = V[(WORD)HD(CODE)];
        I = I + 2;
        V[I - 1] = HD(TL(X)), V[I] = TL(TL(X));
      }
      break;
    case STOP_C:
      PRINTEXP(V[I], 0);
      if (!(IF_FLAG)) {
        return;
      }
      bcpl_WRITES(", ");
      PRINTEXP(V[I - 1], 0);
      return;
    default:
      bcpl_WRITES("IMPOSSIBLE INSTRUCTION IN \"DISPLAYRHS\"\n");
    }
    // end of switch
    CODE = TL(CODE);
  } while (1);
}

// extracts that part of the code which
// determines which cases this equation applies to
LIST PROFILE(LIST EQN) {
  LIST CODE = TL(EQN);
  if (HD(CODE) == (LIST)LINENO_C) {
    CODE = TL(TL(CODE));
  }
  {
    LIST C = CODE;
    while (PARMY(HD(C)))
      C = REST(C);
    {
      LIST HOLD = C;
      while (!(HD(C) == (LIST)IF_C || HD(C) == (LIST)STOP_C))
        C = REST(C);
      if (HD(C) == (LIST)IF_C) {
        return SUBTRACT(CODE, C);
      } else {
        return SUBTRACT(CODE, HOLD);
      }
    }
  }
}

static BOOL PARMY(LIST X) {
  return X == (LIST)MATCH_C || X == (LIST)MATCHARG_C || X == (LIST)MATCHPAIR_C;
}

// removes one complete instruction from C
static LIST REST(LIST C) {
  LIST X = HD(C);
  C = TL(C);

  if (X == (LIST)APPLY_C || X == (LIST)IF_C || X == (LIST)STOP_C) {
    return C;
  }

  C = TL(C);

  if (!(X == (LIST)MATCH_C || X == (LIST)MATCHARG_C)) {
    return C;
  }

  return TL(C);
}

// list subtraction
static LIST SUBTRACT(LIST X, LIST Y) {
  LIST Z = NIL;

  while (!(X == Y)) {
    Z = CONS(HD(X), Z), X = TL(X);
  }

  // note the result is reversed - for our purposes this does not matter
  return Z;
}

// called whenever the definiendum is subject of a
// display,reorder or (partial)delete command - has the effect of
// restoring the standard line numbering
void REMOVELINENO(LIST EQN) {
  if (HD(TL(EQN)) == (LIST)LINENO_C) {
    TL(EQN) = TL(TL(TL(EQN)));
  }
}

// compiler for krc expressions and equations

LIST EXP() {
  INIT_CODEV();
  EXPR(0);
  PLANT0(STOP_C);
  return COLLECTCODE();
}

// returns a triple: cons(subject,cons(nargs,eqn))
LIST EQUATION() {
  LIST SUBJECT = 0, LHS = 0;
  WORD NARGS = 0;
  INIT_CODEV();
  if (haveid()) {
    SUBJECT = (LIST)THE_ID, LHS = (LIST)THE_ID;
    while (STARTFORMAL(HD(TOKENS))) {
      LHS = CONS(LHS, FORMAL());
      NARGS = NARGS + 1;
    }
  } else if (HD(TOKENS) == (LIST)'=' && LASTLHS != NIL) {
    SUBJECT = LASTLHS, LHS = LASTLHS;
    while (ISCONS(SUBJECT))
      SUBJECT = HD(SUBJECT), NARGS = NARGS + 1;
  } else {
    syntax(), bcpl_WRITES("missing LHS\n");
    return NIL;
  }
  COMPILELHS(LHS, NARGS);
  {
    LIST CODE = COLLECTCODE();
    check((TOKEN)'=');
    EXPR(0);
    PLANT0(STOP_C);
    {
      LIST EXPCODE = COLLECTCODE();

      // change from EMAS/KRC to allow guarded simple def
      if (have((TOKEN)',')) {
        EXPR(0);
        PLANT0(IF_C);
        CODE = APPEND(CODE, APPEND(COLLECTCODE(), EXPCODE));
      } else {
        CODE = APPEND(CODE, EXPCODE);
      }

      if (!(HD(TOKENS) == ENDSTREAMCH)) {
        check(EOL);
      }

      if (!(ERRORFLAG)) {
        LASTLHS = LHS;
      }

      if (NARGS == 0) {
        LHS = 0;
      }

      // in this case the lhs field is used to remember
      // the value of the variable - 0 means not yet set

      // OK
      return CONS(SUBJECT, CONS((LIST)NARGS, CONS(LHS, CODE)));
    }
  }
}

// N is the priority level
static void EXPR(WORD N) {
  if (N <= 3 && (have((TOKEN)'\\') || have((TOKEN)'~'))) {
    PLANT1(LOAD_C, (LIST)NOT_OP);
    EXPR(3);
    PLANT0(APPLY_C);
  } else if (N <= 5 && have((TOKEN)'+')) {
    EXPR(5);
  } else if (N <= 5 && have((TOKEN)'-')) {
    PLANT1(LOAD_C, (LIST)NEG_OP);
    EXPR(5);
    PLANT0(APPLY_C);
  } else if (have((TOKEN)'#')) {
    PLANT1(LOAD_C, (LIST)LENGTH_OP);
    COMBN();
    PLANT0(APPLY_C);
  } else if (STARTSIMPLE(HD(TOKENS)))
    COMBN();
  else {
    syntax();
    return;
  }
  {
    OPERATOR OP = MKINFIX(HD(TOKENS));
    while (DIPRIO(OP) >= N) {
      WORD I, AND_COUNT = 0; // FOR CONTINUED RELATIONS
      TOKENS = TL(TOKENS);
      EXPR(RIGHTPREC(OP));

      if (ERRORFLAG) {
        return;
      }

      while (ISRELOP((LIST)OP) && ISRELOP((LIST)MKINFIX(HD(TOKENS)))) {
        // continued relations
        AND_COUNT = AND_COUNT + 1;
        PLANT1(CONTINUE_INFIX_C, (LIST)OP);
        OP = MKINFIX(HD(TOKENS));
        TOKENS = TL(TOKENS);
        EXPR(4);
        if (ERRORFLAG) {
          return;
        }
      }
      PLANT1(APPLYINFIX_C, (LIST)OP);
      for (I = 1; I <= AND_COUNT; I++) {
        PLANT1(APPLYINFIX_C, (LIST)AND_OP);
      }
      // for continued relations
      OP = MKINFIX(HD(TOKENS));
    }
  }
}

static void COMBN() {
  SIMPLE();
  while (STARTSIMPLE(HD(TOKENS))) {
    SIMPLE();
    PLANT0(APPLY_C);
  }
}

static BOOL STARTFORMAL(TOKEN T) {
  return ISCONS(T) ? (HD(T) == IDENT || HD(T) == (LIST)CONST)
                   : T == (TOKEN)'(' || T == (TOKEN)'[' || T == (TOKEN)'-';
}

static BOOL STARTSIMPLE(TOKEN T) {
  return ISCONS(T) ? (HD(T) == IDENT || HD(T) == (LIST)CONST)
                   : T == (TOKEN)'(' || T == (TOKEN)'[' || T == (TOKEN)'{' ||
                         T == (TOKEN)'\'';
}

static void SIMPLE() {
  if (haveid()) {
    COMPILENAME(THE_ID);
  } else if (haveconst()) {
    PLANT1(LOAD_C, (LIST)INTERNALISE(THE_CONST));
  } else if (have((TOKEN)'(')) {
    EXPR(0);
    check((TOKEN)')');
  } else if (have((TOKEN)'['))
    if (have((TOKEN)']')) {
      PLANT1(LOAD_C, NIL);
    } else {
      WORD N = 1;
      EXPR(0);
      if (have((TOKEN)',')) {
        EXPR(0);
        N = N + 1;
      }
      if (have(DOTDOT_SY)) {
        if (HD(TOKENS) == (TOKEN)']') {
          PLANT1(LOAD_C, INFINITY);
        } else {
          EXPR(0);
        }

        if (N == 2) {
          PLANT0(APPLY_C);
        }

        PLANT1(APPLYINFIX_C, (LIST)(N == 1 ? DOTDOT_OP : COMMADOTDOT_OP));

        // OK
      } else {
        while (have((TOKEN)',')) {
          EXPR(0);
          N = N + 1;
        }
        PLANT1(FORMLIST_C, (LIST)N);
        // OK
      }
      check((TOKEN)']');
    }
  else if (have((TOKEN)'{')) {
    // ZF expressions bug?
    WORD N = 0;
    LIST HOLD = TOKENS;
    PERFORM_ALPHA_CONVERSIONS();
    EXPR(0);
    // implicit zf body no longer legal
    // if ( HD(TOKENS)==BACKARROW_SY ) TOKENS=HOLD; else
    check((TOKEN)';');
    do
      N = N + QUALIFIER();
    while (have((TOKEN)';'));
    // OK
    PLANT1(FORMZF_C, (LIST)N);
    check((TOKEN)'}');
  } else if (have((TOKEN)'\'')) {
    // operator denotation
    if (have((TOKEN)'#')) {
      PLANT1(LOAD_C, (LIST)LENGTH_OP);
    } else if (have((TOKEN)'\\') || have((TOKEN)'~')) {
      PLANT1(LOAD_C, (LIST)NOT_OP);
    } else {
      OPERATOR OP = MKINFIX((TOKEN)(HD(TOKENS)));
      if (ISINFIX((LIST)OP)) {
        TOKENS = TL(TOKENS);
      } else {
        // missing infix or prefix operator
        syntax();
      }
      PLANT1(LOAD_C, (LIST)QUOTE_OP);
      PLANT1(LOAD_C, (LIST)OP);
      PLANT0(APPLY_C);
    }
    check((TOKEN)'\'');
  } else
    // missing identifier|constant|(|[|{
    syntax();
}

static void COMPILENAME(ATOM N) {
  WORD I = 0;
  while (!(I > ENVP || ENV[I] == (LIST)N)) {
    I = I + 1;
  }

  if (I > ENVP) {
    PLANT1(LOAD_C, (LIST)N);
  } else {
    // OK
    PLANT1(LOADARG_C, (LIST)I);
  }
}

static WORD QUALIFIER() {
  // what about more general formals?
  if (ISGENERATOR(TL(TOKENS))) {
    WORD N = 0;

    do {
      haveid();
      PLANT1(LOAD_C, (LIST)THE_ID);
      N = N + 1;
    } while (have((TOKEN)','));

    check(BACKARROW_SY);
    EXPR(0);
    PLANT1(APPLYINFIX_C, (LIST)GENERATOR);

    if (N > 1) {
      // OK
      PLANT1(CONT_GENERATOR_C, (LIST)(N - 1));
    }

    return N;
  } else {
    EXPR(0);
    return 1;
  }
}

// also recognises the "such that" bar and converts it to ';'
// to distinguish it from "or"
static void PERFORM_ALPHA_CONVERSIONS() {
  LIST P = TOKENS;
  while (!(HD(P) == (TOKEN)'}' || HD(P) == (TOKEN)']' || HD(P) == EOL)) {
    if (HD(P) == (TOKEN)'[' || HD(P) == (TOKEN)'{') {
      P = SKIPCHUNK(P);
      continue;
    }

    if (HD(P) == (TOKEN)'|' && ISID(HD(TL(P))) && ISGENERATOR(TL(TL(P)))) {
      HD(P) = (TOKEN)';';
    }

    if (ISID(HD(P)) && ISGENERATOR(TL(P))) {
      ALPHA_CONVERT(HD(P), TL(P));
    }

    P = TL(P);
  }
}

BOOL ISID(LIST X) { return ISCONS(X) && HD(X) == IDENT; }

static BOOL ISGENERATOR(LIST T) {
  return !ISCONS(T) ? FALSE
                    : HD(T) == BACKARROW_SY ||
                          (HD(T) == (TOKEN)',' && ISID(HD(TL(T))) &&
                           ISGENERATOR(TL(TL(T))));
}

static void ALPHA_CONVERT(LIST VAR, LIST P) {
  LIST T = TOKENS;
  LIST VAR1 = CONS((LIST)ALPHA, TL(VAR));
  LIST EDGE = T;
  while (
      !(HD(EDGE) == (TOKEN)';' || HD(EDGE) == BACKARROW_SY || HD(EDGE) == EOL))
    EDGE = SKIPCHUNK(EDGE);
  while (!(T == EDGE)) {
    CONV1(T, VAR, VAR1);
    T = TL(T);
  }
  T = P;

  while (!(HD(T) == (TOKEN)';' || HD(T) == EOL)) {
    T = SKIPCHUNK(T);
  }

  EDGE = T;
  while (
      !(HD(EDGE) == (TOKEN)'}' || HD(EDGE) == (TOKEN)']' || HD(EDGE) == EOL)) {
    EDGE = SKIPCHUNK(EDGE);
  }

  while (!(T == EDGE)) {
    CONV1(T, VAR, VAR1);
    T = TL(T);
  }
  TL(VAR) = VAR1;
}

static LIST SKIPCHUNK(LIST P) {
  WORD KET = HD(P) == (TOKEN)'{' ? '}' : HD(P) == (TOKEN)'[' ? ']' : -1;
  P = TL(P);

  if (KET == -1) {
    return P;
  }

  // OK
  while (!(HD(P) == (LIST)KET || HD(P) == EOL)) {
    P = SKIPCHUNK(P);
  }

  if (!(HD(P) == EOL)) {
    P = TL(P);
  }

  return (P);
}

static void CONV1(LIST T, LIST VAR, LIST VAR1) {
  if (EQUAL(HD(T), VAR) && HD(T) != VAR) {
    TL(HD(T)) = VAR1;
  }
}

static LIST FORMAL() {
  if (haveid()) {
    return (LIST)THE_ID;
  } else if (haveconst()) {
    return INTERNALISE(THE_CONST);
  } else if (have((TOKEN)'(')) {
    LIST P = PATTERN();
    check((TOKEN)')');
    return P;
  } else if (have((TOKEN)'[')) {
    LIST PLIST = NIL, P = NIL;

    if (have((TOKEN)']')) {
      return NIL;
    }

    do {
      PLIST = CONS(PATTERN(), PLIST);
    } while (have((TOKEN)','));
    // note they are in reverse order

    check((TOKEN)']');

    while (!(PLIST == NIL)) {
      P = CONS((TOKEN)COLON_OP, CONS(HD(PLIST), P));
      PLIST = TL(PLIST);
    }
    // now they are in correct order

    return P;
  } else if (have((TOKEN)'-') && havenum()) {
    THE_NUM = -THE_NUM;
    return STONUM(THE_NUM);
  } else {
    syntax(); // MISSING identifier|constant|(|[
    return NIL;
  }
}

static LIST INTERNALISE(LIST VAL) {
  return VAL == TL(TRUTH)
             ? TRUTH
             : VAL == TL(FALSITY) ? FALSITY
                                  : ISATOM(VAL) ? CONS((LIST)QUOTE, VAL) : VAL;
}

static LIST PATTERN() {
  LIST P = FORMAL();

  if (have((TOKEN)':')) {
    P = CONS((LIST)COLON_OP, CONS(P, PATTERN()));
  }

  return P;
}

static void COMPILELHS(LIST LHS, WORD NARGS) {
  WORD I;
  ENVP = NARGS - 1;
  for (I = 1; I <= NARGS; I++) {
    ENV[NARGS - I] = TL(LHS);
    LHS = HD(LHS);
  }
  for (I = 0; I <= NARGS - 1; I++) {
    COMPILEFORMAL(ENV[I], I);
  }
}

static void COMPILEFORMAL(LIST X, WORD I) {

  // identifier
  if (ISATOM(X)) {
    WORD J = 0;

    while (!(J >= I || ENV[J] == X)) {
      // is this a repeated name?
      J = J + 1;
    }

    if (J >= I) {
      // no, no code compiled
      return;
    } else {
      PLANT2(MATCHARG_C, (LIST)I, (LIST)J);
    }

  } else if (ISNUM(X) || X == NIL || (ISCONS(X) && HD(X) == (LIST)QUOTE)) {
    PLANT2(MATCH_C, (LIST)I, X);
  } else if (ISCONS(X) && HD(X) == (TOKEN)COLON_OP && ISCONS(TL(X))) {
    PLANT1(MATCHPAIR_C, (LIST)I); // OK
    ENVP = ENVP + 2;
    {
      WORD A = ENVP - 1, B = ENVP;
      ENV[A] = HD(TL(X)), ENV[B] = TL(TL(X));
      COMPILEFORMAL(ENV[A], A);
      COMPILEFORMAL(ENV[B], B);
    }
  } else {
    bcpl_WRITES("Impossible event in \"COMPILEFORMAL\"\n");
  }
}

// PLANT stores INSTRUCTIONs and their operands in the code vector
// OP is always an instruction code (*_C);
// A and B can be operators (*_OP), INTs, CONSTs, IDs (names) or
// the address of a C function - all are mapped to LIST type.

// APPLY_C IF_C STOP_C
static void PLANT0(INSTRUCTION OP) { CODEV = CONS((LIST)OP, CODEV); }

// everything else
static void PLANT1(INSTRUCTION OP, LIST A) {
  CODEV = CONS((LIST)OP, CODEV);
  CODEV = CONS(A, CODEV);
}

// MATCH_C MATCHARG_C
static void PLANT2(INSTRUCTION OP, LIST A, LIST B) {
  CODEV = CONS((LIST)OP, CODEV);
  CODEV = CONS(A, CODEV);
  CODEV = CONS(B, CODEV);
}

// flushes the code buffer
static LIST COLLECTCODE() {
  LIST TMP = CODEV;
  CODEV = NIL;

  return REVERSE(TMP);
}

// mark elements in CODEV and ENV for preservation by the GC.
// this routine should be called by your BASES() function.
void COMPILER_BASES(void (*F)(LIST *)) {
  WORD I;

  F(&CODEV);
  // ENVP indexes the last used element and starts as -1.
  for (I = 0; I <= ENVP; I++) {
    F(&ENV[I]);
  }
}
