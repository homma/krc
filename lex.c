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
word ERRORFLAG;
word EQNFLAG;
word EXPFLAG;
word COMMENTFLAG;

bool SKIPCOMMENTS;
list TOKENS = 0;
atom THE_ID = 0;
list THE_CONST = 0;

word THE_NUM;
word THE_DECIMALS;

// local function declarations
static token readtoken(void);
static word read_decimals(void);

// returns value in hundredths
static word peekalpha(void);

// local variables
static bool EXPECTFILE = false;
static token MISSING;

// reads the next line into "TOKENS"
void readline() {

  do {
    list *p = &TOKENS;
    token t = 0;
    MISSING = 0;
    TOKENS = NIL;
    THE_DECIMALS = 0;
    ERRORFLAG = false;

    // will get set if the line contains "?" or "!"
    EXPFLAG = false;

    // >0 records number of lines in the comment
    COMMENTFLAG = 0;

    EXPECTFILE = false;

    // will get set if the line contains "="
    EQNFLAG = false;

    do {
      t = readtoken();

      // GCC
      *p = cons((list)t, NIL);
      p = &(TL(*p));

    } while (
        !(t == (token)EOL || t == (token)ENDSTREAMCH || t == (token)BADTOKEN));

    // ignore first line of Unix script file
    if (HD(TOKENS) == (list)'#' && iscons(TL(TOKENS)) &&
        HD(TL(TOKENS)) == (list)'!') {
      continue;
    }

    if (t == (token)EOL || t == (token)ENDSTREAMCH) {
      return;
    }

    bcpl_writes("Closing quote missing - line ignored\n");
    ERRORFLAG = true;

    return;

  } while (1);
}

#define NOTCH(CH) (CH == '\\' || CH == '~' && LEGACY)

// TOKEN: := CHAR | <certain digraphs, represented by nos above 256 > |
//          | cons(IDENT, ATOM) | cons(CONST, <ATOM | NUM>)
static token readtoken(void) {
  word ch = rdch();

  while ((ch == ' ' || ch == '\t')) {
    ch = rdch();
  }

  if (ch == '\n') {
    return (token)EOL;
  }

  if (ch == EOF) {
    return (token)ENDSTREAMCH;
  }

  if (('a' <= ch && ch <= 'z') || ('A' <= ch && ch <= 'Z') ||
      (EXPECTFILE && !isspace(ch))) {
    // expt to allow _ID, discontinued
    // ||(ch == '_' && peekalpha())

    do {
      bufch(ch);
      ch = rdch();
    } while (('a' <= ch && ch <= 'z') || ('A' <= ch && ch <= 'Z') ||
             isdigit(ch) || ch == '\'' || ch == '_' ||
             (EXPECTFILE && !isspace(ch)));

    unrdch(ch);

    {
      list x = (list)packbuffer();
      if (TOKENS != NIL && HD(TOKENS) == (token)'/' && TL(TOKENS) == NIL &&
          member(FILECOMMANDS, x)) {
        EXPECTFILE = true;
      }
      return cons((list)IDENT, x);
    }
  }

  if (isdigit(ch)) {
    THE_NUM = 0;

    while (isdigit(ch)) {
      THE_NUM = THE_NUM * 10 + ch - '0';
      if (THE_NUM < 0) {
        bcpl_writes("\n**integer overflow**\n");
        escapetonextcommand();
      }
      ch = rdch();
    }

    if (ch != EOF) {
      unrdch(ch);
    }

    return cons((token)CONST, stonum(THE_NUM));
  }

  if (ch == '"') {
    atom a;
    ch = rdch();

    while (!(ch == '"' || ch == '\n' || ch == EOF)) {
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
          if ('0' <= ch && ch <= '9') {
            int i = 3;
            int n = ch - '0';
            int n1;
            ch = rdch();
            while (--i && '0' <= ch && ch <= '9' &&
                   (n1 = 10 * n + ch - '0') < 256) {
              n = n1, ch = rdch();
            }
            bufch(n);
            unrdch(ch);
          }
        }
      } else
        bufch(ch);
      ch = rdch();
    }
    a = packbuffer();
    return ch != '"' ? (token)BADTOKEN : cons(CONST, (list)a);
  }

  {
    word ch2 = rdch();
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

      // option - s
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

      while (!(ch == ';' || ch == EOF))
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

      if (ch == EOF) {
        fprintf(bcpl_OUTPUT, "%s :- ...", NAME((atom)subject)),
            bcpl_writes(" missing \";\"\n");
        COMMENTFLAG--;
        syntax();
      } else {
        c = cons((list)packbuffer(), c);
      }

      return reverse(c);
    }

    if (ch == ch2) {
      if (ch == '+') {
        return PLUSPLUS_SY;
      }

      if (ch == '.') {
        return DOTDOT_SY;
      }

      if (ch == '-') {
        return DASHDASH_SY;
      }

      if (ch == '*') {
        return STARSTAR_SY;
      }

      // added DT 2015
      if (ch == '=') {
        return EQ_SY;
      }

      // comment to end of line(new)
      if (ch == '|') {
        do {
          ch = rdch();
          if (ch == '\n')
            return EOL;
          if (ch == EOF)
            return ENDSTREAMCH;
        } while (1);
      }
    }

    if (ch == '<' && '-' == ch2) {
      return BACKARROW_SY;
    }

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

    unrdch(ch2);

    if (ch == '?' || ch == '!') {
      EXPFLAG = true;
    }

    if (ch == '=' && !LEGACY) {
      EQNFLAG = true;
    }

    // GCC warning expected
    return (token)(NOTCH(ch) ? '\\' : ch);
  }
}

word caseconv(word ch) { return tolower(ch); }

static word peekalpha() {

  word ch = rdch();
  unrdch(ch);
  return (('a' <= ch && ch <= 'z') || ('A' <= ch && ch <= 'Z'));
}

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
        fprintf(bcpl_OUTPUT, "<UNKNOWN TOKEN<%p>>", t);
      } else if (HD(t) == IDENT) {
        bcpl_writes(NAME((atom)(
            iscons(TL(t)) && HD(TL(t)) == (list)ALPHA ? TL(TL(t)) : TL(t))));
      } else if (isnum(TL(t))) {
        bcpl_writen(getnum(TL(t)));
      } else {
        fprintf(bcpl_OUTPUT, "\"%s\"", NAME((atom)TL(t)));
      }
    }
  }
}

bool have(token t) {

  if (TOKENS == NIL || HD(TOKENS) != t) {
    return false;
  }

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

word haveid() {

  while (!(iscons(HD(TOKENS)) && HD(HD(TOKENS)) == IDENT)) {
    return false;
  }

  THE_ID = (atom)TL(HD(TOKENS));
  TOKENS = TL(TOKENS);

  return true;
}

word haveconst() {

  while (!(iscons(HD(TOKENS)) && HD(HD(TOKENS)) == CONST)) {
    return false;
  }

  THE_CONST = TL(HD(TOKENS));
  TOKENS = TL(TOKENS);

  return true;
}

word havenum() {

  while (!(iscons(HD(TOKENS)) && HD(HD(TOKENS)) == CONST &&
           isnum(TL(HD(TOKENS))))) {
    return false;
  }

  THE_NUM = getnum(TL(HD(TOKENS)));
  TOKENS = TL(TOKENS);

  return true;
}

// syntax error diagnosis(needs refining)
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
