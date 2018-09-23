#include "common.h"
#include "iolib.h"
#include "listlib.h"
#include "compiler.h"

#include <ctype.h>

// KRC lex analyser

// ----------------------------------------------------------------------
// The KRC system is Copyright(c) D.A.Turner 1981
// All rights reserved.It is distributed as free software under the
// terms in the file "COPYING", which is included in the distribution.
// ----------------------------------------------------------------------

// from main.c
extern void escapetonextcommand();

// global variables owned by lex.c

// error happend : syntax errors, file read errors, etc.
word ERRORFLAG;

// is equation
// foo = bar
word EQNFLAG;

// is expression
// foo! or bar?
word EXPFLAG;

// is comment and holds number of comment lines
word COMMENTFLAG;

// set by -s option
bool SKIPCOMMENTS;

// list of tokens
// set value by readline and modified by have, haveid, haveconst, havenum
list TOKENS = 0;

// set value by haveid
atom THE_ID = 0;

// set value by haveconst
list THE_CONST = 0;

// set value by havenum
word THE_NUM;

// local function declarations
static token readtoken(void);

// unused
// static word peekalpha(void);

// local variables

// in the FILECOMMANDS and expects a filename
static bool EXPECTFILE = false;

static token MISSING;

// read one line
// tokens are stored into TOKENS
void readline() {

  do {
    list *p = &TOKENS;
    token t = 0;
    MISSING = 0;
    TOKENS = NIL;
    ERRORFLAG = false;

    // will get set if the line contains "?" or "!"
    EXPFLAG = false;

    // >0 records number of lines in the comment
    COMMENTFLAG = 0;

    // not in FILECOMMANDS yet
    EXPECTFILE = false;

    // will get set if the line contains "="
    EQNFLAG = false;

    // reads token one by one
    do {
      t = readtoken();

      // GCC

      // first time : ( TOKENS = t:[] )
      // after that : ( t1 : t2 : ... : p ++ t : [] )
      *p = cons((list)t, NIL);

      // p = tail(t:[])
      p = &(TL(*p));

    } while (
        !(t == (token)EOL || t == (token)EOFTOKEN || t == (token)BADTOKEN));

    // ignore first line of Unix script file
    //
    // tokens = [ "#", "!", "/" ]
    // "#" == hd tokens & list (tl tokens) & "!" == (hd (tl tokens))?
    //
    if (HD(TOKENS) == (list)'#' && iscons(TL(TOKENS)) &&
        HD(TL(TOKENS)) == (list)'!') {
      continue;
    }

    // finished reading one line
    if (t == (token)EOL || t == (token)EOFTOKEN) {
      return;
    }

    // BADTOKEN
    // malformed string
    bcpl_writes("Closing quote missing - line ignored\n");
    ERRORFLAG = true;

    return;

    // loops only when the line stared with shebang
  } while (1);
}

// char for NOT -- '\' or '~' (legacy)
#define NOTCH(ch) (ch == '\\' || ch == '~' && LEGACY)

