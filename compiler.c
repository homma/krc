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
static bool isop(list X);
static bool isinfix(list X);
static bool isrelop(list X);
static word diprio(operator op);
static operator mkinfix(token T);
static void printzf_exp(list X);
static bool islistexp(list E);
static bool isrelation(list X);
static bool isrelation_beginning(list A, list X);
static word leftprec(operator op);
static word rightprec(operator op);
static bool rotate(list E);
static bool parmy(list X);
static list rest(list C);
static list subtract(list X, list Y);
static void expr(word N);
static bool startformal(token T);
static bool startsimple(token T);
static void combn(void);
static void simple(void);
static void compilename(atom N);
static word qualifier(void);
static void perform_alpha_conversions();
static bool isgenerator(list T);
static void alpha_convert(list VAR, list P);
static list skipchunk(list P);
static void conv1(list T, list VAR, list VAR1);
static list formal(void);
static list internalise(list VAL);
static list pattern(void);
static void compilelhs(list LHS, word NARGS);
static void compileformal(list X, word I);
static void plant0(instruction op);
static void plant1(instruction op, list A);
static void plant2(instruction op, list A, list B);
static list collectcode(void);

// global variables
void (*TRUEWRCH)(word C) = bcpl_wrch;
list LASTLHS = NIL;
list TRUTH, FALSITY, INFINITY;

// setup_infixes() - interesting elements start at [1]
// the indices correspond to the operator values in compiler.h
// EQ_SY was (token)'=', changed DT May 2015
static token INFIXNAMEVEC[] = {
    (token)0,   (token)':', PLUSPLUS_SY, DASHDASH_SY, (token)'|',
    (token)'&', (token)'>', GE_SY,       NE_SY,       EQ_SY,
    LE_SY,      (token)'<', (token)'+',  (token)'-',  (token)'*',
    (token)'/', (token)'%', STARSTAR_SY, (token)'.',
};
static word INFIXPRIOVEC[] = {0, 0, 0, 0, 1, 2, 3, 3, 3, 3,
                              3, 3, 4, 4, 5, 5, 5, 6, 6};

// bases for garbage collection
// store for opcodes and ther params, which may be operators,
// various CONStructs or the addresses of C functions.
static list CODEV = NIL;

// appears to be a store for formal parameters
static list ENV[100];

static word ENVP;

void init_codev() {
  ENVP = -1;
  CODEV = NIL;
}

static bool isop(list X) {
  return X == (list)ALPHA || X == (list)INDIR ||
         ((list)QUOTE <= X && X <= (list)QUOTE_OP);
}

static bool isinfix(list X) { return (list)COLON_OP <= X && X <= (list)DOT_OP; }

static bool isrelop(list X) { return (list)GR_OP <= X && X <= (list)LS_OP; }

// return the priority of an operator from its index in INFIX*
static word diprio(operator op) { return op == -1 ? -1 : INFIXPRIOVEC[op]; }

