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
static bool isop(list x);
static bool isinfix(list x);
static bool isrelop(list x);
static word diprio(operator op);
static operator mkinfix(token t);
static void printzf_exp(list x);
static bool islistexp(list e);
static bool isrelation(list x);
static bool isrelation_beginning(list a, list x);
static bool is_non_associative(operator op);
static bool is_right_associative(operator op);
static bool is_left_associative(operator op);
static word leftprec(operator op);
static word rightprec(operator op);
static bool rotate(list e);
static bool parmy(list x);
static list rest(list c);
static list subtract(list x, list y);
static void expr(word n);
static bool startformal(token t);
static bool startsimple(token t);
static void combn(void);
static void simple(void);
static void compilename(atom n);
static word qualifier(void);
static void perform_alpha_conversions();
static bool isgenerator(list t);
static void alpha_convert(list var, list p);
static list skipchunk(list P);
static void conv1(list T, list var, list var1);
static list formal(void);
static list internalise(list val);
static list pattern(void);
static void compilelhs(list lhs, word nargs);
static void compileformal(list x, word i);
static void plant0(instruction op);
static void plant1(instruction op, list a);
static void plant2(instruction op, list a, list b);
static list collectcode(void);

// global variables
void (*TRUEWRCH)(word c) = bcpl_wrch;
list LASTLHS = NIL;
list TRUTH;
list FALSITY;
list INFINITY;

// infix operator names
//
// interesting elements start at [1]
// the indices correspond to the operator values in compiler.h
// EQ_SY was (token)'=', changed DT May 2015
// renamed from INFIXNAMEVEC
static token infix_names[] = {
    (token)0,   (token)':', PLUSPLUS_SY, DASHDASH_SY, (token)'|',
    (token)'&', (token)'>', GE_SY,       NE_SY,       EQ_SY,
    LE_SY,      (token)'<', (token)'+',  (token)'-',  (token)'*',
    (token)'/', (token)'%', STARSTAR_SY, (token)'.',
};

// infix operator priorities
//
// renamed from INIFXPRIOVEC
static word infix_pri[] = {0, 0, 0, 0, 1, 2, 3, 3, 3, 3,
                           3, 3, 4, 4, 5, 5, 5, 6, 6};

// code vector
//
// bases for garbage collection
// store for opcodes and their params, which may be operators,
// various CONStructs or the addresses of C functions.
static list CODEV = NIL;

// appears to be a store for formal parameters
static list ENV[100];

static word ENVP;

void init_codev() {
  ENVP = -1;
  CODEV = NIL;
}

// check if x is an operator
static bool isop(list x) {

  // return x == (list)ALPHA || x == (list)INDIR ||
  //        ((list)QUOTE <= x && x <= (list)QUOTE_OP);

  // check if x is in operator enum - see compiler.h
  return (list)ALPHA <= x && x <= (list)QUOTE_OP;
}

// check if x is an infix operator
static bool isinfix(list x) { return (list)COLON_OP <= x && x <= (list)DOT_OP; }

// check if x is a relational operator
static bool isrelop(list x) { return (list)GR_OP <= x && x <= (list)LS_OP; }

// return the priority of an operator from its index in infix_pri
static word diprio(operator op) { return op == -1 ? -1 : infix_pri[op]; }

// make infix operator
//
// takes a token, returns an operator
// else -1 if t not the name of an infix
static operator mkinfix(token t) {

  // legacy, accept "=" for "=="
  if (t == (token)'=') {
    return EQ_OP;
  }

  // search t in infix_names
  // infix operator's range is (COLON_OP(1) .. DOT_OP(18))
  word i = 1;
  while (!(i > DOT_OP || infix_names[i] == t)) {
    i++;
  }

  // not found
  if (i > DOT_OP) {
    return -1;
  }

  return i;
}