// reads one token
// returns a token as a list
//
// TOKEN: := CHAR | <certain digraphs, represented by nos above 256 > |
//          | cons(IDENT, ATOM) | cons(CONST, <ATOM | NUM>)
static token readtoken(void) {

  word ch = rdch();

  // skip initial spaces
  while ((ch == ' ' || ch == '\t')) {
    ch = rdch();
  }

  // no token found
  if (ch == '\n') {
    return (token)EOL;
  }

  // no token found
  if (ch == EOF) {
    return (token)EOFTOKEN;
  }

  // lexer for KRC program and KRC shell commands are mixed here.

  // initial character is
  // 1. alphabet chars
  // 2. non-space chars for filename if we are in FILECOMMANDS
  if (('a' <= ch && ch <= 'z') || ('A' <= ch && ch <= 'Z') ||
      (EXPECTFILE && !isspace(ch))) {

    // starting with '_' as _ID is discontinued
    // ||(ch == '_' && peekalpha())

    do {

      // keep it in BUFFER
      bufch(ch);

      // read next char
      ch = rdch();

      // 1. alphanumeric, '\'', '_'
      // 2. non-space for a filename
    } while (('a' <= ch && ch <= 'z') || ('A' <= ch && ch <= 'Z') ||
             isdigit(ch) || ch == '\'' || ch == '_' ||
             (EXPECTFILE && !isspace(ch)));

    unrdch(ch);

    {
      // creates an atom from BUFFER
      list x = (list)packbuffer();

      // if token is a FILECOMMAND
      if (TOKENS != NIL && HD(TOKENS) == (token)'/' && TL(TOKENS) == NIL &&
          member(FILECOMMANDS, x)) {
        EXPECTFILE = true;
      }

      // make identifier
      return cons((list)IDENT, x);
    }
  }

  // initial character is number
  if (isdigit(ch)) {

    // avoid globals
    // THE_NUM = 0;

    word n = 0;

    while (isdigit(ch)) {

      // avoid globals
      // THE_NUM = THE_NUM * 10 + ch - '0';
      // if (THE_NUM < 0) {

      n = n * 10 + ch - '0';
      if (n < 0) {
        bcpl_writes("\n**integer overflow**\n");
        escapetonextcommand();
      }
      ch = rdch();
    }

    if (ch != EOF) {
      unrdch(ch);
    }

    // avoid globals
    // return cons((token)CONST, stonum(THE_NUM));

    // make constant
    return cons((token)CONST, stonum(n));
  }

  // the token is expect to be a string
  if (ch == '"') {

    atom a;
    ch = rdch();

    while (!(ch == '"' || ch == '\n' || ch == EOF)) {

      // escape chars
      // add C escape chars, DT 2015
      if (ch == '\\') {

        ch = rdch();

        switch (ch) {
        case 'a':
          bufch('\a');
          break;
        case 'b':
          bufch('\b');
          break;
        case 'f':
          bufch('\f');
          break;
        case 'n':
          bufch('\n');
          break;
        case 'r':
          bufch('\r');
          break;
        case 't':
          bufch('\t');
          break;
        case 'v':
          bufch('\v');
          break;
        case '\\':
          bufch('\\');
          break;
        case '\'':
          bufch('\'');
          break;
        case '\"':
          bufch('\"');
          break;
        case '\n':
          return (token)BADTOKEN;
        default:
          // coded char such as "\97" == "a"
          if ('0' <= ch && ch <= '9') {
            int i = 3;
            int n = ch - '0';
            int n1;
            ch = rdch();
            while (--i && '0' <= ch && ch <= '9' &&
                   (n1 = 10 * n + ch - '0') < 256) {
              n = n1;
              ch = rdch();
            }
            bufch(n);
            unrdch(ch);
          }
        }
      } else {

        // not escape chars
        bufch(ch);
      }

      ch = rdch();
    }

    // create an atom from BUFFER
    a = packbuffer();

    // return ch != '"' ? (token)BADTOKEN : cons(CONST, (list)a);
    if (ch != '"') {

      // malformed string
      return (token)BADTOKEN;

    } else {

      // make a constant
      return cons(CONST, (list)a);
    }
  }

  // need to check the second char
  {
    word ch2 = rdch();

    // comment found
    // :-
    if (ch == ':' && ch2 == '-' && TOKENS != NIL && iscons(HD(TOKENS)) &&
        HD(HD(TOKENS)) == IDENT && TL(TOKENS) == NIL) {
      list c = NIL;
      list subject = TL(HD(TOKENS));
      COMMENTFLAG = 1;

      ch = rdch();

      // ignore blank lines
      while (ch == '\n') {
        COMMENTFLAG++;
        ch = rdch();
      }

      // option -s
      if (SKIPCOMMENTS) {
        while (!(ch == ';' || ch == EOF)) {
          if (ch == '\n') {
            COMMENTFLAG++;
          }
          ch = rdch();
        }
        return NIL;
      }

      if (ch == ';') {
        return NIL;
      }

      while (!(ch == ';' || ch == EOF)) {
        if (ch == '\n') {
          c = cons((list)packbuffer(), c);
          do {
            COMMENTFLAG++;
            ch = rdch();
          } while (ch == '\n');
          // ignore blank lines
        } else {
          bufch(ch);
          ch = rdch();
        }
      }

      if (ch == EOF) {

        // malformed comment
        fprintf(bcpl_OUTPUT, "%s :- ...", NAME((atom)subject)),
            bcpl_writes(" missing \";\"\n");
        COMMENTFLAG--;
        syntax();

      } else {
        c = cons((list)packbuffer(), c);
      }

      return reverse(c);
    }
    // comments end

    // consecutive two same chars
    // operators and comment
    if (ch == ch2) {

      // ++
      if (ch == '+') {
        return PLUSPLUS_SY;
      }

      // ..
      if (ch == '.') {
        return DOTDOT_SY;
      }

      // --
      if (ch == '-') {
        return DASHDASH_SY;
      }

      // **
      if (ch == '*') {
        return STARSTAR_SY;
      }

      // ==
      // added DT 2015
      if (ch == '=') {
        return EQ_SY;
      }

      // ||
      // comment to end of line(new)
      if (ch == '|') {
        do {
          ch = rdch();
          if (ch == '\n')
            return EOL;
          if (ch == EOF)
            return EOFTOKEN;
        } while (1);
      }
    }

    // other operators

    // <-
    if (ch == '<' && '-' == ch2) {
      return BACKARROW_SY;
    }

    // >=, <=, \=, ~= (legacy)
    if (ch2 == '=') {
      if (ch == '>') {
        return GE_SY;
      }

      if (ch == '<') {
        return LE_SY;
      }

      if (NOTCH(ch)) {
        return NE_SY;
      }
    }

    // not a two char symbol

    unrdch(ch2);

    // expression
    if (ch == '?' || ch == '!') {
      EXPFLAG = true;
    }

    // equation
    if (ch == '=' && !LEGACY) {
      EQNFLAG = true;
    }

    // GCC warning expected
    // return (token)(NOTCH(ch) ? '\\' : ch);

    if (NOTCH(ch)) {
      // '\\', '~'
      return (token)'\\';

    } else {
      return (token)ch;
    }
  }
}

