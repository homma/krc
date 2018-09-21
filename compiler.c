// KRC compiler

// Note: What is now '{' here was '{ ' in the BCPL.

#include "common.h"
#include "iolib.h"
#include "listlib.h"
#include "compiler.h"

//----------------------------------------------------------------------
// The KRC system is Copyright (c) D. A. Turner 1981
// All  rights reserved.  It is distributed as free software under the
// terms in the file "COPYING", which is included in the distribution.
//----------------------------------------------------------------------

// local function declarations
static bool isop(LIST X);
static bool isinfix(LIST X);
static bool isrelop(LIST X);
static word diprio(OPERATOR OP);
static OPERATOR mkinfix(TOKEN T);
static void printzf_exp(LIST X);
static bool islistexp(LIST E);
static bool isrelation(LIST X);
static bool isrelation_beginning(LIST A, LIST X);
static word leftprec(OPERATOR OP);
static word rightprec(OPERATOR OP);
static bool rotate(LIST E);
static bool parmy(LIST X);
static LIST rest(LIST C);
static LIST subtract(LIST X, LIST Y);
static void expr(word N);
static bool startformal(TOKEN T);
static bool startsimple(TOKEN T);
static void combn(void);
static void simple(void);
static void compilename(ATOM N);
static word qualifier(void);
static void perform_alpha_conversions();
static bool isgenerator(LIST T);
static void alpha_convert(LIST VAR, LIST P);
static LIST skipchunk(LIST P);
static void conv1(LIST T, LIST VAR, LIST VAR1);
static LIST formal(void);
static LIST internalise(LIST VAL);
static LIST pattern(void);
static void compilelhs(LIST LHS, word NARGS);
static void compileformal(LIST X, word I);
static void plant0(INSTRUCTION OP);
static void plant1(INSTRUCTION OP, LIST A);
static void plant2(INSTRUCTION OP, LIST A, LIST B);
static LIST collectcode(void);

// global variables
void (*TRUEWRCH)(word C) = bcpl_WRCH;
LIST LASTLHS = NIL;
LIST TRUTH, FALSITY, INFINITY;

// setup_infixes() - interesting elements start at [1]
// the indices correspond to the OPERATOR values in compiler.h
// EQ_SY was (TOKEN)'=', changed DT May 2015
static TOKEN INFIXNAMEVEC[] = {
    (TOKEN)0,   (TOKEN)':', PLUSPLUS_SY, DASHDASH_SY, (TOKEN)'|',
    (TOKEN)'&', (TOKEN)'>', GE_SY,       NE_SY,       EQ_SY,
    LE_SY,      (TOKEN)'<', (TOKEN)'+',  (TOKEN)'-',  (TOKEN)'*',
    (TOKEN)'/', (TOKEN)'%', STARSTAR_SY, (TOKEN)'.',
};
static word INFIXPRIOVEC[] = {0, 0, 0, 0, 1, 2, 3, 3, 3, 3,
                              3, 3, 4, 4, 5, 5, 5, 6, 6};

// bases for garbage collection
// store for opcodes and ther params, which may be operators,
// various CONStructs or the addresses of C functions.
static LIST CODEV = NIL;

// appears to be a store for formal parameters
static LIST ENV[100];

static word ENVP;

void init_codev() {
  ENVP = -1;
  CODEV = NIL;
}

static bool isop(LIST X) {
  return X == (LIST)ALPHA || X == (LIST)INDIR ||
         ((LIST)QUOTE <= X && X <= (LIST)QUOTE_OP);
}

static bool isinfix(LIST X) { return (LIST)COLON_OP <= X && X <= (LIST)DOT_OP; }

static bool isrelop(LIST X) { return (LIST)GR_OP <= X && X <= (LIST)LS_OP; }

// return the priority of an operator from its index in INFIX*
static word diprio(OPERATOR OP) { return OP == -1 ? -1 : INFIXPRIOVEC[OP]; }