// print expression
//
// n is the priority level
void printexp(list e, word n) {

  if (e == NIL) {
    // empty

    bcpl_writes("[]");

  } else if (isatom(e)) {
    // atom

    bcpl_writes(NAME((atom)e));

  } else if (isnum(e)) {
    // number

    word x = getnum(e);
    if (x < 0 && n > 5) {

      wrch('(');
      bcpl_writen(x);
      wrch(')');

    } else {

      bcpl_writen(x);
    }

  } else {

    if (!iscons(e)) {

      if (e == (list)NOT_OP) {
        // NOT op

        bcpl_writes("'\\'");

      } else if (e == (list)LENGTH_OP) {
        // length op

        bcpl_writes("'#'");

      } else {

        fprintf(bcpl_OUTPUT, "<internal value:%p>", e);
      }
      return;
    }

    {
      // maybe could be operator

      list op = getop(e);

      if (!isop(op) && n <= 7) {

        printexp(op, 7);
        wrch(' ');
        printexp(next(e), 8);

      } else if (op == (list)QUOTE) {

        printatom((atom)getval(e), true);

      } else if (op == (list)INDIR || op == (list)ALPHA) {

        printexp(TL(e), n);

      } else if (op == (list)DOTDOT_OP || op == (list)COMMADOTDOT_OP) {

        wrch('[');
        e = next(e);
        printexp(gettoken(e), 0);

        if (op == (list)COMMADOTDOT_OP) {
          wrch(',');
          e = next(e);
          printexp(gettoken(e), 0);
        }

        bcpl_writes("..");

        if (next(e) != INFINITY) {
          printexp(next(e), 0);
        }

        wrch(']');

      } else if (op == (list)ZF_OP) {

        wrch('{');
        printzf_exp(next(e));
        wrch('}');

      } else if (op == (list)NOT_OP && n <= 3) {

        wrch('\\');
        printexp(next(e), 3);

      } else if (op == (list)NEG_OP && n <= 5) {

        wrch('-');
        printexp(next(e), 5);

      } else if (op == (list)LENGTH_OP && n <= 7) {

        wrch('#');
        printexp(next(e), 7);

      } else if (op == (list)QUOTE_OP) {
        // '#', '\', 'op'

        wrch('\'');

        if (next(e) == (list)LENGTH_OP) {
          wrch('#');
        } else if (next(e) == (list)NOT_OP) {
          wrch('\\');
        } else {
          writetoken(infix_names[(word)next(e)]);
        }

        wrch('\'');

      } else if (islistexp(e)) {

        wrch('[');

        while (e != NIL) {

          printexp(gettoken(next(e)), 0);

          // first time : [ next , next_next
          // after that : [ e , next , next_next
          if (next(next(e)) != NIL) {
            wrch(',');
          }

          e = next(next(e));
        }

        wrch(']');

      } else if (op == (list)AND_OP && n <= 3 && rotate(e) &&
                 isrelation(getop(next(e))) &&
                 isrelation_beginning(TL(TL(HD(TL(e)))), TL(TL(e)))) {

        // continued relations

        printexp(HD(TL(HD(TL(e)))), 4);
        wrch(' ');
        writetoken(infix_names[(word)HD(HD(TL(e)))]);
        wrch(' ');
        printexp(TL(TL(e)), 2);

      } else if (isinfix(op) && infix_pri[(word)op] >= n) {

        printexp(HD(TL(e)), leftprec((operator) op));

        // DOT_OP should be spaced, DT 2015
        if (op != (list)COLON_OP) {
          wrch(' ');
        }

        writetoken(infix_names[(word)op]);

        if (op != (list)COLON_OP) {
          wrch(' ');
        }

        printexp(TL(TL(e)), rightprec((operator) op));

      } else {

        wrch('(');
        printexp(e, 0);
        wrch(')');
      }
    }
  }
}

// print ZF expression
static void printzf_exp(list x) {

  list y = x;

  while (TL(y) != NIL) {
    y = TL(y);
  }

  // body
  printexp(HD(y), 0);

  // print "such that" as bar '|' if a generator directly follows
  if (iscons(HD(x)) && HD(HD(x)) == (list)GENERATOR) {
    wrch('|');
  } else {
    wrch(';');
  }

  while (TL(x) != NIL) {

    list qualifier = HD(x);

    if (iscons(qualifier) && HD(qualifier) == (list)GENERATOR) {

      printexp(HD(TL(qualifier)), 0);

      // deals with repeated generators
      while (iscons(TL(x)) &&
#ifdef INSTRUMENT_KRC_GC
             iscons(HD(TL(x))) &&
#endif
             HD(HD(TL(x))) == (list)GENERATOR &&
             equal(TL(TL(HD(TL(x)))), TL(TL(qualifier)))) {
        x = TL(x);
        qualifier = HD(x);
        wrch(',');
        printexp(HD(TL(qualifier)), 0);
      }

      bcpl_writes("<-");
      printexp(TL(TL(qualifier)), 0);

    } else {

      printexp(qualifier, 0);
    }

    x = TL(x);

    if (TL(x) != NIL) {
      wrch(';');
    }
  }
}