word caseconv(word ch) { return tolower(ch); }

/* unused
// peek one alphabet
// returns value in hundredths
static word peekalpha() {

  word ch = rdch();
  unrdch(ch);
  return (('a' <= ch && ch <= 'z') || ('A' <= ch && ch <= 'Z'));
}
*/

void writetoken(token t) {

  if (t < (token)256 && t > (token)32) {

    wrch((word)t);

  } else {

    switch ((word)t) {
    case (word)'\n':
      bcpl_writes("newline");
      break;
    case (word)PLUSPLUS_SY:
      bcpl_writes("++");
      break;
    case (word)DASHDASH_SY:
      bcpl_writes("--");
      break;
    case (word)STARSTAR_SY:
      bcpl_writes("**");
      break;
    case (word)GE_SY:
      bcpl_writes(">=");
      break;
    case (word)LE_SY:
      bcpl_writes("<=");
      break;
    case (word)NE_SY:
      bcpl_writes("\\=");
      break;
    case (word)EQ_SY:
      bcpl_writes("==");
      break;
    case (word)BACKARROW_SY:
      bcpl_writes("<-");
      break;
    case (word)DOTDOT_SY:
      bcpl_writes("..");
      break;
    default:
      if (!(iscons(t) && (HD(t) == IDENT || HD(t) == CONST))) {

        // unknown token
        fprintf(bcpl_OUTPUT, "<UNKNOWN TOKEN<%p>>", t);

      } else if (HD(t) == IDENT) {

        // identifier
        bcpl_writes(NAME((atom)(
            iscons(TL(t)) && HD(TL(t)) == (list)ALPHA ? TL(TL(t)) : TL(t))));

      } else if (isnum(TL(t))) {

        // number
        bcpl_writen(getnum(TL(t)));

      } else {

        fprintf(bcpl_OUTPUT, "\"%s\"", NAME((atom)TL(t)));
      }
    }
  }
}