// takes a token , returns an operator
// else -1 if t not the name of an infix
static OPERATOR mkinfix(TOKEN T) {
  word I = 1;
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

// N is the priority level
void printexp(LIST E, word N) {
  if (E == NIL) {
    bcpl_WRITES("[]");
  } else if (ISATOM(E)) {
    bcpl_WRITES(PRINTNAME((ATOM)E));
  } else if (ISNUM(E)) {
    word X = GETNUM(E);
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
      // maybe could be operator
      LIST OP = HD(E);
      if (!isop(OP) && N <= 7) {
        printexp(OP, 7);
        (*_WRCH)(' ');
        printexp(TL(E), 8);
      } else if (OP == (LIST)QUOTE) {
        printatom((ATOM)TL(E), true);
      } else if (OP == (LIST)INDIR || OP == (LIST)ALPHA) {
        printexp(TL(E), N);
      } else if (OP == (LIST)DOTDOT_OP || OP == (LIST)COMMADOTDOT_OP) {
        (*_WRCH)('[');
        E = TL(E);
        printexp(HD(E), 0);
        if (OP == (LIST)COMMADOTDOT_OP) {
          (*_WRCH)(',');
          E = TL(E);
          printexp(HD(E), 0);
        }
        bcpl_WRITES("..");
        if (!(TL(E) == INFINITY)) {
          printexp(TL(E), 0);
        }
        (*_WRCH)(']');
      } else if (OP == (LIST)ZF_OP) {
        (*_WRCH)('{');
        printzf_exp(TL(E));
        (*_WRCH)('}');
      } else if (OP == (LIST)NOT_OP && N <= 3) {
        (*_WRCH)('\\');
        printexp(TL(E), 3);
      } else if (OP == (LIST)NEG_OP && N <= 5) {
        (*_WRCH)('-');
        printexp(TL(E), 5);
      } else if (OP == (LIST)LENGTH_OP && N <= 7) {
        (*_WRCH)('#');
        printexp(TL(E), 7);
      } else if (OP == (LIST)QUOTE_OP) {
        (*_WRCH)('\'');
        if (TL(E) == (LIST)LENGTH_OP) {
          (*_WRCH)('#');
        } else if (TL(E) == (LIST)NOT_OP) {
          (*_WRCH)('\\');
        } else {
          writetoken(INFIXNAMEVEC[(word)TL(E)]);
        }
        (*_WRCH)('\'');
      } else if (islistexp(E)) {
        (*_WRCH)('[');
        while (!(E == NIL)) {
          printexp(HD(TL(E)), 0);
          if (!(TL(TL(E)) == NIL)) {
            (*_WRCH)(',');
          }
          E = TL(TL(E));
        }
        (*_WRCH)(']');
      } else if (OP == (LIST)AND_OP && N <= 3 && rotate(E) &&
                 isrelation(HD(TL(E))) &&
                 isrelation_beginning(TL(TL(HD(TL(E)))), TL(TL(E)))) {
        // continued relations
        printexp(HD(TL(HD(TL(E)))), 4);
        (*_WRCH)(' ');
        writetoken(INFIXNAMEVEC[(word)HD(HD(TL(E)))]);
        (*_WRCH)(' ');
        printexp(TL(TL(E)), 2);
      } else if (isinfix(OP) && INFIXPRIOVEC[(word)OP] >= N) {
        printexp(HD(TL(E)), leftprec((OPERATOR)OP));
        if (!(OP == (LIST)COLON_OP)) {
          // DOT.OP should be spaced, DT 2015
          (*_WRCH)(' ');
        }
        writetoken(INFIXNAMEVEC[(word)OP]);
        if (!(OP == (LIST)COLON_OP)) {
          (*_WRCH)(' ');
        }
        printexp(TL(TL(E)), rightprec((OPERATOR)OP));
      } else {
        (*_WRCH)('(');
        printexp(E, 0);
        (*_WRCH)(')');
      }
    }
  }
}

static void printzf_exp(LIST X) {
  LIST Y = X;
  while (!(TL(Y) == NIL)) {
    Y = TL(Y);
  }

  // body
  printexp(HD(Y), 0);

  // print "such that" as bar if a generator directly follows
  if (ISCONS(HD(X)) && HD(HD(X)) == (LIST)GENERATOR) {
    (*_WRCH)('|');
  } else {
    (*_WRCH)(';');
  }
  while (!(TL(X) == NIL)) {
    LIST qualifier = HD(X);

    if (ISCONS(qualifier) && HD(qualifier) == (LIST)GENERATOR) {
      printexp(HD(TL(qualifier)), 0);

      // deals with repeated generators
      while (ISCONS(TL(X)) &&
#ifdef INSTRUMENT_KRC_GC
             ISCONS(HD(TL(X))) &&
#endif
             HD(HD(TL(X))) == (LIST)GENERATOR &&
             EQUAL(TL(TL(HD(TL(X)))), TL(TL(qualifier)))) {
        X = TL(X);
        qualifier = HD(X);
        (*_WRCH)(',');
        printexp(HD(TL(qualifier)), 0);
      }
      bcpl_WRITES("<-");
      printexp(TL(TL(qualifier)), 0);
    } else {
      printexp(qualifier, 0);
    }
    X = TL(X);
    if (!(TL(X) == NIL)) {
      (*_WRCH)(';');
    }
  }
}