// takes a token , returns an operator
// else -1 if t not the name of an infix
static operator mkinfix(token T) {
  word I = 1;
  if (T == (token)'=') {
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
void printexp(list E, word N) {
  if (E == NIL) {
    bcpl_writes("[]");
  } else if (isatom(E)) {
    bcpl_writes(NAME((atom)E));
  } else if (isnum(E)) {
    word X = getnum(E);
    if (X < 0 && N > 5) {
      wrch('(');
      bcpl_writen(X);
      wrch(')');
    } else {
      bcpl_writen(X);
    }
  } else {
    if (!(iscons(E))) {
      if (E == (list)NOT_OP) {
        bcpl_writes("'\\'");
      } else if (E == (list)LENGTH_OP) {
        bcpl_writes("'#'");
      } else {
        fprintf(bcpl_OUTPUT, "<internal value:%p>", E);
      }
      return;
    }
    {
      // maybe could be operator
      list op = HD(E);
      if (!isop(op) && N <= 7) {
        printexp(op, 7);
        wrch(' ');
        printexp(TL(E), 8);
      } else if (op == (list)QUOTE) {
        printatom((atom)TL(E), true);
      } else if (op == (list)INDIR || op == (list)ALPHA) {
        printexp(TL(E), N);
      } else if (op == (list)DOTDOT_OP || op == (list)COMMADOTDOT_OP) {
        wrch('[');
        E = TL(E);
        printexp(HD(E), 0);
        if (op == (list)COMMADOTDOT_OP) {
          wrch(',');
          E = TL(E);
          printexp(HD(E), 0);
        }
        bcpl_writes("..");
        if (!(TL(E) == INFINITY)) {
          printexp(TL(E), 0);
        }
        wrch(']');
      } else if (op == (list)ZF_OP) {
        wrch('{');
        printzf_exp(TL(E));
        wrch('}');
      } else if (op == (list)NOT_OP && N <= 3) {
        wrch('\\');
        printexp(TL(E), 3);
      } else if (op == (list)NEG_OP && N <= 5) {
        wrch('-');
        printexp(TL(E), 5);
      } else if (op == (list)LENGTH_OP && N <= 7) {
        wrch('#');
        printexp(TL(E), 7);
      } else if (op == (list)QUOTE_OP) {
        wrch('\'');
        if (TL(E) == (list)LENGTH_OP) {
          wrch('#');
        } else if (TL(E) == (list)NOT_OP) {
          wrch('\\');
        } else {
          writetoken(INFIXNAMEVEC[(word)TL(E)]);
        }
        wrch('\'');
      } else if (islistexp(E)) {
        wrch('[');
        while (!(E == NIL)) {
          printexp(HD(TL(E)), 0);
          if (!(TL(TL(E)) == NIL)) {
            wrch(',');
          }
          E = TL(TL(E));
        }
        wrch(']');
      } else if (op == (list)AND_OP && N <= 3 && rotate(E) &&
                 isrelation(HD(TL(E))) &&
                 isrelation_beginning(TL(TL(HD(TL(E)))), TL(TL(E)))) {
        // continued relations
        printexp(HD(TL(HD(TL(E)))), 4);
        wrch(' ');
        writetoken(INFIXNAMEVEC[(word)HD(HD(TL(E)))]);
        wrch(' ');
        printexp(TL(TL(E)), 2);
      } else if (isinfix(op) && INFIXPRIOVEC[(word)op] >= N) {
        printexp(HD(TL(E)), leftprec((operator) op));
        if (!(op == (list)COLON_OP)) {
          // DOT.OP should be spaced, DT 2015
          wrch(' ');
        }
        writetoken(INFIXNAMEVEC[(word)op]);
        if (!(op == (list)COLON_OP)) {
          wrch(' ');
        }
        printexp(TL(TL(E)), rightprec((operator) op));
      } else {
        wrch('(');
        printexp(E, 0);
        wrch(')');
      }
    }
  }
}

static void printzf_exp(list X) {
  list Y = X;
  while (!(TL(Y) == NIL)) {
    Y = TL(Y);
  }

  // body
  printexp(HD(Y), 0);

  // print "such that" as bar if a generator directly follows
  if (iscons(HD(X)) && HD(HD(X)) == (list)GENERATOR) {
    wrch('|');
  } else {
    wrch(';');
  }
  while (!(TL(X) == NIL)) {
    list qualifier = HD(X);

    if (iscons(qualifier) && HD(qualifier) == (list)GENERATOR) {
      printexp(HD(TL(qualifier)), 0);

      // deals with repeated generators
      while (iscons(TL(X)) &&
#ifdef INSTRUMENT_KRC_GC
             iscons(HD(TL(X))) &&
#endif
             HD(HD(TL(X))) == (list)GENERATOR &&
             equal(TL(TL(HD(TL(X)))), TL(TL(qualifier)))) {
        X = TL(X);
        qualifier = HD(X);
        wrch(',');
        printexp(HD(TL(qualifier)), 0);
      }
      bcpl_writes("<-");
      printexp(TL(TL(qualifier)), 0);
    } else {
      printexp(qualifier, 0);
    }
    X = TL(X);
    if (!(TL(X) == NIL)) {
      wrch(';');
    }
  }
}

static bool islistexp(list E) {
  while (iscons(E) && HD(E) == (list)COLON_OP) {
    list E1 = TL(TL(E));

    while (iscons(E1) && HD(E1) == (list)INDIR) {
      E1 = TL(E1);
    }

    TL(TL(E)) = E1;
    E = E1;
  }
  return E == NIL;
}

static bool isrelation(list X) { return iscons(X) && isrelop(HD(X)); }

static bool isrelation_beginning(list A, list X) {
  return (isrelation(X) && equal(HD(TL(X)), A)) ||
         (iscons(X) && HD(X) == (list)AND_OP &&
          isrelation_beginning(A, HD(TL(X))));
}

static word leftprec(operator op) {
  return op == COLON_OP || op == APPEND_OP || op == LISTDIFF_OP ||
                 op == AND_OP || op == OR_OP || op == EXP_OP ||
                 isrelop((list)op)
             ? INFIXPRIOVEC[op] + 1
             : INFIXPRIOVEC[op];
}

// relops are non-associative
// colon, append, and, or are right-associative
// all other infixes are left-associative

static word rightprec(operator op) {
  return op == COLON_OP || op == APPEND_OP || op == LISTDIFF_OP ||
                 op == AND_OP || op == OR_OP || op == EXP_OP
             ? INFIXPRIOVEC[op]
             : INFIXPRIOVEC[op] + 1;
}

// puts nested and's into rightist form to ensure
// detection of continued relations
static bool rotate(list E) {
  while (iscons(HD(TL(E))) && HD(HD(TL(E))) == (list)AND_OP) {
    list X = TL(HD(TL(E))), C = TL(TL(E));
    list A = HD(X), B = TL(X);
    HD(TL(E)) = A, TL(TL(E)) = cons((list)AND_OP, cons(B, C));
  }
  return true;
}

// decompiler

// the val field of each user defined name
// contains - cons(cons(nargs,comment),<list of eqns>)
void display(atom ID, bool WITHNOS, bool DOUBLESPACING) {
  if (VAL(ID) == NIL) {
    fprintf(bcpl_OUTPUT, "\"%s\" - not defined\n", NAME(ID));
    return;
  }
  {
    list X = HD(VAL(ID)), EQNS = TL(VAL(ID));
    word NARGS = (word)(HD(X));
    list COMMENT = TL(X);
    word N = length(EQNS), I;
    LASTLHS = NIL;
    if (!(COMMENT == NIL)) {
      list C = COMMENT;
      fprintf(bcpl_OUTPUT, "    %s :-", NAME(ID));
      while (!(C == NIL)) {
        bcpl_writes(NAME((atom)HD(C)));
        C = TL(C);
        if (!(C == NIL)) {
          wrch('\n');
          if (DOUBLESPACING) {
            wrch('\n');
          }
        }
      }
      bcpl_writes(";\n");
      if (DOUBLESPACING) {
        wrch('\n');
      }
    }
    if (COMMENT != NIL && N == 1 && HD(TL(HD(EQNS))) == (list)CALL_C) {
      return;
    }
    for (I = 1; I <= N; I++) {

      if (WITHNOS && (N > 1 || COMMENT != NIL)) {
        fprintf(bcpl_OUTPUT, "%2" W ") ", I);
      } else {
        bcpl_writes("    ");
      }

      removelineno(HD(EQNS));
      displayeqn(ID, NARGS, HD(EQNS));
      if (DOUBLESPACING) {
        wrch('\n');
      }
      EQNS = TL(EQNS);
    }
  }
}

static void shch(word CH) { TRUEWRCH(' '); }

// equation decoder
void displayeqn(atom ID, word NARGS, list EQN) {
  list LHS = HD(EQN), CODE = TL(EQN);

  if (NARGS == 0) {
    bcpl_writes(NAME(ID));
    LASTLHS = (list)ID;
  } else {

    if (equal(LHS, LASTLHS)) {
      wrch = shch;
    } else {
      LASTLHS = LHS;
    }
    printexp(LHS, 0);
    wrch = TRUEWRCH;
  }

  bcpl_writes(" = ");

  if (HD(CODE) == (list)CALL_C) {
    bcpl_writes("<primitive function>");
  } else {
    displayrhs(LHS, NARGS, CODE);
  }

  wrch('\n');
}

void displayrhs(list LHS, word NARGS, list CODE) {
  list V[100];
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
        V[I] = cons((list)COLON_OP, cons(V[I], V[I + 1]));
      }
      break;
    case FORMZF_C:
      CODE = TL(CODE);
      I = I - (word)(HD(CODE));
      V[I] = cons(V[I], NIL);
      for (J = (word)(HD(CODE)); J >= 1; J = J - 1)
        V[I] = cons(V[I + J], V[I]);
      V[I] = cons((list)ZF_OP, V[I]);
      break;
    case CONT_GENERATOR_C:
      CODE = TL(CODE);
      for (J = 1; J <= (word)(HD(CODE)); J++)
        V[I - J] = cons((list)GENERATOR, cons(V[I - J], TL(TL(V[I]))));
      break;
    case MATCH_C:
    case MATCHARG_C:
      CODE = TL(CODE);
      CODE = TL(CODE);
      break;
    case MATCHPAIR_C:
      CODE = TL(CODE);
      {
        list X = V[(word)HD(CODE)];
        I = I + 2;
        V[I - 1] = HD(TL(X)), V[I] = TL(TL(X));
      }
      break;
    case STOP_C:
      printexp(V[I], 0);
      if (!(IF_FLAG)) {
        return;
      }
      bcpl_writes(", ");
      printexp(V[I - 1], 0);
      return;
    default:
      bcpl_writes("IMPOSSIBLE INSTRUCTION IN \"displayrhs\"\n");
    }
    // end of switch
    CODE = TL(CODE);
  } while (1);
}