// check if e is a list expression
static bool islistexp(list e) {

  while (iscons(e) && getop(e) == (list)COLON_OP) {

    list e1 = TL(TL(e));

    while (iscons(e1) && getop(e1) == (list)INDIR) {
      e1 = TL(e1);
    }

    TL(TL(e)) = e1;
    e = e1;
  }

  return e == NIL;
}

// check if x is a relation ('>', '>=', '\=', '==', '~=', '<=', '<')
static bool isrelation(list x) { return iscons(x) && isrelop(HD(x)); }

// called from printexp
static bool isrelation_beginning(list a, list x) {

  // acceptable list: correct?
  // (relop a ...)
  // (& relop a ...)
  // (& & ... relop a ...)

  // isrelation(x) && => x = (relop . _)
  // equeal(HD(TL(x)), a)) => x = (_ . (a . _))
  // => x = (relop . (a . _))

  // iscons(x) && => x = (_ . _)
  // HD(x) == (list)AND_OP && => x = ('&' . _)
  // isrelation_beginning(a, HD(TL(x)))) => x = (_ . (next_x . _))
  // => x = ('&' . (next_x . _))

  return (isrelation(x) && equal(HD(TL(x)), a)) ||
         (iscons(x) && HD(x) == (list)AND_OP &&
          isrelation_beginning(a, HD(TL(x))));
}

// relops are non-associative
static bool is_non_associative(operator op) { return isrelop((list)op); }

// colon, append, listdiff, and, or, exp are right-associative
static bool is_right_associative(operator op) {

  return op == COLON_OP || op == APPEND_OP || op == LISTDIFF_OP ||
         op == AND_OP || op == OR_OP || op == EXP_OP;
}

// all other infixes are left-associative
static bool is_left_associative(operator op) {

  return !is_non_associative(op) && !is_right_associative(op);
}

// operator precedences
//
//   non-associatives : +1 always
// right associatives : +1 when left
//  left associatives : +1 when right

// returns operator precedence
static word leftprec(operator op) {

  if (is_right_associative(op) || is_non_associative(op)) {

    return infix_pri[op] + 1;

  } else {

    return infix_pri[op];
  }

  // return op == COLON_OP || op == APPEND_OP || op == LISTDIFF_OP ||
  //                op == AND_OP || op == OR_OP || op == EXP_OP ||
  //                isrelop((list)op)
  //            ? infix_pri[op] + 1
  //            : infix_pri[op];
}

// returns operator precedence
static word rightprec(operator op) {

  if (is_right_associative(op)) {

    return infix_pri[op];

  } else {

    return infix_pri[op] + 1;
  }

  // return op == COLON_OP || op == APPEND_OP || op == LISTDIFF_OP ||
  //                op == AND_OP || op == OR_OP || op == EXP_OP
  //            ? infix_pri[op]
  //            : infix_pri[op] + 1;
}

// puts nested and's into rightist form to ensure
// detection of continued relations
static bool rotate(list e) {

  while (iscons(HD(TL(e))) && HD(HD(TL(e))) == (list)AND_OP) {

    list x = TL(HD(TL(e)));
    list c = TL(TL(e));
    list a = HD(x);
    list b = TL(x);

    HD(TL(e)) = a, TL(TL(e)) = cons((list)AND_OP, cons(b, c));
  }

  return true;
}

// decompiler