static bool islistexp(LIST E) {
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

static bool isrelation(LIST X) { return ISCONS(X) && isrelop(HD(X)); }

static bool isrelation_beginning(LIST A, LIST X) {
  return (isrelation(X) && EQUAL(HD(TL(X)), A)) ||
         (ISCONS(X) && HD(X) == (LIST)AND_OP &&
          isrelation_beginning(A, HD(TL(X))));
}

static word leftprec(OPERATOR OP) {
  return OP == COLON_OP || OP == APPEND_OP || OP == LISTDIFF_OP ||
                 OP == AND_OP || OP == OR_OP || OP == EXP_OP ||
                 isrelop((LIST)OP)
             ? INFIXPRIOVEC[OP] + 1
             : INFIXPRIOVEC[OP];
}

// relops are non-associative
// colon, append, and, or are right-associative
// all other infixes are left-associative

static word rightprec(OPERATOR OP) {
  return OP == COLON_OP || OP == APPEND_OP || OP == LISTDIFF_OP ||
                 OP == AND_OP || OP == OR_OP || OP == EXP_OP
             ? INFIXPRIOVEC[OP]
             : INFIXPRIOVEC[OP] + 1;
}

// puts nested and's into rightist form to ensure
// detection of continued relations
static bool rotate(LIST E) {
  while (ISCONS(HD(TL(E))) && HD(HD(TL(E))) == (LIST)AND_OP) {
    LIST X = TL(HD(TL(E))), C = TL(TL(E));
    LIST A = HD(X), B = TL(X);
    HD(TL(E)) = A, TL(TL(E)) = cons((LIST)AND_OP, cons(B, C));
  }
  return true;
}

// decompiler

// the val field of each user defined name
// contains - cons(cons(nargs,comment),<list of eqns>)
void display(ATOM ID, bool WITHNOS, bool DOUBLESPACING) {
  if (VAL(ID) == NIL) {
    fprintf(bcpl_OUTPUT, "\"%s\" - not defined\n", PRINTNAME(ID));
    return;
  }
  {
    LIST X = HD(VAL(ID)), EQNS = TL(VAL(ID));
    word NARGS = (word)(HD(X));
    LIST COMMENT = TL(X);
    word N = LENGTH(EQNS), I;
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

      removelineno(HD(EQNS));
      displayeqn(ID, NARGS, HD(EQNS));
      if (DOUBLESPACING) {
        (*_WRCH)('\n');
      }
      EQNS = TL(EQNS);
    }
  }
}

static void SHCH(word CH) { TRUEWRCH(' '); }

// equation decoder
void displayeqn(ATOM ID, word NARGS, LIST EQN) {
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
    printexp(LHS, 0);
    _WRCH = TRUEWRCH;
  }

  bcpl_WRITES(" = ");

  if (HD(CODE) == (LIST)CALL_C) {
    bcpl_WRITES("<primitive function>");
  } else {
    displayrhs(LHS, NARGS, CODE);
  }

  (*_WRCH)('\n');
}