// extracts that part of the code which
// determines which cases this equation applies to
list profile(list EQN) {
  list CODE = TL(EQN);
  if (HD(CODE) == (list)LINENO_C) {
    CODE = TL(TL(CODE));
  }
  {
    list C = CODE;
    while (parmy(HD(C)))
      C = rest(C);
    {
      list HOLD = C;
      while (!(HD(C) == (list)IF_C || HD(C) == (list)STOP_C))
        C = rest(C);
      if (HD(C) == (list)IF_C) {
        return subtract(CODE, C);
      } else {
        return subtract(CODE, HOLD);
      }
    }
  }
}

static bool parmy(list X) {
  return X == (list)MATCH_C || X == (list)MATCHARG_C || X == (list)MATCHPAIR_C;
}

// removes one complete instruction from C
static list rest(list C) {
  list X = HD(C);
  C = TL(C);

  if (X == (list)APPLY_C || X == (list)IF_C || X == (list)STOP_C) {
    return C;
  }

  C = TL(C);

  if (!(X == (list)MATCH_C || X == (list)MATCHARG_C)) {
    return C;
  }

  return TL(C);
}

// list subtraction
static list subtract(list X, list Y) {
  list Z = NIL;

  while (!(X == Y)) {
    Z = cons(HD(X), Z), X = TL(X);
  }

  // note the result is reversed - for our purposes this does not matter
  return Z;
}

