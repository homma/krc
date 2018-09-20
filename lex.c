#include "common.h"
#include "iolib.h"
#include "listlib.h"
#include "compiler.h"

#include <ctype.h>

// KRC LEX ANALYSER

// ----------------------------------------------------------------------
// The KRC system is Copyright(c) D.A.Turner 1981
// All rights reserved.It is distributed as free software under the
// terms in the file "COPYING", which is included in the distribution.
// ----------------------------------------------------------------------

// global variables owned by lex.c
word ERRORFLAG, EQNFLAG, EXPFLAG, COMMENTFLAG;
bool SKIPCOMMENTS;
LIST TOKENS = 0;
ATOM THE_ID = 0;
LIST THE_CONST = 0;
word THE_NUM, THE_DECIMALS;

// local function declarations
static TOKEN readtoken(void);
static word read_decimals(void);

// returns value in hundredths
static word peekalpha(void);

// local variables
static bool EXPECTFILE = false;
static TOKEN MISSING;

// reads the next line into "TOKENS"
void readline() {

  do {
    LIST *P = &TOKENS;
    TOKEN T = 0;
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
      T = readtoken();

      // GCC
      *P = CONS((LIST)T, NIL);
      P = &(TL(*P));

    } while (
        !(T == (TOKEN)EOL || T == (TOKEN)ENDSTREAMCH || T == (TOKEN)BADTOKEN));

    // ignore first line of Unix script file
    if (HD(TOKENS) == (LIST)'#' && ISCONS(TL(TOKENS)) &&
        HD(TL(TOKENS)) == (LIST)'!') {
      continue;
    }

    if (T == (TOKEN)EOL || T == (TOKEN)ENDSTREAMCH) {
      return;
    }

    bcpl_WRITES("Closing quote missing - line ignored\n");
    ERRORFLAG = true;

    return;
  } while (1);
}

#define NOTCH(CH) (CH == '\\' || CH == '~' && LEGACY)