void displayrhs(LIST LHS, word NARGS, LIST CODE) {
  LIST V[100];
  word I = NARGS, J;
  bool IF_FLAG = false;

  // unpack formal parameters into v
  while (I > 0) {
    I = I - 1;
    V[I] = TL(LHS);
    LHS = HD(LHS);
  }

  I = NARGS - 1;

  do {
    switch ((word)(HD(CODE))) {
    case LOAD_C:
      CODE = TL(CODE);
      I = I + 1;
      V[I] = HD(CODE);
      break;
    case LOADARG_C:
      CODE = TL(CODE);
      I = I + 1;
      V[I] = V[(word)(HD(CODE))];
      break;
    case APPLY_C:
      I = I - 1;
      V[I] = cons(V[I], V[I + 1]);
      break;
    case APPLYINFIX_C:
      CODE = TL(CODE);
      I = I - 1;
      V[I] = cons(HD(CODE), cons(V[I], V[I + 1]));
      break;
    case CONTINUE_INFIX_C:
      CODE = TL(CODE);
      V[I - 1] = cons(HD(CODE), cons(V[I - 1], V[I]));
      // note that 2nd arg is left in place above
      // new expression
      break;
    case IF_C:
      IF_FLAG = true;
      break;
    case FORMLIST_C:
      CODE = TL(CODE);
      I = I + 1;
      V[I] = NIL;
      for (J = 1; J <= (word)(HD(CODE)); J++) {
        I = I - 1;
        V[I] = cons((LIST)COLON_OP, cons(V[I], V[I + 1]));
      }
      break;
    case FORMZF_C:
      CODE = TL(CODE);
      I = I - (word)(HD(CODE));
      V[I] = cons(V[I], NIL);
      for (J = (word)(HD(CODE)); J >= 1; J = J - 1)
        V[I] = cons(V[I + J], V[I]);
      V[I] = cons((LIST)ZF_OP, V[I]);
      break;
    case CONT_GENERATOR_C:
      CODE = TL(CODE);
      for (J = 1; J <= (word)(HD(CODE)); J++)
        V[I - J] = cons((LIST)GENERATOR, cons(V[I - J], TL(TL(V[I]))));
      break;
    case MATCH_C:
    case MATCHARG_C:
      CODE = TL(CODE);
      CODE = TL(CODE);
      break;
    case MATCHPAIR_C:
      CODE = TL(CODE);
      {
        LIST X = V[(word)HD(CODE)];
        I = I + 2;
        V[I - 1] = HD(TL(X)), V[I] = TL(TL(X));
      }
      break;
    case STOP_C:
      printexp(V[I], 0);
      if (!(IF_FLAG)) {
        return;
      }
      bcpl_WRITES(", ");
      printexp(V[I - 1], 0);
      return;
    default:
      bcpl_WRITES("IMPOSSIBLE INSTRUCTION IN \"displayrhs\"\n");
    }
    // end of switch
    CODE = TL(CODE);
  } while (1);
}

// extracts that part of the code which
// determines which cases this equation applies to
LIST profile(LIST EQN) {
  LIST CODE = TL(EQN);
  if (HD(CODE) == (LIST)LINENO_C) {
    CODE = TL(TL(CODE));
  }
  {
    LIST C = CODE;
    while (parmy(HD(C)))
      C = rest(C);
    {
      LIST HOLD = C;
      while (!(HD(C) == (LIST)IF_C || HD(C) == (LIST)STOP_C))
        C = rest(C);
      if (HD(C) == (LIST)IF_C) {
        return subtract(CODE, C);
      } else {
        return subtract(CODE, HOLD);
      }
    }
  }
}

static bool parmy(LIST X) {
  return X == (LIST)MATCH_C || X == (LIST)MATCHARG_C || X == (LIST)MATCHPAIR_C;
}