// the val field of each user defined name
// contains - cons(cons(nargs, comment), <list of eqns>)
void display(atom id, bool withnos, bool doublespacing) {

  if (VAL(id) == NIL) {
    fprintf(bcpl_OUTPUT, "\"%s\" - not defined\n", NAME(id));
    return;
  }

  {
    list x = HD(VAL(id));
    list eqns = TL(VAL(id));
    word nargs = (word)(HD(x));
    list comment = TL(x);

    word n = length(eqns);

    LASTLHS = NIL;

    if (comment != NIL) {
      list c = comment;
      fprintf(bcpl_OUTPUT, "    %s :-", NAME(id));

      while (c != NIL) {
        bcpl_writes(NAME((atom)HD(c)));
        c = TL(c);
        if (c != NIL) {
          wrch('\n');
          if (doublespacing) {
            wrch('\n');
          }
        }
      }

      bcpl_writes(";\n");
      if (doublespacing) {
        wrch('\n');
      }
    }

    if (comment != NIL && n == 1 && HD(TL(HD(eqns))) == (list)CALL_C) {
      return;
    }

    for (word i = 1; i <= n; i++) {

      if (withnos && (n > 1 || comment != NIL)) {
        fprintf(bcpl_OUTPUT, "%2" W ") ", i);
      } else {
        bcpl_writes("    ");
      }

      removelineno(HD(eqns));
      displayeqn(id, nargs, HD(eqns));
      if (doublespacing) {
        wrch('\n');
      }
      eqns = TL(eqns);
    }
  }
}

static void shch(word ch) { TRUEWRCH(' '); }

// equation decoder
void displayeqn(atom id, word nargs, list eqn) {

  list lhs = HD(eqn);
  list code = TL(eqn);

  if (nargs == 0) {

    bcpl_writes(NAME(id));
    LASTLHS = (list)id;

  } else {

    if (equal(lhs, LASTLHS)) {
      wrch = shch;
    } else {
      LASTLHS = lhs;
    }
    printexp(lhs, 0);
    wrch = TRUEWRCH;
  }

  bcpl_writes(" = ");

  if (HD(code) == (list)CALL_C) {
    bcpl_writes("<primitive function>");
  } else {
    displayrhs(lhs, nargs, code);
  }

  wrch('\n');
}

void displayrhs(list lhs, word nargs, list code) {

  list v[100];
  word i = nargs;
  bool if_flag = false;

  // unpack formal parameters into v
  while (i > 0) {
    i = i - 1;
    v[i] = TL(lhs);
    lhs = HD(lhs);
  }

  i = nargs - 1;

  do {
    switch ((word)(HD(code))) {
    case LOAD_C:
      code = TL(code);
      i++;
      v[i] = HD(code);
      break;
    case LOADARG_C:
      code = TL(code);
      i++;
      v[i] = v[(word)(HD(code))];
      break;
    case APPLY_C:
      i--;
      v[i] = cons(v[i], v[i + 1]);
      break;
    case APPLYINFIX_C:
      code = TL(code);
      i--;
      v[i] = cons(HD(code), cons(v[i], v[i + 1]));
      break;
    case CONTINUE_INFIX_C:
      code = TL(code);
      v[i - 1] = cons(HD(code), cons(v[i - 1], v[i]));
      // note that 2nd arg is left in place above
      // new expression
      break;
    case IF_C:
      if_flag = true;
      break;
    case FORMLIST_C:
      code = TL(code);
      i++;
      v[i] = NIL;

      for (word j = 1; j <= (word)(HD(code)); j++) {
        i = i - 1;
        v[i] = cons((list)COLON_OP, cons(v[i], v[i + 1]));
      }

      break;
    case FORMZF_C:
      code = TL(code);
      i = i - (word)(HD(code));
      v[i] = cons(v[i], NIL);

      for (word j = (word)(HD(code)); j >= 1; j = j - 1) {
        v[i] = cons(v[i + j], v[i]);
      }

      v[i] = cons((list)ZF_OP, v[i]);
      break;
    case CONT_GENERATOR_C:
      code = TL(code);

      for (word j = 1; j <= (word)(HD(code)); j++) {
        v[i - j] = cons((list)GENERATOR, cons(v[i - j], TL(TL(v[i]))));
      }

      break;
    case MATCH_C:
    case MATCHARG_C:
      code = TL(code);
      code = TL(code);
      break;
    case MATCHPAIR_C:
      code = TL(code);
      {
        list x = v[(word)HD(code)];
        i = i + 2;
        v[i - 1] = HD(TL(x)), v[i] = TL(TL(x));
      }
      break;
    case STOP_C:
      printexp(v[i], 0);

      if (!if_flag) {
        return;
      }

      bcpl_writes(", ");
      printexp(v[i - 1], 0);
      return;
    default:
      bcpl_writes("IMPOSSIBLE INSTRUCTION IN \"displayrhs\"\n");
    }
    // end of switch
    code = TL(code);
  } while (1);
}