// TOKEN: := CHAR | <certain digraphs, represented by nos above 256 > |
//          | CONS(IDENT, ATOM) | CONS(CONST, <ATOM | NUM>)
static TOKEN readtoken(void) {
  word CH = (*_RDCH)();

  while ((CH == ' ' || CH == '\t'))
    CH = (*_RDCH)();

  if (CH == '\n') {
    return (TOKEN)EOL;
  }

  if (CH == EOF) {
    return (TOKEN)ENDSTREAMCH;
  }

  if (('a' <= CH && CH <= 'z') || ('A' <= CH && CH <= 'Z') ||
      (EXPECTFILE && !isspace(CH))) {
    // expt to allow _ID, discontinued
    // ||(CH == '_' && peekalpha())

    do {
      BUFCH(CH);
      CH = (*_RDCH)();
    } while (('a' <= CH && CH <= 'z') || ('A' <= CH && CH <= 'Z') ||
             isdigit(CH) || CH == '\'' || CH == '_' ||
             (EXPECTFILE && !isspace(CH)));

    (*_UNRDCH)(CH);

    {
      LIST X = (LIST)PACKBUFFER();
      if (TOKENS != NIL && HD(TOKENS) == (TOKEN)'/' && TL(TOKENS) == NIL &&
          MEMBER(FILECOMMANDS, X)) {
        EXPECTFILE = true;
      }
      return CONS((LIST)IDENT, X);
    }
  }

  if (isdigit(CH)) {
    THE_NUM = 0;

    while (isdigit(CH)) {
      THE_NUM = THE_NUM * 10 + CH - '0';
      if (THE_NUM < 0) {
        bcpl_WRITES("\n**integer overflow**\n");
        ESCAPETONEXTCOMMAND();
      }
      CH = (*_RDCH)();
    }

    if (CH != EOF) {
      (*_UNRDCH)(CH);
    }

    return CONS((TOKEN)CONST, STONUM(THE_NUM));
  }

  if (CH == '"') {
    ATOM A;
    CH = (*_RDCH)();

    while (!(CH == '"' || CH == '\n' || CH == EOF)) {
      // add C escape chars, DT 2015
      if (CH == '\\') {
        CH = (*_RDCH)();
        switch (CH) {
        case 'a':
          BUFCH('\a');
          break;
        case 'b':
          BUFCH('\b');
          break;
        case 'f':
          BUFCH('\f');
          break;
        case 'n':
          BUFCH('\n');
          break;
        case 'r':
          BUFCH('\r');
          break;
        case 't':
          BUFCH('\t');
          break;
        case 'v':
          BUFCH('\v');
          break;
        case '\\':
          BUFCH('\\');
          break;
        case '\'':
          BUFCH('\'');
          break;
        case '\"':
          BUFCH('\"');
          break;
        case '\n':
          return (TOKEN)BADTOKEN;
        default:
          if ('0' <= CH && CH <= '9') {
            int i = 3, n = CH - '0', n1;
            CH = (*_RDCH)();
            while (--i && '0' <= CH && CH <= '9' &&
                   (n1 = 10 * n + CH - '0') < 256)
              n = n1, CH = (*_RDCH)();
            BUFCH(n);
            (*_UNRDCH)(CH);
          }
        }
      } else
        BUFCH(CH);
      CH = (*_RDCH)();
    }
    A = PACKBUFFER();
    return CH != '"' ? (TOKEN)BADTOKEN : CONS(CONST, (LIST)A);
  }
  {
    word CH2 = (*_RDCH)();
    if (CH == ':' && CH2 == '-' && TOKENS != NIL && ISCONS(HD(TOKENS)) &&
        HD(HD(TOKENS)) == IDENT && TL(TOKENS) == NIL) {
      LIST C = NIL;
      LIST SUBJECT = TL(HD(TOKENS));
      COMMENTFLAG = 1;

      CH = (*_RDCH)();

      // ignore blank lines
      while (CH == '\n') {
        COMMENTFLAG++, CH = (*_RDCH)();
      }

      // option - s
      if (SKIPCOMMENTS) {
        while (!(CH == ';' || CH == EOF)) {
          if (CH == '\n') {
            COMMENTFLAG++;
          }
          CH = (*_RDCH)();
        }
        return NIL;
      }

      if (CH == ';') {
        return NIL;
      }

      while (!(CH == ';' || CH == EOF))
        if (CH == '\n') {
          C = CONS((LIST)PACKBUFFER(), C);
          do {
            COMMENTFLAG++;
            CH = (*_RDCH)();
          } while (CH == '\n');
          // IGNORE BLANK LINES
        } else {
          BUFCH(CH);
          CH = (*_RDCH)();
        }

      if (CH == EOF) {
        fprintf(bcpl_OUTPUT, "%s :- ...", PRINTNAME((ATOM)SUBJECT)),
            bcpl_WRITES(" missing \";\"\n");
        COMMENTFLAG--;
        syntax();
      } else {
        C = CONS((LIST)PACKBUFFER(), C);
      }

      return REVERSE(C);
    }

    if (CH == CH2) {
      if (CH == '+')
        return PLUSPLUS_SY;
      if (CH == '.')
        return DOTDOT_SY;
      if (CH == '-')
        return DASHDASH_SY;
      if (CH == '*')
        return STARSTAR_SY;

      // added DT 2015
      if (CH == '=')
        return EQ_SY;

      // comment to end of line(new)
      if (CH == '|')
        do {
          CH = (*_RDCH)();
          if (CH == '\n')
            return EOL;
          if (CH == EOF)
            return ENDSTREAMCH;
        } while (1);
    }

    if (CH == '<' && '-' == CH2) {
      return BACKARROW_SY;
    }

    if (CH2 == '=') {
      if (CH == '>')
        return GE_SY;
      if (CH == '<')
        return LE_SY;
      if (NOTCH(CH))
        return NE_SY;
    }

    (*_UNRDCH)(CH2);

    if (CH == '?' || CH == '!') {
      EXPFLAG = true;
    }

    if (CH == '=' && !LEGACY) {
      EQNFLAG = true;
    }

    // GCC warning expected
    return (TOKEN)(NOTCH(CH) ? '\\' : CH);
  }
}