// removes one complete instruction from C
static LIST rest(LIST C) {
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
static LIST subtract(LIST X, LIST Y) {
  LIST Z = NIL;

  while (!(X == Y)) {
    Z = cons(HD(X), Z), X = TL(X);
  }

  // note the result is reversed - for our purposes this does not matter
  return Z;
}

// called whenever the definiendum is subject of a
// display,reorder or (partial)delete command - has the effect of
// restoring the standard line numbering
void removelineno(LIST EQN) {
  if (HD(TL(EQN)) == (LIST)LINENO_C) {
    TL(EQN) = TL(TL(TL(EQN)));
  }
}

// compiler for krc expressions and equations
// renamed from exp as the name conflicts with exp(3)
LIST expression() {
  init_codev();
  expr(0);
  plant0(STOP_C);
  return collectcode();
}

// returns a triple: cons(subject,cons(nargs,eqn))
LIST equation() {
  LIST SUBJECT = 0, LHS = 0;
  word NARGS = 0;
  init_codev();
  if (haveid()) {
    SUBJECT = (LIST)THE_ID, LHS = (LIST)THE_ID;
    while (startformal(HD(TOKENS))) {
      LHS = cons(LHS, formal());
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
  compilelhs(LHS, NARGS);
  {
    LIST CODE = collectcode();
    check((TOKEN)'=');
    expr(0);
    plant0(STOP_C);
    {
      LIST EXPCODE = collectcode();

      // change from EMAS/KRC to allow guarded simple def
      if (have((TOKEN)',')) {
        expr(0);
        plant0(IF_C);
        CODE = APPEND(CODE, APPEND(collectcode(), EXPCODE));
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
      return cons(SUBJECT, cons((LIST)NARGS, cons(LHS, CODE)));
    }
  }
}

// N is the priority level
static void expr(word N) {
  if (N <= 3 && (have((TOKEN)'\\') || have((TOKEN)'~'))) {
    plant1(LOAD_C, (LIST)NOT_OP);
    expr(3);
    plant0(APPLY_C);
  } else if (N <= 5 && have((TOKEN)'+')) {
    expr(5);
  } else if (N <= 5 && have((TOKEN)'-')) {
    plant1(LOAD_C, (LIST)NEG_OP);
    expr(5);
    plant0(APPLY_C);
  } else if (have((TOKEN)'#')) {
    plant1(LOAD_C, (LIST)LENGTH_OP);
    combn();
    plant0(APPLY_C);
  } else if (startsimple(HD(TOKENS)))
    combn();
  else {
    syntax();
    return;
  }
  {
    OPERATOR OP = mkinfix(HD(TOKENS));
    while (diprio(OP) >= N) {
      // for continued relations
      word I, AND_COUNT = 0;

      TOKENS = TL(TOKENS);
      expr(rightprec(OP));

      if (ERRORFLAG) {
        return;
      }

      while (isrelop((LIST)OP) && isrelop((LIST)mkinfix(HD(TOKENS)))) {
        // continued relations
        AND_COUNT = AND_COUNT + 1;
        plant1(CONTINUE_INFIX_C, (LIST)OP);
        OP = mkinfix(HD(TOKENS));
        TOKENS = TL(TOKENS);
        expr(4);
        if (ERRORFLAG) {
          return;
        }
      }
      plant1(APPLYINFIX_C, (LIST)OP);
      for (I = 1; I <= AND_COUNT; I++) {
        plant1(APPLYINFIX_C, (LIST)AND_OP);
      }
      // for continued relations
      OP = mkinfix(HD(TOKENS));
    }
  }
}

static void combn() {
  simple();
  while (startsimple(HD(TOKENS))) {
    simple();
    plant0(APPLY_C);
  }
}

static bool startformal(TOKEN T) {
  return ISCONS(T) ? (HD(T) == IDENT || HD(T) == (LIST)CONST)
                   : T == (TOKEN)'(' || T == (TOKEN)'[' || T == (TOKEN)'-';
}

static bool startsimple(TOKEN T) {
  return ISCONS(T) ? (HD(T) == IDENT || HD(T) == (LIST)CONST)
                   : T == (TOKEN)'(' || T == (TOKEN)'[' || T == (TOKEN)'{' ||
                         T == (TOKEN)'\'';
}

static void simple() {
  if (haveid()) {
    compilename(THE_ID);
  } else if (haveconst()) {
    plant1(LOAD_C, (LIST)internalise(THE_CONST));
  } else if (have((TOKEN)'(')) {
    expr(0);
    check((TOKEN)')');
  } else if (have((TOKEN)'['))
    if (have((TOKEN)']')) {
      plant1(LOAD_C, NIL);
    } else {
      word N = 1;
      expr(0);
      if (have((TOKEN)',')) {
        expr(0);
        N = N + 1;
      }
      if (have(DOTDOT_SY)) {
        if (HD(TOKENS) == (TOKEN)']') {
          plant1(LOAD_C, INFINITY);
        } else {
          expr(0);
        }

        if (N == 2) {
          plant0(APPLY_C);
        }

        plant1(APPLYINFIX_C, (LIST)(N == 1 ? DOTDOT_OP : COMMADOTDOT_OP));

        // OK
      } else {
        while (have((TOKEN)',')) {
          expr(0);
          N = N + 1;
        }
        plant1(FORMLIST_C, (LIST)N);
        // OK
      }
      check((TOKEN)']');
    }
  else if (have((TOKEN)'{')) {
    // ZF expressions bug?
    word N = 0;
    LIST HOLD = TOKENS;
    perform_alpha_conversions();
    expr(0);
    // implicit zf body no longer legal
    // if ( HD(TOKENS)==BACKARROW_SY ) TOKENS=HOLD; else
    check((TOKEN)';');
    do
      N = N + qualifier();
    while (have((TOKEN)';'));
    // OK
    plant1(FORMZF_C, (LIST)N);
    check((TOKEN)'}');
  } else if (have((TOKEN)'\'')) {
    // operator denotation
    if (have((TOKEN)'#')) {
      plant1(LOAD_C, (LIST)LENGTH_OP);
    } else if (have((TOKEN)'\\') || have((TOKEN)'~')) {
      plant1(LOAD_C, (LIST)NOT_OP);
    } else {
      OPERATOR OP = mkinfix((TOKEN)(HD(TOKENS)));
      if (isinfix((LIST)OP)) {
        TOKENS = TL(TOKENS);
      } else {
        // missing infix or prefix operator
        syntax();
      }
      plant1(LOAD_C, (LIST)QUOTE_OP);
      plant1(LOAD_C, (LIST)OP);
      plant0(APPLY_C);
    }
    check((TOKEN)'\'');
  } else
    // missing identifier|constant|(|[|{
    syntax();
}

static void compilename(ATOM N) {
  word I = 0;
  while (!(I > ENVP || ENV[I] == (LIST)N)) {
    I = I + 1;
  }

  if (I > ENVP) {
    plant1(LOAD_C, (LIST)N);
  } else {
    // OK
    plant1(LOADARG_C, (LIST)I);
  }
}

static word qualifier() {
  // what about more general formals?
  if (isgenerator(TL(TOKENS))) {
    word N = 0;

    do {
      haveid();
      plant1(LOAD_C, (LIST)THE_ID);
      N = N + 1;
    } while (have((TOKEN)','));

    check(BACKARROW_SY);
    expr(0);
    plant1(APPLYINFIX_C, (LIST)GENERATOR);

    if (N > 1) {
      // OK
      plant1(CONT_GENERATOR_C, (LIST)(N - 1));
    }

    return N;
  } else {
    expr(0);
    return 1;
  }
}

// also recognises the "such that" bar and converts it to ';'
// to distinguish it from "or"
static void perform_alpha_conversions() {
  LIST P = TOKENS;
  while (!(HD(P) == (TOKEN)'}' || HD(P) == (TOKEN)']' || HD(P) == EOL)) {
    if (HD(P) == (TOKEN)'[' || HD(P) == (TOKEN)'{') {
      P = skipchunk(P);
      continue;
    }

    if (HD(P) == (TOKEN)'|' && isid(HD(TL(P))) && isgenerator(TL(TL(P)))) {
      HD(P) = (TOKEN)';';
    }

    if (isid(HD(P)) && isgenerator(TL(P))) {
      alpha_convert(HD(P), TL(P));
    }

    P = TL(P);
  }
}

bool isid(LIST X) { return ISCONS(X) && HD(X) == IDENT; }

static bool isgenerator(LIST T) {
  return !ISCONS(T) ? false
                    : HD(T) == BACKARROW_SY ||
                          (HD(T) == (TOKEN)',' && isid(HD(TL(T))) &&
                           isgenerator(TL(TL(T))));
}

static void alpha_convert(LIST VAR, LIST P) {
  LIST T = TOKENS;
  LIST VAR1 = cons((LIST)ALPHA, TL(VAR));
  LIST EDGE = T;
  while (
      !(HD(EDGE) == (TOKEN)';' || HD(EDGE) == BACKARROW_SY || HD(EDGE) == EOL))
    EDGE = skipchunk(EDGE);
  while (!(T == EDGE)) {
    conv1(T, VAR, VAR1);
    T = TL(T);
  }
  T = P;

  while (!(HD(T) == (TOKEN)';' || HD(T) == EOL)) {
    T = skipchunk(T);
  }

  EDGE = T;
  while (
      !(HD(EDGE) == (TOKEN)'}' || HD(EDGE) == (TOKEN)']' || HD(EDGE) == EOL)) {
    EDGE = skipchunk(EDGE);
  }

  while (!(T == EDGE)) {
    conv1(T, VAR, VAR1);
    T = TL(T);
  }
  TL(VAR) = VAR1;
}

static LIST skipchunk(LIST P) {
  word KET = HD(P) == (TOKEN)'{' ? '}' : HD(P) == (TOKEN)'[' ? ']' : -1;
  P = TL(P);

  if (KET == -1) {
    return P;
  }

  // OK
  while (!(HD(P) == (LIST)KET || HD(P) == EOL)) {
    P = skipchunk(P);
  }

  if (!(HD(P) == EOL)) {
    P = TL(P);
  }

  return (P);
}

static void conv1(LIST T, LIST VAR, LIST VAR1) {
  if (EQUAL(HD(T), VAR) && HD(T) != VAR) {
    TL(HD(T)) = VAR1;
  }
}

static LIST formal() {
  if (haveid()) {
    return (LIST)THE_ID;
  } else if (haveconst()) {
    return internalise(THE_CONST);
  } else if (have((TOKEN)'(')) {
    LIST P = pattern();
    check((TOKEN)')');
    return P;
  } else if (have((TOKEN)'[')) {
    LIST PLIST = NIL, P = NIL;

    if (have((TOKEN)']')) {
      return NIL;
    }

    do {
      PLIST = cons(pattern(), PLIST);
    } while (have((TOKEN)','));
    // note they are in reverse order

    check((TOKEN)']');

    while (!(PLIST == NIL)) {
      P = cons((TOKEN)COLON_OP, cons(HD(PLIST), P));
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

static LIST internalise(LIST VAL) {
  return VAL == TL(TRUTH)
             ? TRUTH
             : VAL == TL(FALSITY) ? FALSITY
                                  : ISATOM(VAL) ? cons((LIST)QUOTE, VAL) : VAL;
}

static LIST pattern() {
  LIST P = formal();

  if (have((TOKEN)':')) {
    P = cons((LIST)COLON_OP, cons(P, pattern()));
  }

  return P;
}

static void compilelhs(LIST LHS, word NARGS) {
  word I;
  ENVP = NARGS - 1;
  for (I = 1; I <= NARGS; I++) {
    ENV[NARGS - I] = TL(LHS);
    LHS = HD(LHS);
  }
  for (I = 0; I <= NARGS - 1; I++) {
    compileformal(ENV[I], I);
  }
}

static void compileformal(LIST X, word I) {

  // identifier
  if (ISATOM(X)) {
    word J = 0;

    while (!(J >= I || ENV[J] == X)) {
      // is this a repeated name?
      J = J + 1;
    }

    if (J >= I) {
      // no, no code compiled
      return;
    } else {
      plant2(MATCHARG_C, (LIST)I, (LIST)J);
    }

  } else if (ISNUM(X) || X == NIL || (ISCONS(X) && HD(X) == (LIST)QUOTE)) {
    plant2(MATCH_C, (LIST)I, X);
  } else if (ISCONS(X) && HD(X) == (TOKEN)COLON_OP && ISCONS(TL(X))) {
    // OK
    plant1(MATCHPAIR_C, (LIST)I);
    ENVP = ENVP + 2;
    {
      word A = ENVP - 1, B = ENVP;
      ENV[A] = HD(TL(X)), ENV[B] = TL(TL(X));
      compileformal(ENV[A], A);
      compileformal(ENV[B], B);
    }
  } else {
    bcpl_WRITES("Impossible event in \"compileformal\"\n");
  }
}

// plant stores INSTRUCTIONs and their operands in the code vector
// OP is always an instruction code (*_C);
// A and B can be operators (*_OP), INTs, CONSTs, IDs (names) or
// the address of a C function - all are mapped to LIST type.

// APPLY_C IF_C STOP_C
static void plant0(INSTRUCTION OP) { CODEV = cons((LIST)OP, CODEV); }

// everything else
static void plant1(INSTRUCTION OP, LIST A) {
  CODEV = cons((LIST)OP, CODEV);
  CODEV = cons(A, CODEV);
}

// MATCH_C MATCHARG_C
static void plant2(INSTRUCTION OP, LIST A, LIST B) {
  CODEV = cons((LIST)OP, CODEV);
  CODEV = cons(A, CODEV);
  CODEV = cons(B, CODEV);
}

// flushes the code buffer
static LIST collectcode() {
  LIST TMP = CODEV;
  CODEV = NIL;

  return REVERSE(TMP);
}

// mark elements in CODEV and ENV for preservation by the GC.
// this routine should be called by your BASES() function.
void compiler_bases(void (*F)(LIST *)) {
  word I;

  F(&CODEV);
  // ENVP indexes the last used element and starts as -1.
  for (I = 0; I <= ENVP; I++) {
    F(&ENV[I]);
  }
}