// extracts that part of the code which
// determines which cases this equation applies to
list profile(list eqn) {

  list code = TL(eqn);
  if (HD(code) == (list)LINENO_C) {
    code = TL(TL(code));
  }

  {
    list c = code;

    while (parmy(HD(c))) {
      c = rest(c);
    }

    {
      list hold = c;

      while (!(HD(c) == (list)IF_C || HD(c) == (list)STOP_C)) {
        c = rest(c);
      }

      if (HD(c) == (list)IF_C) {
        return subtract(code, c);
      } else {
        return subtract(code, hold);
      }
    }
  }
}

static bool parmy(list x) {
  return x == (list)MATCH_C || x == (list)MATCHARG_C || x == (list)MATCHPAIR_C;
}

// removes one complete instruction from C
static list rest(list c) {

  list x = HD(c);
  c = TL(c);

  if (x == (list)APPLY_C || x == (list)IF_C || x == (list)STOP_C) {
    return c;
  }

  c = TL(c);

  if (!(x == (list)MATCH_C || x == (list)MATCHARG_C)) {
    return c;
  }

  return TL(c);
}

// list subtraction
static list subtract(list x, list y) {

  list z = NIL;

  while (x != y) {
    z = cons(HD(x), z), x = TL(x);
  }

  // note the result is reversed - for our purposes this does not matter
  return z;
}