// called whenever the definiendum is subject of a
// display,reorder or (partial)delete command - has the effect of
// restoring the standard line numbering
void removelineno(list EQN) {
  if (HD(TL(EQN)) == (list)LINENO_C) {
    TL(EQN) = TL(TL(TL(EQN)));
  }
}

// compiler for krc expressions and equations
// renamed from exp as the name conflicts with exp(3)
list expression() {
  init_codev();
  expr(0);
  plant0(STOP_C);
  return collectcode();
}

// returns a triple: cons(subject,cons(nargs,eqn))
list equation() {
  list SUBJECT = 0, LHS = 0;
  word NARGS = 0;
  init_codev();
  if (haveid()) {
    SUBJECT = (list)THE_ID, LHS = (list)THE_ID;
    while (startformal(HD(TOKENS))) {
      LHS = cons(LHS, formal());
      NARGS = NARGS + 1;
    }
  } else if (HD(TOKENS) == (list)'=' && LASTLHS != NIL) {
    SUBJECT = LASTLHS, LHS = LASTLHS;
    while (iscons(SUBJECT))
      SUBJECT = HD(SUBJECT), NARGS = NARGS + 1;
  } else {
    syntax(), bcpl_writes("missing LHS\n");
    return NIL;
  }
  compilelhs(LHS, NARGS);
  {
    list CODE = collectcode();
    check((token)'=');
    expr(0);
    plant0(STOP_C);
    {
      list EXPCODE = collectcode();

      // change from EMAS/KRC to allow guarded simple def
      if (have((token)',')) {
        expr(0);
        plant0(IF_C);
        CODE = append(CODE, append(collectcode(), EXPCODE));
      } else {
        CODE = append(CODE, EXPCODE);
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
      return cons(SUBJECT, cons((list)NARGS, cons(LHS, CODE)));
    }
  }
}

// N is the priority level
static void expr(word N) {
  if (N <= 3 && (have((token)'\\') || have((token)'~'))) {
    plant1(LOAD_C, (list)NOT_OP);
    expr(3);
    plant0(APPLY_C);
  } else if (N <= 5 && have((token)'+')) {
    expr(5);
  } else if (N <= 5 && have((token)'-')) {
    plant1(LOAD_C, (list)NEG_OP);
    expr(5);
    plant0(APPLY_C);
  } else if (have((token)'#')) {
    plant1(LOAD_C, (list)LENGTH_OP);
    combn();
    plant0(APPLY_C);
  } else if (startsimple(HD(TOKENS)))
    combn();
  else {
    syntax();
    return;
  }
  {
    operator op = mkinfix(HD(TOKENS));
    while (diprio(op) >= N) {
      // for continued relations
      word I, AND_COUNT = 0;

      TOKENS = TL(TOKENS);
      expr(rightprec(op));

      if (ERRORFLAG) {
        return;
      }

      while (isrelop((list)op) && isrelop((list)mkinfix(HD(TOKENS)))) {
        // continued relations
        AND_COUNT = AND_COUNT + 1;
        plant1(CONTINUE_INFIX_C, (list)op);
        op = mkinfix(HD(TOKENS));
        TOKENS = TL(TOKENS);
        expr(4);
        if (ERRORFLAG) {
          return;
        }
      }
      plant1(APPLYINFIX_C, (list)op);
      for (I = 1; I <= AND_COUNT; I++) {
        plant1(APPLYINFIX_C, (list)AND_OP);
      }
      // for continued relations
      op = mkinfix(HD(TOKENS));
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

static bool startformal(token T) {
  return iscons(T) ? (HD(T) == IDENT || HD(T) == (list)CONST)
                   : T == (token)'(' || T == (token)'[' || T == (token)'-';
}

static bool startsimple(token T) {
  return iscons(T) ? (HD(T) == IDENT || HD(T) == (list)CONST)
                   : T == (token)'(' || T == (token)'[' || T == (token)'{' ||
                         T == (token)'\'';
}

static void simple() {
  if (haveid()) {
    compilename(THE_ID);
  } else if (haveconst()) {
    plant1(LOAD_C, (list)internalise(THE_CONST));
  } else if (have((token)'(')) {
    expr(0);
    check((token)')');
  } else if (have((token)'['))
    if (have((token)']')) {
      plant1(LOAD_C, NIL);
    } else {
      word N = 1;
      expr(0);
      if (have((token)',')) {
        expr(0);
        N = N + 1;
      }
      if (have(DOTDOT_SY)) {
        if (HD(TOKENS) == (token)']') {
          plant1(LOAD_C, INFINITY);
        } else {
          expr(0);
        }

        if (N == 2) {
          plant0(APPLY_C);
        }

        plant1(APPLYINFIX_C, (list)(N == 1 ? DOTDOT_OP : COMMADOTDOT_OP));

        // OK
      } else {
        while (have((token)',')) {
          expr(0);
          N = N + 1;
        }
        plant1(FORMLIST_C, (list)N);
        // OK
      }
      check((token)']');
    }
  else if (have((token)'{')) {
    // ZF expressions bug?
    word N = 0;
    list HOLD = TOKENS;
    perform_alpha_conversions();
    expr(0);
    // implicit zf body no longer legal
    // if ( HD(TOKENS)==BACKARROW_SY ) TOKENS=HOLD; else
    check((token)';');
    do
      N = N + qualifier();
    while (have((token)';'));
    // OK
    plant1(FORMZF_C, (list)N);
    check((token)'}');
  } else if (have((token)'\'')) {
    // operator denotation
    if (have((token)'#')) {
      plant1(LOAD_C, (list)LENGTH_OP);
    } else if (have((token)'\\') || have((token)'~')) {
      plant1(LOAD_C, (list)NOT_OP);
    } else {
      operator op = mkinfix((token)(HD(TOKENS)));
      if (isinfix((list)op)) {
        TOKENS = TL(TOKENS);
      } else {
        // missing infix or prefix operator
        syntax();
      }
      plant1(LOAD_C, (list)QUOTE_OP);
      plant1(LOAD_C, (list)op);
      plant0(APPLY_C);
    }
    check((token)'\'');
  } else
    // missing identifier|constant|(|[|{
    syntax();
}

static void compilename(atom N) {
  word I = 0;
  while (!(I > ENVP || ENV[I] == (list)N)) {
    I = I + 1;
  }

  if (I > ENVP) {
    plant1(LOAD_C, (list)N);
  } else {
    // OK
    plant1(LOADARG_C, (list)I);
  }
}

static word qualifier() {
  // what about more general formals?
  if (isgenerator(TL(TOKENS))) {
    word N = 0;

    do {
      haveid();
      plant1(LOAD_C, (list)THE_ID);
      N = N + 1;
    } while (have((token)','));

    check(BACKARROW_SY);
    expr(0);
    plant1(APPLYINFIX_C, (list)GENERATOR);

    if (N > 1) {
      // OK
      plant1(CONT_GENERATOR_C, (list)(N - 1));
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
  list P = TOKENS;
  while (!(HD(P) == (token)'}' || HD(P) == (token)']' || HD(P) == EOL)) {
    if (HD(P) == (token)'[' || HD(P) == (token)'{') {
      P = skipchunk(P);
      continue;
    }

    if (HD(P) == (token)'|' && isid(HD(TL(P))) && isgenerator(TL(TL(P)))) {
      HD(P) = (token)';';
    }

    if (isid(HD(P)) && isgenerator(TL(P))) {
      alpha_convert(HD(P), TL(P));
    }

    P = TL(P);
  }
}

bool isid(list X) { return iscons(X) && HD(X) == IDENT; }

static bool isgenerator(list T) {
  return !iscons(T) ? false
                    : HD(T) == BACKARROW_SY ||
                          (HD(T) == (token)',' && isid(HD(TL(T))) &&
                           isgenerator(TL(TL(T))));
}

static void alpha_convert(list VAR, list P) {
  list T = TOKENS;
  list VAR1 = cons((list)ALPHA, TL(VAR));
  list EDGE = T;
  while (
      !(HD(EDGE) == (token)';' || HD(EDGE) == BACKARROW_SY || HD(EDGE) == EOL))
    EDGE = skipchunk(EDGE);
  while (!(T == EDGE)) {
    conv1(T, VAR, VAR1);
    T = TL(T);
  }
  T = P;

  while (!(HD(T) == (token)';' || HD(T) == EOL)) {
    T = skipchunk(T);
  }

  EDGE = T;
  while (
      !(HD(EDGE) == (token)'}' || HD(EDGE) == (token)']' || HD(EDGE) == EOL)) {
    EDGE = skipchunk(EDGE);
  }

  while (!(T == EDGE)) {
    conv1(T, VAR, VAR1);
    T = TL(T);
  }
  TL(VAR) = VAR1;
}

static list skipchunk(list P) {
  word KET = HD(P) == (token)'{' ? '}' : HD(P) == (token)'[' ? ']' : -1;
  P = TL(P);

  if (KET == -1) {
    return P;
  }

  // OK
  while (!(HD(P) == (list)KET || HD(P) == EOL)) {
    P = skipchunk(P);
  }

  if (!(HD(P) == EOL)) {
    P = TL(P);
  }

  return (P);
}

static void conv1(list T, list VAR, list VAR1) {
  if (equal(HD(T), VAR) && HD(T) != VAR) {
    TL(HD(T)) = VAR1;
  }
}

static list formal() {
  if (haveid()) {
    return (list)THE_ID;
  } else if (haveconst()) {
    return internalise(THE_CONST);
  } else if (have((token)'(')) {
    list P = pattern();
    check((token)')');
    return P;
  } else if (have((token)'[')) {
    list PLIST = NIL, P = NIL;

    if (have((token)']')) {
      return NIL;
    }

    do {
      PLIST = cons(pattern(), PLIST);
    } while (have((token)','));
    // note they are in reverse order

    check((token)']');

    while (!(PLIST == NIL)) {
      P = cons((token)COLON_OP, cons(HD(PLIST), P));
      PLIST = TL(PLIST);
    }
    // now they are in correct order

    return P;
  } else if (have((token)'-') && havenum()) {
    THE_NUM = -THE_NUM;
    return stonum(THE_NUM);
  } else {
    syntax(); // MISSING identifier|constant|(|[
    return NIL;
  }
}

static list internalise(list VAL) {
  return VAL == TL(TRUTH)
             ? TRUTH
             : VAL == TL(FALSITY) ? FALSITY
                                  : isatom(VAL) ? cons((list)QUOTE, VAL) : VAL;
}

static list pattern() {
  list P = formal();

  if (have((token)':')) {
    P = cons((list)COLON_OP, cons(P, pattern()));
  }

  return P;
}

static void compilelhs(list LHS, word NARGS) {
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

static void compileformal(list X, word I) {

  // identifier
  if (isatom(X)) {
    word J = 0;

    while (!(J >= I || ENV[J] == X)) {
      // is this a repeated name?
      J = J + 1;
    }

    if (J >= I) {
      // no, no code compiled
      return;
    } else {
      plant2(MATCHARG_C, (list)I, (list)J);
    }

  } else if (isnum(X) || X == NIL || (iscons(X) && HD(X) == (list)QUOTE)) {
    plant2(MATCH_C, (list)I, X);
  } else if (iscons(X) && HD(X) == (token)COLON_OP && iscons(TL(X))) {
    // OK
    plant1(MATCHPAIR_C, (list)I);
    ENVP = ENVP + 2;
    {
      word A = ENVP - 1, B = ENVP;
      ENV[A] = HD(TL(X)), ENV[B] = TL(TL(X));
      compileformal(ENV[A], A);
      compileformal(ENV[B], B);
    }
  } else {
    bcpl_writes("Impossible event in \"compileformal\"\n");
  }
}

// plant stores instructions and their operands in the code vector
// op is always an instruction code (*_C);
// A and B can be operators (*_op), INTs, CONSTs, IDs (names) or
// the address of a C function - all are mapped to list type.

// APPLY_C IF_C STOP_C
static void plant0(instruction op) { CODEV = cons((list)op, CODEV); }

// everything else
static void plant1(instruction op, list A) {
  CODEV = cons((list)op, CODEV);
  CODEV = cons(A, CODEV);
}

// MATCH_C MATCHARG_C
static void plant2(instruction op, list A, list B) {
  CODEV = cons((list)op, CODEV);
  CODEV = cons(A, CODEV);
  CODEV = cons(B, CODEV);
}

// flushes the code buffer
static list collectcode() {
  list TMP = CODEV;
  CODEV = NIL;

  return reverse(TMP);
}

// mark elements in CODEV and ENV for preservation by the GC.
// this routine should be called by your bases() function.
void compiler_bases(void (*F)(list *)) {
  word I;

  F(&CODEV);
  // ENVP indexes the last used element and starts as -1.
  for (I = 0; I <= ENVP; I++) {
    F(&ENV[I]);
  }
}