word caseconv(word CH) { return tolower(CH); }

static word peekalpha() {
  word CH = (*_RDCH)();
  (*_UNRDCH)(CH);
  return (('a' <= CH && CH <= 'z') || ('A' <= CH && CH <= 'Z'));
}

void writetoken(TOKEN T) {
  if (T < (TOKEN)256 && T > (TOKEN)32) {
    (*_WRCH)((word)T);
  } else {
    switch ((word)T) {
    case (word)'\n':
      bcpl_WRITES("newline");
      break;
    case (word)PLUSPLUS_SY:
      bcpl_WRITES("++");
      break;
    case (word)DASHDASH_SY:
      bcpl_WRITES("--");
      break;
    case (word)STARSTAR_SY:
      bcpl_WRITES("**");
      break;
    case (word)GE_SY:
      bcpl_WRITES(">=");
      break;
    case (word)LE_SY:
      bcpl_WRITES("<=");
      break;
    case (word)NE_SY:
      bcpl_WRITES("\\=");
      break;
    case (word)EQ_SY:
      bcpl_WRITES("==");
      break;
    case (word)BACKARROW_SY:
      bcpl_WRITES("<-");
      break;
    case (word)DOTDOT_SY:
      bcpl_WRITES("..");
      break;
    default:
      if (!(ISCONS(T) && (HD(T) == IDENT || HD(T) == CONST)))
        fprintf(bcpl_OUTPUT, "<UNKNOWN TOKEN<%p>>", T);
      else if (HD(T) == IDENT)
        bcpl_WRITES(PRINTNAME((ATOM)(
            ISCONS(TL(T)) && HD(TL(T)) == (LIST)ALPHA ? TL(TL(T)) : TL(T))));
      else if (ISNUM(TL(T)))
        bcpl_WRITEN(GETNUM(TL(T)));
      else
        fprintf(bcpl_OUTPUT, "\"%s\"", PRINTNAME((ATOM)TL(T)));
    }
  }
}

bool have(TOKEN T) {
  if (TOKENS == NIL || HD(TOKENS) != T) {
    return false;
  }

  TOKENS = TL(TOKENS);
  return true;
}

void check(TOKEN T) {
  if (have(T)) {
    return;
  }

  ERRORFLAG = true;

  if (MISSING == 0) {
    MISSING = T;
  }
}

void syntax() { ERRORFLAG = true; }

word haveid() {
  while (!(ISCONS(HD(TOKENS)) && HD(HD(TOKENS)) == IDENT)) {
    return false;
  }

  THE_ID = (ATOM)TL(HD(TOKENS));
  TOKENS = TL(TOKENS);

  return true;
}

word haveconst() {
  while (!(ISCONS(HD(TOKENS)) && HD(HD(TOKENS)) == CONST)) {
    return false;
  }

  THE_CONST = TL(HD(TOKENS));
  TOKENS = TL(TOKENS);

  return true;
}

word havenum() {
  while (!(ISCONS(HD(TOKENS)) && HD(HD(TOKENS)) == CONST &&
           ISNUM(TL(HD(TOKENS))))) {
    return false;
  }

  THE_NUM = GETNUM(TL(HD(TOKENS)));
  TOKENS = TL(TOKENS);

  return true;
}

// syntax error diagnosis(needs refining)
void syntax_error(char *message) {
  // unclosed string quotes
  if (ISCONS(TOKENS) && HD(TOKENS) != BADTOKEN) {
    bcpl_WRITES("**unexpected `"), writetoken(HD(TOKENS)), (*_WRCH)('\'');
    if (MISSING && MISSING != EOL && MISSING != (TOKEN)';' &&
        MISSING != (TOKEN)'\'') {

      bcpl_WRITES(", missing `"), writetoken(MISSING), (*_WRCH)('\'');

      if (MISSING == (TOKEN)'?') {
        bcpl_WRITES(" or `!'");
      }
    }
    (*_WRCH)('\n');
  }
  bcpl_WRITES(message);
}