// examine one token from TOKENS
// if it is not a requested token, return false
// if it is, return true and set TOKENS the next token
//
// TOKENS = (t)
// have t [] = "FALSE"
// have t (x:xs) = x == t
bool have(token t) {

  if (TOKENS == NIL || HD(TOKENS) != t) {
    return false;
  }

  // go to next token
  TOKENS = TL(TOKENS);

  return true;
}

void check(token t) {

  if (have(t)) {
    return;
  }

  ERRORFLAG = true;

  if (MISSING == 0) {
    MISSING = t;
  }
}

void syntax() { ERRORFLAG = true; }

// examine one token from TOKENS
// if it is not an identifier, return false
// if it is an identifier, set it to THE_ID
// then set TOKENS the next token
//
// TOKENS = (("IDENT" . identifier))
// haveid (x:xs) = list x & hd x == "IDENT"
word haveid() {

  if (!(iscons(HD(TOKENS)) && HD(HD(TOKENS)) == IDENT)) {
    return false;
  }

  // set the identifier to share it to the caller
  // it would be better to avoid using global variables
  // and return this value as return val
  THE_ID = (atom)TL(HD(TOKENS));

  // go to next token
  TOKENS = TL(TOKENS);

  return true;
}

// returns the atom in THE_ID
// encupsulate the access to THE_ID and avoid global reference
atom the_id() { return THE_ID; }

// set THE_ID
void set_the_id(atom id) { THE_ID = id; }

// examine one token from TOKENS
// if it is not a constant, return false
// if it is a constant, set it to THE_CONST
// then set TOKENS the next token
//
// TOKENS = (("CONST" . constant))
// haveconst (x:xs) = list x & hd x == "CONST"
word haveconst() {

  if (!(iscons(HD(TOKENS)) && HD(HD(TOKENS)) == CONST)) {
    return false;
  }

  // set the constant to share it to the caller
  THE_CONST = TL(HD(TOKENS));

  // go to next token
  TOKENS = TL(TOKENS);

  return true;
}

// returns the const in THE_CONST
// encupsulate the access to THE_CONST and avoid global reference
list the_const() { return THE_CONST; }

// examine one token from TOKENS
// if it is not a number, return false
// if it is a number, set it to THE_NUM
// then set TOKENS the next token
//
// TOKENS = (("CONST" . number))
word havenum() {

  if (!(iscons(HD(TOKENS)) && HD(HD(TOKENS)) == CONST &&
        isnum(TL(HD(TOKENS))))) {
    return false;
  }

  // set the number to share it to the caller
  THE_NUM = getnum(TL(HD(TOKENS)));

  // go to next token
  TOKENS = TL(TOKENS);

  return true;
}

// returns the number in THE_NUM
// encupsulate the access to THE_NUM and avoid global reference
word the_num() { return THE_NUM; }

// change the sign of THE_NUM
void negate_the_num() { THE_NUM = -THE_NUM; }

// syntax error diagnosis (needs refining)
void syntax_error(char *message) {

  // unclosed string quotes
  if (iscons(TOKENS) && HD(TOKENS) != BADTOKEN) {
    bcpl_writes("**unexpected `"), writetoken(HD(TOKENS)), wrch('\'');
    if (MISSING && MISSING != EOL && MISSING != (token)';' &&
        MISSING != (token)'\'') {

      bcpl_writes(", missing `"), writetoken(MISSING), wrch('\'');

      if (MISSING == (token)'?') {
        bcpl_writes(" or `!'");
      }
    }
    wrch('\n');
  }
  bcpl_writes(message);
}
