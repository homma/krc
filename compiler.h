// KRC compiler header file

//----------------------------------------------------------------------
// The KRC system is Copyright (c) D. A. Turner 1981
// All  rights reserved.  It is distributed as free software under the
// terms in the file "COPYING", which is included in the distribution.
//----------------------------------------------------------------------

// lex analyser manifests

// in-core tokens can be CONSes
#define token list

#define IDENT (token)0
#define CONST (token)1
#define EOL (token)'\n'
#define BADTOKEN (token)256
#define PLUSPLUS_SY (token)257
#define GE_SY (token)258
#define LE_SY (token)259
#define NE_SY (token)260
#define EQ_SY (token)261
#define DOTDOT_SY (token)262
#define BACKARROW_SY (token)263
#define DASHDASH_SY (token)264
#define STARSTAR_SY (token)265

//-1
#define ENDSTREAMCH (token) EOF

// lex analyser globals

// bases
extern list TOKENS, THE_CONST;
extern atom THE_ID;

// DECIMALS are never used
extern word THE_NUM, THE_DECIMALS;

extern word EXPFLAG, ERRORFLAG, EQNFLAG;
extern token MISSEDTOK;
extern word caseconv(word CH);
extern word COMMENTFLAG;
extern list FILECOMMANDS;
extern bool LEGACY;
extern void writetoken(token T);

//
// KRC expression representations
// the internal representations of expressions is as follows
//
// EXP   ::= ID | CONST | APPLN | OTHER
// ID    ::= ATOM
// CONST ::= NUM | cons(QUOTE, ATOM) | NIL
// APPLN ::= cons(EXP, EXP)
// OTHER ::= cons(OPERATOR, OPERANDS)
//
// note that the internal form of ZF exessions is
// cons(ZF_OP,BODY) :-
//  BODY ::= cons(EXP, NIL) | cons(QUALIFIER, BODY)
//  QUALIFIER::= EXP | cons(GENERATOR, cons(ID, EXP))
//

// operator values:
typedef enum {
  // quasi operators
  ALPHA = -2,
  INDIR = -1,
  QUOTE = 0,
  // infix operators
  COLON_OP = 1,
  APPEND_OP = 2,
  LISTDIFF_OP = 3,
  OR_OP = 4,
  AND_OP = 5,
  // subgroup: relational operators
  GR_OP = 6,
  GE_OP = 7,
  NE_OP = 8,
  EQ_OP = 9,
  LE_OP = 10,
  LS_OP = 11,
  PLUS_OP = 12,
  MINUS_OP = 13,
  TIMES_OP = 14,
  DIV_OP = 15,
  REM_OP = 16,
  EXP_OP = 17,
  DOT_OP = 18,
  // other operators
  DOTDOT_OP = 19,
  COMMADOTDOT_OP = 20,
  ZF_OP = 21,
  GENERATOR = 22,
  LENGTH_OP = 23,
  NEG_OP = 24,
  NOT_OP = 25,
  QUOTE_OP = 26
  // used to convert an infix into a function
}
operator;

//
// internal representation of KRC equations
//
// VAL FIELD OF ATOM ::= cons(cons(NARGS, COMMENT), LISTOF(EQN))
// COMMENT ::= NIL | cons(ATOM, COMMENT)
// EQN     ::= cons(LHS, CODE)
//
// (if NARGS=0 there is only one equation in the list and its lhs field
//  is used to remember the value of the variable)
//
// LHS     ::= ID | cons(LHS,FORMAL)
// FORMAL  ::= ID | CONST | cons(COLON_OP,cons(FORMAL,FORMAL))
// CODE    ::= INSTR*
// INSTR   ::= LOAD_C <ID | CONST | MONOP> |
//             LOADARG_C INT |
//             APPLY_C |
//             APPLYINFIX_C DIOP |
//             IF_C |
//             FORMLIST_C INT |
//             MATCH_C INT CONST
//             MATCHARG_C INT INT |
//             MATCHPAIR_C INT |
//             STOP_C |
//             LINENO_C INT |
//             CONTINUE.INFIX_C DIOP |
//             CONT.GENERATOR_C INT |
//             FORMZF_C INT |
//             CALL_C BCPL_FN
//

// instruction codes
//
// the lineno command has no effect at execution time, it is used
// to give an equation a non standard line number for insertion purposes

typedef enum {
  LOAD_C = 0,
  LOADARG_C = 1,
  APPLY_C = 2,
  APPLYINFIX_C = 3,
  IF_C = 4,
  FORMLIST_C = 5,
  MATCH_C = 6,
  MATCHARG_C = 7,
  MATCHPAIR_C = 8,
  STOP_C = 9,
  LINENO_C = 10,
  CALL_C = 11,
  CONTINUE_INFIX_C = 12,
  FORMZF_C = 13,
  CONT_GENERATOR_C = 14,
} instruction;

//
// external syntax for KRC expressions and equations
//
// EQUATION   ::= LHS=EXP | LHS=EXP,EXP
// LHS        ::= ID FORMAL*
// FORMAL     ::= ID | CONST | (PATTERN) | [PATTERN-LIST?]
// PATTERN    ::= FORMAL:PATTERN | FORMAL
// EXP        ::= PREFIX EXP | EXP INFIX EXP | SIMPLE SIMPLE*
// SIMPLE     ::= ID | CONST | (EXP) | [EXP-LIST?] | [EXP..EXP] | [EXP..] |
//                [EXP,EXP..EXP] | [EXP,EXP..] | {EXP;QUALIFIERS}
// QUALIFIERS ::= QUALIFIER | QUALIFIER;QUALIFIERS
// QUALIFIER  ::= EXP | NAME-LIST<-EXP
// CONST      ::= INT | "STRING"
//

//
// prefix and infix operators, in order of increasing binding power:
//
//    (prefix)               (infix)            (remarks)
//                          :  ++  --           right associative
//                             |
//                             &
//      \
//                     >  >=  ==  \=  <=  <     continued relations allowed
//                            +  -              left associative
//      -
//                          *  /  %             left associative
//                           **  .              (** is right associative)
//      #
// Notes - "%" is remainder operator, "." is functional composition and "#"
// takes the length of lists
//

//// compiler globals

// defined in lex.c
extern void readline(void);
extern bool have(token);
extern word haveid(void);
extern void syntax(void);
extern void check(token);
extern word haveconst(void);
extern word havenum(void);
extern void syntax_error(char *);

// defined in compiler.c
extern void init_codev(void);
extern list equation(void);
extern list profile(list);
extern void printexp(list, word);
extern list expression(void);
extern void removelineno(list);
extern bool isid(list);
extern void display(atom id, bool withnos, bool doublespacing);
extern void displayeqn(atom id, word nargs, list eqn);
extern void displayrhs(list lhs, word nargs, list code);

// defined in reducer.c
extern void printatom(atom a, bool format);

// others
extern void (*TRUEWRCH)(word c);

// bases
extern list TRUTH, FALSITY, INFINITY, LASTLHS;

//// GC helpers

// defined in compiler.c
extern void compiler_bases(void (*F)(list *));

// defined in reducer.c
extern void reducer_bases(void (*F)(list *));