// called whenever the definiendum is subject of a
// display,reorder or (partial)delete command - has the effect of
// restoring the standard line numbering
void removelineno(list eqn) {

  if (HD(TL(eqn)) == (list)LINENO_C) {
    TL(eqn) = TL(TL(TL(eqn)));
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

  list subject = 0;
  list lhs = 0;
  word nargs = 0;
  init_codev();

  if (haveid()) {

    // avoid global variables
    // subject = (list)THE_ID;
    // lhs = (list)THE_ID;

    subject = (list)the_id();
    lhs = (list)the_id();

    while (startformal(HD(TOKENS))) {

      lhs = cons(lhs, formal());
      nargs = nargs + 1;
    }

  } else if (HD(TOKENS) == (list)'=' && LASTLHS != NIL) {

    subject = LASTLHS;
    lhs = LASTLHS;

    while (iscons(subject)) {
      subject = HD(subject);
      nargs = nargs + 1;
    }

  } else {
    syntax();
    bcpl_writes("missing LHS\n");
    return NIL;
  }

  compilelhs(lhs, nargs);

  {
    list code = collectcode();
    check((token)'=');
    expr(0);
    plant0(STOP_C);

    {
      list expcode = collectcode();

      // change from EMAS/KRC to allow guarded simple def
      if (have((token)',')) {
        expr(0);
        plant0(IF_C);
        code = append(code, append(collectcode(), expcode));
      } else {
        code = append(code, expcode);
      }

      if (HD(TOKENS) != EOFTOKEN) {
        check(EOL);
      }

      if (!ERRORFLAG) {
        LASTLHS = lhs;
      }

      if (nargs == 0) {
        lhs = 0;
      }

      // in this case the lhs field is used to remember
      // the value of the variable - 0 means not yet set

      // OK
      return cons(subject, cons((list)nargs, cons(lhs, code)));
    }
  }
}

// n is the priority level
static void expr(word n) {

  if (n <= 3 && (have((token)'\\') || have((token)'~'))) {

    plant1(LOAD_C, (list)NOT_OP);
    expr(3);
    plant0(APPLY_C);

  } else if (n <= 5 && have((token)'+')) {

    expr(5);

  } else if (n <= 5 && have((token)'-')) {

    plant1(LOAD_C, (list)NEG_OP);
    expr(5);
    plant0(APPLY_C);

  } else if (have((token)'#')) {

    plant1(LOAD_C, (list)LENGTH_OP);
    combn();
    plant0(APPLY_C);

  } else if (startsimple(HD(TOKENS))) {

    combn();

  } else {

    syntax();
    return;
  }

  {
    operator op = mkinfix(HD(TOKENS));

    while (diprio(op) >= n) {

      // for continued relations
      word and_count = 0;

      TOKENS = TL(TOKENS);
      expr(rightprec(op));

      if (ERRORFLAG) {
        return;
      }

      while (isrelop((list)op) && isrelop((list)mkinfix(HD(TOKENS)))) {

        // continued relations
        and_count = and_count + 1;
        plant1(CONTINUE_INFIX_C, (list)op);
        op = mkinfix(HD(TOKENS));
        TOKENS = TL(TOKENS);
        expr(4);

        if (ERRORFLAG) {
          return;
        }
      }

      plant1(APPLYINFIX_C, (list)op);

      for (word i = 1; i <= and_count; i++) {
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

static bool startformal(token t) {
  // return iscons(t) ? (HD(t) == IDENT || HD(t) == (list)CONST)
  //                  : t == (token)'(' || t == (token)'[' || t == (token)'-';

  if (iscons(t)) {

    // identifier or constant
    return (HD(t) == IDENT || HD(t) == (list)CONST);

  } else {

    // '(', '[', '-'
    return t == (token)'(' || t == (token)'[' || t == (token)'-';
  }
}

static bool startsimple(token t) {

  // return iscons(t) ? (HD(t) == IDENT || HD(t) == (list)CONST)
  //                  : t == (token)'(' || t == (token)'[' || t == (token)'{' ||
  //                        t == (token)'\'';

  if (iscons(t)) {

    // identifier or constant
    return (HD(t) == IDENT || HD(t) == (list)CONST);

  } else {

    // '(', '[', '{', '\''
    return t == (token)'(' || t == (token)'[' || t == (token)'{' ||
           t == (token)'\'';
  }
}

static void simple() {

  if (haveid()) {

    // avoid global variables
    // compilename(THE_ID);

    compilename(the_id());

  } else if (haveconst()) {

    // avoid global variables
    // plant1(LOAD_C, (list)internalise(THE_CONST));

    plant1(LOAD_C, (list)internalise(the_const()));

  } else if (have((token)'(')) {
    // (

    expr(0);

    // (_)
    check((token)')');

  } else if (have((token)'[')) {
    // [

    if (have((token)']')) {
      // [] => NIL

      plant1(LOAD_C, NIL);

    } else {

      word n = 1;

      expr(0);

      if (have((token)',')) {
        // [_,
        expr(0);
        n = n + 1;
      }

      if (have(DOTDOT_SY)) {
        // [_,_.. => COMMADOTDOT
        // [_..   => DOTDOT

        if (HD(TOKENS) == (token)']') {
          // [_..]

          plant1(LOAD_C, INFINITY);

        } else {

          expr(0);
        }

        if (n == 2) {

          plant0(APPLY_C);
        }

        plant1(APPLYINFIX_C, (list)(n == 1 ? DOTDOT_OP : COMMADOTDOT_OP));

        // OK
      } else {

        while (have((token)',')) {
          // [_,_, ...
          expr(0);
          n = n + 1;
        }

        plant1(FORMLIST_C, (list)n);
        // OK
      }

      check((token)']');
    }

  } else if (have((token)'{')) {
    // {

    // ZF expressions bug?
    word n = 0;

    // unused
    // list hold = TOKENS;

    perform_alpha_conversions();
    expr(0);

    // implicit zf body no longer legal
    // if ( HD(TOKENS) == BACKARROW_SY ) { TOKENS = hold; } else

    // {_;
    check((token)';');

    // {_;_;_; ...
    do {
      n = n + qualifier();
    } while (have((token)';'));

    // OK
    plant1(FORMZF_C, (list)n);

    // {_;_}
    check((token)'}');

  } else if (have((token)'\'')) {
    // QUOTE_OP
    // operator denotation -- `'#' [1,2,3]?`, `'\' (0==1)?`, `'+' 2 3?`
    // '

    if (have((token)'#')) {
      // '#

      plant1(LOAD_C, (list)LENGTH_OP);

    } else if (have((token)'\\') || have((token)'~')) {
      // '\
      // '~

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

    // '#', '\', '~', '+', ...
    check((token)'\'');

  } else

    // missing identifier|constant|(|[|{
    syntax();
}

static void compilename(atom n) {

  word i = 0;
  while (!(i > ENVP || ENV[i] == (list)n)) {
    i = i + 1;
  }

  if (i > ENVP) {

    plant1(LOAD_C, (list)n);

  } else {

    // OK
    plant1(LOADARG_C, (list)i);
  }
}

static word qualifier() {

  // what about more general formals?
  if (isgenerator(TL(TOKENS))) {

    word n = 0;

    do {
      haveid();

      // avoid global variables
      // plant1(LOAD_C, (list)THE_ID;

      plant1(LOAD_C, (list)the_id());

      n = n + 1;

    } while (have((token)','));

    check(BACKARROW_SY);
    expr(0);
    plant1(APPLYINFIX_C, (list)GENERATOR);

    if (n > 1) {
      // OK
      plant1(CONT_GENERATOR_C, (list)(n - 1));
    }

    return n;

  } else {

    expr(0);
    return 1;
  }
}

static void perform_alpha_conversions() {

  list p = TOKENS;
  while (!(HD(p) == (token)'}' || HD(p) == (token)']' || HD(p) == EOL)) {

    if (HD(p) == (token)'[' || HD(p) == (token)'{') {
      p = skipchunk(p);
      continue;
    }

    // recognises the "such that" bar '|' and converts it to ';'
    // to distinguish it from "or"
    if (HD(p) == (token)'|' && isid(HD(TL(p))) && isgenerator(TL(TL(p)))) {
      HD(p) = (token)';';
    }

    if (isid(HD(p)) && isgenerator(TL(p))) {
      alpha_convert(HD(p), TL(p));
    }

    p = TL(p);
  }
}

bool isid(list x) { return iscons(x) && HD(x) == IDENT; }

static bool isgenerator(list t) {

  return !iscons(t) ? false
                    : HD(t) == BACKARROW_SY ||
                          (HD(t) == (token)',' && isid(HD(TL(t))) &&
                           isgenerator(TL(TL(t))));
}

static void alpha_convert(list var, list p) {

  list t = TOKENS;
  list var1 = cons((list)ALPHA, TL(var));
  list edge = t;

  while (!(HD(edge) == (token)';' || HD(edge) == BACKARROW_SY ||
           HD(edge) == EOL)) {
    edge = skipchunk(edge);
  }

  while (t != edge) {
    conv1(t, var, var1);
    t = TL(t);
  }
  t = p;

  while (!(HD(t) == (token)';' || HD(t) == EOL)) {
    t = skipchunk(t);
  }

  edge = t;
  while (
      !(HD(edge) == (token)'}' || HD(edge) == (token)']' || HD(edge) == EOL)) {
    edge = skipchunk(edge);
  }

  while (t != edge) {
    conv1(t, var, var1);
    t = TL(t);
  }
  TL(var) = var1;
}

static list skipchunk(list p) {

  // select a bracket
  word ket = HD(p) == (token)'{' ? '}' : HD(p) == (token)'[' ? ']' : -1;

  p = TL(p);

  // NG
  if (ket == -1) {
    return p;
  }

  while (!(HD(p) == (list)ket || HD(p) == EOL)) {
    p = skipchunk(p);
  }

  if (HD(p) != EOL) {
    p = TL(p);
  }

  return (p);
}

static void conv1(list t, list var, list var1) {

  if (equal(HD(t), var) && HD(t) != var) {
    TL(HD(t)) = var1;
  }
}

static list formal() {

  if (haveid()) {

    // avoid global variables
    // return (list)THE_ID;

    return (list)the_id();

  } else if (haveconst()) {

    // avoid global variables
    // return internalise(THE_CONST);

    return internalise(the_const());

  } else if (have((token)'(')) {

    list p = pattern();
    check((token)')');
    return p;

  } else if (have((token)'[')) {

    list plist = NIL;
    list p = NIL;

    if (have((token)']')) {
      return NIL;
    }

    do {
      plist = cons(pattern(), plist);
    } while (have((token)','));
    // note they are in reverse order

    check((token)']');

    while (plist != NIL) {
      p = cons((token)COLON_OP, cons(HD(plist), p));
      plist = TL(plist);
    }
    // now they are in correct order

    return p;

  } else if (have((token)'-') && havenum()) {

    // avoid global variables
    // THE_NUM = -THE_NUM;
    // return stonum(THE_NUM);

    negate_the_num();
    return stonum(the_num());

  } else {

    // MISSING identifier|constant|(|[
    syntax();
    return NIL;
  }
}

static list internalise(list val) {

  // return val == TL(TRUTH)
  //            ? TRUTH
  //            : val == TL(FALSITY) ? FALSITY
  //                                 : isatom(val) ? cons((list)QUOTE, val) :
  //                                 val;

  if (val == getval(TRUTH)) {
    // TRUTH = (QUOTE . "TRUE")

    return TRUTH;

  } else if (val == getval(FALSITY)) {
    // FALSITY = (QUOTE . "FALSE")

    return FALSITY;

  } else if (isatom(val)) {
    // (QUOTE . atom)

    return cons((list)QUOTE, val);

  } else {

    return val;
  }
}

static list pattern() {

  list p = formal();

  if (have((token)':')) {
    p = cons((list)COLON_OP, cons(p, pattern()));
  }

  return p;
}

static void compilelhs(list lhs, word nargs) {

  ENVP = nargs - 1;

  for (word i = 1; i <= nargs; i++) {
    ENV[nargs - i] = TL(lhs);
    lhs = HD(lhs);
  }
  for (word i = 0; i <= nargs - 1; i++) {
    compileformal(ENV[i], i);
  }
}

static void compileformal(list x, word i) {

  // identifier
  if (isatom(x)) {
    word j = 0;

    while (!(j >= i || ENV[j] == x)) {
      // is this a repeated name?
      j = j + 1;
    }

    if (j >= i) {
      // no, no code compiled
      return;
    } else {
      plant2(MATCHARG_C, (list)i, (list)j);
    }

  } else if (isnum(x) || x == NIL || (iscons(x) && HD(x) == (list)QUOTE)) {
    plant2(MATCH_C, (list)i, x);
  } else if (iscons(x) && HD(x) == (token)COLON_OP && iscons(TL(x))) {
    // OK
    plant1(MATCHPAIR_C, (list)i);
    ENVP = ENVP + 2;

    {
      word a = ENVP - 1;
      word b = ENVP;
      ENV[a] = HD(TL(x));
      ENV[b] = TL(TL(x));
      compileformal(ENV[a], a);
      compileformal(ENV[b], b);
    }

  } else {
    bcpl_writes("Impossible event in \"compileformal\"\n");
  }
}

// plant stores instructions and their operands in the code vector
// op is always an instruction code (*_C);
// a and b can be operators (*_op), INTs, CONSTs, IDs (names) or
// the address of a C function - all are mapped to list type.

// APPLY_C IF_C STOP_C
static void plant0(instruction op) {

  // CODEV = (op . CODEV)
  CODEV = cons((list)op, CODEV);
}

// everything else
static void plant1(instruction op, list a) {

  // CODEV = (a . (op . CODEV))
  CODEV = cons((list)op, CODEV);
  CODEV = cons(a, CODEV);
}

// MATCH_C MATCHARG_C
static void plant2(instruction op, list a, list b) {

  // CODEV = (b . (a . (op . CODEV)))
  CODEV = cons((list)op, CODEV);
  CODEV = cons(a, CODEV);
  CODEV = cons(b, CODEV);
}

// flushes the code buffer
static list collectcode() {

  list tmp = CODEV;
  CODEV = NIL;

  return reverse(tmp);
}

// mark elements in CODEV and ENV for preservation by the GC.
// this routine should be called by your bases() function.
void compiler_bases(void (*f)(list *)) {

  f(&CODEV);

  // ENVP indexes the last used element and starts as -1.
  for (word i = 0; i <= ENVP; i++) {
    f(&ENV[i]);
  }
}
