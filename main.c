#include "listlib.h"
#include "compiler.h"
#include "reducer.h"
#include "revision"
#include "linenoise.h"

//----------------------------------------------------------------------
// The KRC system is Copyright (c) D. A. Turner 1981
// All  rights reserved.  It is distributed as free software under the
// terms in the file "COPYING", which is included in the distribution.
//----------------------------------------------------------------------

//#include <ctype.h>	// for toupper()
#include <setjmp.h>
#include <string.h>    // for strcmp()
#include <unistd.h>    // for fork(), stat()
#include <sys/types.h> // for sys/wait.h, stat()
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>

// local function declarations
static void DIRCOM(), DISPLAYCOM(), QUITCOM(), OBJECTCOM();
static void RESETCOM(), GCCOM(), COUNTCOM(), SAVECOM(), FILECOM(), GETCOM();
static void LISTCOM(), NAMESCOM(), LIBCOM(), CLEARCOM(), OPENLIBCOM();
static void HELPCOM(), RENAMECOM(), ABORDERCOM(), REORDERCOM(), DELETECOM();
static bool STARTDISPLAYCOM();

static void PARSELINE(char *line);
static void INITIALISE();
static void ENTERARGV(int USERARGC, LIST USERARGV);
static void SETUP_COMMANDS();
static void COMMAND();
static void DISPLAYALL(bool DOUBLESPACING);
static bool MAKESURE();
static void FILENAME();
static bool OKFILE(FILE *STR, char *FILENAME);
static void CHECK_HITS();
static bool GETFILE(char *FILENAME);
static void FIND_UNDEFS();
static bool ISDEFINED(ATOM X);
static void SCRIPTLIST(LIST S);
static LIST SUBST(LIST Z, LIST A);
static void NEWEQUATION();
static void CLEARMEMORY();
static void COMMENT();
static void EVALUATION();
static LIST SORT(LIST X);
static void SCRIPTREORDER();
static word NO_OF_EQNS(ATOM A);
static bool PROTECTED(ATOM A);
static bool PRIMITIVE(ATOM A);
static void REMOVE(ATOM A);
static LIST EXTRACT(ATOM A, ATOM B);

// bases
static LIST COMMANDS = NIL, SCRIPT = NIL, OUTFILES = NIL;
static ATOM LASTFILE = 0;
static LIST LIBSCRIPT = NIL, HOLDSCRIPT = NIL, GET_HITS = NIL;

static bool SIGNOFF = false, SAVED = true, EVALUATING = false;

// flags used in debugging system
static bool ATOBJECT = false, ATCOUNT = false;

// for calling emas
static char PARAMV[256];

// global variables owned by main.c

// set by -z option
bool LEGACY = false;

LIST FILECOMMANDS = NIL;

// SET BY -s OPTION
bool SKIPCOMMENTS;

// SET BY -l OPTION
char *USERLIB = NULL;

// local variables

// are we evaluating with '?' ?
static bool FORMATTING;

// suppress greetings, prompts etc.?
static bool QUIET = false;

// expression to execute in batch mode
static char *EVALUATE = NULL;

// initialisation and steering

void ESCAPETONEXTCOMMAND();

// are we ignoring interrupts?
static bool INTERRUPTS_ARE_HELD = false;

// was an interrupt delivered while we were ignoring them?
static bool INTERRUPT_OCCURRED = false;

static void CATCHINTERRUPT(int signum) {
  if (INTERRUPTS_ARE_HELD) {

    // can't be 0
    INTERRUPT_OCCURRED = signum;
    return;
  }

  // in case interrupt struck while reduce was dissecting a constant
  fixup_s();

  _WRCH = TRUEWRCH;
  CLOSECHANNELS();

  // die quietly if running as script or ABORT() called
  // bcpl_WRITES("\n**break in - return to KRC command level**\n");
  if (!(QUIET || ABORTED)) {
    bcpl_WRITES("<interrupt>\n");
  }

  ABORTED = false;
  ESCAPETONEXTCOMMAND();
}

void HOLD_INTERRUPTS() { INTERRUPTS_ARE_HELD = true; }

void RELEASE_INTERRUPTS() {
  INTERRUPTS_ARE_HELD = false;

  if (INTERRUPT_OCCURRED) {
    INTERRUPT_OCCURRED = false;
    CATCHINTERRUPT(INTERRUPT_OCCURRED);
  }
}

// essential that definitions of the above should be provided if
// the package is to be used in an interactive program

// where to jump back to on runtime errors or keyboard interrupts
static jmp_buf nextcommand;

void ESCAPETONEXTCOMMAND() {
  _WRCH = TRUEWRCH;

  if (bcpl_INPUT != stdin) {
    if (bcpl_INPUT_fp != stdin) {
      fclose(bcpl_INPUT_fp);
    }
    bcpl_INPUT_fp = (stdin);
  }

  CLOSECHANNELS();

  if (EVALUATING) {
    if (ATCOUNT) {
      outstats();
    }

    // in case some pointers have been left reversed
    CLEARMEMORY();

    EVALUATING = false;
  }

  if (HOLDSCRIPT != NIL) {
    SCRIPT = HOLDSCRIPT, HOLDSCRIPT = NIL;
    CHECK_HITS();
  }
  init_codev();
  init_argspace();
  longjmp(nextcommand, 1);
}

// buffer for signal handling
// all initialised to 0/NULL is fine.
static struct sigaction act;

// STACKLIMIT:= @V4 + 30000
// implementation dependent,to test for runaway recursion
void GO() {

  // first-time initialization
  if (setjmp(nextcommand) == 0) {

    init_codev();
    init_argspace();
    INITIALISE();

    // set up the interrupt handler
    act.sa_handler = CATCHINTERRUPT;
    act.sa_flags = SA_NODEFER; // Bcos the interrupt handler never returns
    sigaction(SIGINT, &act, NULL);

  } else {

    // when the GC is called from cons() from the depths of an
    // evaluation, it is more likely that stale pointers left in
    // registers, either still in them or saved on the stack,
    // will cause now-unused areas of the heap to be preserved.
    // we mitigate this by calling the GC here, after an interrupt
    // or an out-of-space condition, when the stack is shallow and
    // the registers are less likely to contain values pointing
    // inside the CONS space.
    bool HOLDATGC = ATGC;
    ATGC = false;
    FORCE_GC();
    ATGC = HOLDATGC;
  }

  // both initially and on longjump, continue here.
  if (EVALUATE && !SIGNOFF) {

    // quit on errors or interrupts
    SIGNOFF = true;

    PARSELINE(EVALUATE);
    if (EXPFLAG) {
      EVALUATION();
    } else {
      bcpl_WRITES("-e takes an expression followed by ? or !\n");
    }

    if (ERRORFLAG) {
      syntax_error("malformed expression after -e\n");
    }
  }

  while (!(SIGNOFF)) {
    COMMAND();
  }

  QUITCOM();

  // exit(0);
  // moved inside QUITCOM()
}

// PARSELINE: A version of readline that gets its input from a string

static char *input_line;

// alternative version of RDCH that gets its chars from a string
static int str_RDCH(void) {

  if (input_line == NULL) {
    return EOF;
  }

  if (*input_line == '\0') {
    input_line = NULL;
    return '\n';
  }
  return *input_line++;
}

static int str_UNRDCH(int c) {

  if (input_line == NULL && c == '\n') {
    input_line = "\n";
  } else {
    *(--input_line) = c;
  }

  return c;
}

// same as readline, but gets its input from a C string
static void PARSELINE(char *line) {
  input_line = line;
  _RDCH = str_RDCH, _UNRDCH = str_UNRDCH;
  readline();
  _RDCH = bcpl_RDCH, _UNRDCH = bcpl_UNRDCH;
}

// ----- end of PARSELINE

static char TITLE[] = "Kent Recursive Calculator 1.0";

// where to look for "prelude" and other files KRC needs
#ifndef LIBDIR
#define LIBDIR "/usr/local/lib/krc"
#endif
// but use krclib in current directory if present, see below

static void INITIALISE() {

  // do we need to read the prelude?
  bool LOADPRELUDE = true;

  // use legacy prelude?
  bool OLDLIB = false;

  // script given on command line
  char *USERSCRIPT = NULL;

  // reversed list of args after script name
  LIST USERARGV = NIL;

  // how many items in USERARGV?
  int USERARGC = 0;

  // list the script as we read it?
  // bool LISTSCRIPT = false;

  int I;

  if (!isatty(0)) {
    QUIET = true;
  }

  setup_primfns_etc();

  for (I = 1; I < ARGC; I++) {

    if (ARGV[I][0] == '-') {
      switch (ARGV[I][1]) {
      case 'n':
        LOADPRELUDE = false;
        break;
      case 's':
        SKIPCOMMENTS = true;
        break;
      case 'c':
        ATCOUNT = true;
        break;
      case 'o':
        ATOBJECT = true;
        break;
      case 'd': // handled in listlib.c
      case 'l': // handled in listlib.c
      case 'h':
        ++I;    // handled in listlib.c
      case 'g': // handled in listlib.c
        break;
      case 'e':
        if (++I >= ARGC || ARGV[I][0] == '-') {
          bcpl_WRITES("krc: -e What?\n");
          exit(0);
        }
        if (EVALUATE) {
          bcpl_WRITES("krc: Only one -e flag allowed\n");
          exit(0);
        }
        EVALUATE = ARGV[I];
        QUIET = true;
        break;
      case 'z':
        LISTBASE = 1;
        LEGACY = true;
        bcpl_WRITES("LISTBASE=1\n");
        break;
      case 'L':
        OLDLIB = 1;
        break;
        // case 'v': LISTSCRIPT=true; break;
        // other parameters may be detected using haveparam()
      case 'C':
      case 'N':
      case 'O': // used only by testcomp, disabled
      default:
        fprintf(bcpl_OUTPUT, "krc: invalid option -%c\n", ARGV[I][1]);
        exit(0);
        break;
      }
    } else {

      // filename of script to load, or arguments for script
      if (USERSCRIPT == NULL) {
        // was if ... else
        USERSCRIPT = ARGV[I];
      }

      USERARGV = cons((LIST)MKATOM(ARGV[I]), USERARGV), USERARGC++;
    }
  }
  if (EVALUATE) {

    ENTERARGV(USERARGC, USERARGV);

  } else if (USERARGC > 1) {

    bcpl_WRITES("krc: too many arguments\n");
    exit(0);
  }

  if (LOADPRELUDE) {
    if (USERLIB) {
      // -l option was used
      GETFILE(USERLIB);
    } else {
      struct stat buf;
      if (stat("krclib", &buf) == 0) {
        GETFILE(OLDLIB ? "krclib/lib1981" : "krclib/prelude");
      } else {
        GETFILE(OLDLIB ? LIBDIR "/lib1981" : LIBDIR "/prelude");
      }
    }
  } else {
    // if ( USERLIB || OLDLIB )
    // { bcpl_WRITES("krc: invalid combination -n and -l or -L\n"); exit(0); }
    // else
    bcpl_WRITES("\"PRELUDE\" suppressed\n");
  }

  // effective only for prelude
  SKIPCOMMENTS = false;

  LIBSCRIPT = SORT(SCRIPT), SCRIPT = NIL;

  if (USERSCRIPT) {

    // if ( LISTSCRIPT ) _RDCH=echo_RDCH;
    GETFILE(USERSCRIPT);
    SAVED = true;

    // if ( LISTSCRIPT ) _RDCH=bcpl_RDCH;
    LASTFILE = MKATOM(USERSCRIPT);
  }

  SETUP_COMMANDS();
  RELEASE_INTERRUPTS();

  if (!QUIET) {
    fprintf(bcpl_OUTPUT, "%s\nrevised %s\n%s\n", TITLE, revision,
            //                        "http://krc-lang.org",
            "/h for help");
  }
}

// given the (reverse-order) list of atoms made from command-line arguments
// supplied after the name of the script file, create their an entry in the
// script called "argv" for the krc program to access them.
// we create it as a list of strings (i.e. a list of atoms) for which
// the code for a three-element list of string is:
// ( (0x0.NIL).  :- 0 parameters, no comment
//   ( 0.      :- memo field unset
//     LOAD.(QUOTE."one").LOAD.(QUOTE."two").LOAD.(QUOTE."three").
//     FORMLIST.0x03.STOP.NIL ).
//   NIL )
static void ENTERARGV(int USERARGC, LIST USERARGV) {
  ATOM A = MKATOM("argv");
  LIST CODE =
      cons((LIST)FORMLIST_C, cons((LIST)USERARGC, cons((LIST)STOP_C, NIL)));

  for (; USERARGV != NIL; USERARGV = TL(USERARGV)) {
    CODE = cons((LIST)LOAD_C, cons(cons((LIST)QUOTE, HD(USERARGV)), CODE));
  }

  VAL(A) = cons(cons((LIST)0, NIL), cons(cons((LIST)0, CODE), NIL));
  ENTERSCRIPT(A);
}

void SPACE_ERROR(char *MESSAGE) {
  _WRCH = TRUEWRCH;
  CLOSECHANNELS();

  if (EVALUATING) {

    fprintf(bcpl_OUTPUT, "\n**%s**\n**evaluation abandoned**\n", MESSAGE);
    ESCAPETONEXTCOMMAND();

  } else if (MEMORIES == NIL) {

    fprintf(bcpl_OUTPUT, "\n%s - recovery impossible\n", MESSAGE);
    exit(0);

  } else {

    // let go of memos and try to carry on
    CLEARMEMORY();
  }
}

void BASES(void (*F)(LIST *)) {

  // In reducer.c
  extern LIST S;

  F(&COMMANDS);
  F(&FILECOMMANDS);
  F(&SCRIPT);
  F(&LIBSCRIPT);
  F(&HOLDSCRIPT);
  F(&GET_HITS);
  F((LIST *)&LASTFILE);
  F(&OUTFILES);
  F(&MEMORIES);
  F(&S);
  F(&TOKENS);
  F((LIST *)&THE_ID);
  F(&THE_CONST);
  F(&LASTLHS);
  F(&TRUTH);
  F(&FALSITY);
  F(&INFINITY);
  compiler_bases(F);
  reducer_bases(F);
}

static void SETUP_COMMANDS() {
#define F(S, R)                                                                \
  { COMMANDS = cons(cons((LIST)MKATOM(S), (LIST)R), COMMANDS); }
#define FF(S, R)                                                               \
  {                                                                            \
    FILECOMMANDS = cons((LIST)MKATOM(S), FILECOMMANDS);                        \
    F(S, R);                                                                   \
  }

  F("delete", DELETECOM);
  F("d", DELETECOM); // synonym
  F("reorder", REORDERCOM);
  FF("save", SAVECOM);
  FF("get", GETCOM);
  FF("list", LISTCOM);
  FF("file", FILECOM);
  FF("f", FILECOM);
  F("dir", DIRCOM);
  F("quit", QUITCOM);
  F("q", QUITCOM); // synonym
  F("names", NAMESCOM);
  F("lib", LIBCOM);
  F("aborder", ABORDERCOM);
  F("rename", RENAMECOM);
  F("openlib", OPENLIBCOM);
  F("clear", CLEARCOM);
  F("help", HELPCOM);
  F("h", HELPCOM);        // synonym
  F("object", OBJECTCOM); // these last commands are for use in
  F("reset", RESETCOM);   // debugging the system
  F("gc", GCCOM);
  F("dic", REPORTDIC);
  F("count", COUNTCOM);
  F("lpm", LISTPM);
#undef FF
#undef F
}

static void DIRCOM() {
  int status;
  switch (fork()) {
  case 0:
    execlp("ls", "ls", NULL);
    break;
  case -1:
    break;
  default:
    wait(&status);
  }
}

void CLOSECHANNELS() {
  if (!EVALUATING && bcpl_OUTPUT != stdout) {
    if (bcpl_OUTPUT != stdout) {
      fclose(bcpl_INPUT_fp);
    }
  }
  while (!(OUTFILES == NIL)) {
    bcpl_OUTPUT_fp = ((FILE *)TL(HD(OUTFILES)));
    if (FORMATTING) {
      (*_WRCH)('\n');
    }
    if (bcpl_OUTPUT != stdout) {
      fclose(bcpl_INPUT_fp);
    }
    OUTFILES = TL(OUTFILES);
  }
  bcpl_OUTPUT_fp = (stdout);
}

FILE *FINDCHANNEL(char *F) {
  LIST P = OUTFILES;

  while (!(P == NIL || strcmp((char *)HD(HD(P)), F) == 0)) {
    P = TL(P);
  }

  if (P == NIL) {
    FILE *OUT = bcpl_FINDOUTPUT(F);

    if (OUT != NULL) {
      OUTFILES = cons(cons((LIST)F, (LIST)OUT), OUTFILES);
    }

    return OUT;
  } else {
    return (FILE *)TL(HD(P));
  }
}

// COMMAND INTERPRETER
// each command is terminated by a newline
// <COMMAND>::= /<EMPTY> |    (displays whole script)
//              /DELETE <THINGY>* |
//                  (if no <THINGY>'s are specified it deletes whole script)
//              /DELETE <NAME> <PART>* |
//              /REORDER <THINGY>* |
//              /REORDER <NAME> <PART>* |
//              /ABORDER |
//              /SAVE "<FILENAME>" |
//              /GET "<FILENAME>" |
//              /LIST "<FILENAME>" |
//              /FILE  |
//              /QUIT  |
//              /NAMES |
//              /OPEN|
//              /CLEAR |
//              /LIB   |
//              <NAME> |     (displays EQNS for this name)
//              <NAME> .. <NAME> |    (displays a section of the script)
//              <EXP>? |     (evaluate and print)
//              <EXP>! |     (same but with unformatted printing)
//              <EQUATION>    (add to script)
// <THINGY> ::= <NAME> | <NAME> .. <NAME> | <NAME> ..
// <PART> ::= <INT> | <INT>..<INT> | <INT>..

#define KRCPAGER "less -F -X -P'%F (press q to quit)' "
#define HELPLOCAL KRCPAGER "krclib/help/"
#define HELP KRCPAGER LIBDIR "/help/"
#define BUFLEN 80

static void HELPCOM() {
  struct stat buf;
  char strbuf[BUFLEN + 1], *topic;
  int local = stat("krclib", &buf) == 0, r;
  if (have(EOL)) {

    if (local) {
      r = system(HELPLOCAL "menu");
    } else {
      r = system(HELP "menu");
    }

    return;
  }
  topic = haveid() ? PRINTNAME(THE_ID) : NULL;
  if (!(topic && have(EOL))) {
    bcpl_WRITES("/h What? `/h' for options\n");
    return;
  }
  strncpy(strbuf, local ? HELPLOCAL : HELP, BUFLEN);
  strncat(strbuf, topic, BUFLEN - strlen(strbuf));
  r = system(strbuf);
}

static void COMMAND() {
  static char prompt[] = "krc> ";

  char *line = linenoise(QUIET ? "" : prompt);

  if (line && line[0] == '\0') {
    // otherwise the interpreter exits
    return;
  }

  // handles NULL->EOF OK
  PARSELINE(line);

  // ignore blank lines
  if (have(EOL)) {
    free(line);
    return;
  }

  if (line) {
    linenoiseHistoryAdd(line);
    free(line);
  }

  if (have((TOKEN)EOF)) {

    SIGNOFF = true;

  } else if (have((TOKEN)'/')) {

    if (have(EOL)) {

      DISPLAYALL(false);
      // if ( have((TOKEN)'@') && have(EOL) ) LISTPM(); else
      // for debugging the system

    } else {

      LIST P = COMMANDS;

      if (haveid()) {
        THE_ID = MKATOM(scaseconv(PRINTNAME(THE_ID)));
        // always accept commands in either case
      } else {
        P = NIL;
      }

      while (!(P == NIL || THE_ID == (ATOM)HD(HD(P)))) {
        P = TL(P);
      }

      if (P == NIL) {

        bcpl_WRITES("command not recognised\nfor help type /h\n");

      } else {

        // see "SETUP_COMMANDS()"
        ((void (*)())TL(HD(P)))();
      }
    }

  } else if (STARTDISPLAYCOM()) {

    DISPLAYCOM();

  } else if (COMMENTFLAG > 0) {

    COMMENT();

  } else if (EQNFLAG) {

    NEWEQUATION();

  } else {

    EVALUATION();
  }

  if (ERRORFLAG) {
    syntax_error("**syntax error**\n");
  }
}

static bool STARTDISPLAYCOM() {
  LIST HOLD = TOKENS;
  word R = haveid() && (have(EOL) || have((TOKEN)DOTDOT_SY));
  TOKENS = HOLD;
  return R;
}

static void DISPLAYCOM() {
  if (haveid()) {

    if (have(EOL)) {

      display(THE_ID, true, false);

    } else if (have((TOKEN)DOTDOT_SY)) {

      ATOM A = THE_ID;
      LIST X = NIL;

      // BUG?
      ATOM B = have(EOL) ? (ATOM)EOL : haveid() && have(EOL) ? THE_ID : 0;
      if (B == 0) {
        syntax();
      } else {
        X = EXTRACT(A, B);
      }

      while (!(X == NIL)) {
        display((ATOM)HD(X), false, false);
        X = TL(X);
      }

      // could insert extra line here between groups
    } else {
      syntax();
    }

  } else {
    syntax();
  }
}

// "SCRIPT" is a list of all user defined names in alphabetical order
static void DISPLAYALL(bool DOUBLESPACING) {
  LIST P = SCRIPT;
  if (P == NIL) {
    bcpl_WRITES("Script=empty\n");
  }

  while (!(P == NIL)) {
    // don't display builtin fns (relevant only in /openlib)
    if (!(PRIMITIVE((ATOM)HD(P)))) {
      display((ATOM)HD(P), false, false);
    }

    P = TL(P);

    // extra line between groups
    if (DOUBLESPACING && P != NIL) {
      (*_WRCH)('\n');
    }
  }
}

static bool PRIMITIVE(ATOM A) {
  if (TL(VAL(A)) == NIL) {
    // A has comment but no eqns
    return false;
  }
  return HD(TL(HD(TL(VAL(A))))) == (LIST)CALL_C;
}

static void QUITCOM() {
  if (TOKENS != NIL) {
    check(EOL);
  }

  if (ERRORFLAG) {
    return;
  }

  if (MAKESURE()) {
    bcpl_WRITES("krc logout\n");
    exit(0);
  }
}

static bool MAKESURE() {
  if (SAVED || SCRIPT == NIL) {
    return true;
  }

  bcpl_WRITES("Are you sure? ");

  {
    word CH = (*_RDCH)(), C;
    (*_UNRDCH)(CH);
    while (!((C = (*_RDCH)()) == '\n' || C == EOF)) {
      continue;
    }

    if (CH == 'y' || CH == 'Y') {
      return true;
    }

    bcpl_WRITES("Command ignored\n");

    return false;
  }
}

static void OBJECTCOM() { ATOBJECT = true; }

static void RESETCOM() { ATOBJECT = false, ATCOUNT = false, ATGC = false; }

static void GCCOM() {
  ATGC = true;
  FORCE_GC();
}

static void COUNTCOM() { ATCOUNT = true; }

static void SAVECOM() {
  FILENAME();
  if (ERRORFLAG) {
    return;
  }

  if (SCRIPT == NIL) {
    bcpl_WRITES("Cannot save empty script\n");
    return;
  }

  {
    FILE *OUT = bcpl_FINDOUTPUT("T#SCRIPT");
    bcpl_OUTPUT_fp = (OUT);
    DISPLAYALL(true);
    if (bcpl_OUTPUT != stdout) {
      fclose(bcpl_INPUT_fp);
    }
    bcpl_OUTPUT_fp = (stdout);

    // copy T#SCRIPT back to the save file.
    {
      int status;
      switch (fork()) {
      case 0:
        // child process
        execlp("mv", "mv", "T#SCRIPT", PRINTNAME(THE_ID), (char *)0);
      default:
        // parent process
        wait(&status);
        if (status == 0) {
          SAVED = true;
          bcpl_WRITES("File saved in T#SCRIPT.\n");
        }
        // else /* Drop into... */
        break;
      case -1:
        // fork failed
        break;
      }
    }
  }
}

static void FILENAME() {
  if (have(EOL)) {
    if (LASTFILE == 0) {
      bcpl_WRITES("(No file set)\n");
      syntax();
    } else {
      THE_ID = LASTFILE;
    }
  } else if (haveid() && have(EOL)) {
    LASTFILE = THE_ID;
  } else {
    if (haveconst() && have(EOL) && !isnum(THE_CONST)) {
      bcpl_WRITES("(Warning - quotation marks no longer expected around "
                  "filenames in file commands - DT, Nov 81)\n");
    }
    syntax();
  }
}

static void FILECOM() {
  if (have(EOL)) {
    if (LASTFILE == 0) {
      bcpl_WRITES("No files used\n");
    } else {
      fprintf(bcpl_OUTPUT, "File = %s\n", PRINTNAME(LASTFILE));
    }
  } else {
    FILENAME();
  }
}

static bool OKFILE(FILE *STR, char *FILENAME) {
  if (STR != NULL) {
    return true;
  }

  fprintf(bcpl_OUTPUT, "Cannot open \"%s\"\n", FILENAME);
  return false;
}

static void GETCOM() {
  bool CLEAN = SCRIPT == NIL;

  FILENAME();
  if (ERRORFLAG) {
    return;
  }

  HOLDSCRIPT = SCRIPT, SCRIPT = NIL, GET_HITS = NIL;
  GETFILE(PRINTNAME(THE_ID));
  CHECK_HITS();
  SCRIPT = APPEND(HOLDSCRIPT, SCRIPT), SAVED = CLEAN, HOLDSCRIPT = NIL;
}

static void CHECK_HITS() {
  if (!(GET_HITS == NIL)) {
    bcpl_WRITES("Warning - /get has overwritten or modified:\n");
    SCRIPTLIST(REVERSE(GET_HITS));
    GET_HITS = NIL;
  }
}

static bool GETFILE(char *FILENAME) {
  FILE *IN = bcpl_FINDINPUT(FILENAME);
  if (!(OKFILE(IN, FILENAME))) {
    return false;
  }

  bcpl_INPUT_fp = (IN);

  {
    // to locate line number of error in file
    int line = 0;

    do {
      line++;
      readline();

      if (ferror(IN)) {
        ERRORFLAG = true;
        break;
      }

      if (have(EOL)) {
        continue;
      }

      if (HD(TOKENS) == ENDSTREAMCH) {
        break;
      }

      if (COMMENTFLAG) {
        line += (COMMENTFLAG - 1);
        COMMENT();
      } else {
        NEWEQUATION();
      }

      if (ERRORFLAG) {
        syntax_error("**syntax error in file ");
        fprintf(bcpl_OUTPUT, "%s at line %d\n", FILENAME, line);
      }
    } while (1);

    if (bcpl_INPUT_fp != stdin) {
      fclose(bcpl_INPUT_fp);
    }

    bcpl_INPUT_fp = (stdin);
    LASTLHS = NIL;
    return true;
  }
}

static void LISTCOM() {
  FILENAME();

  if (ERRORFLAG) {
    return;
  }

  {
    char *FNAME = PRINTNAME(THE_ID);
    FILE *IN = bcpl_FINDINPUT(FNAME);

    if (!(OKFILE(IN, FNAME))) {
      return;
    }

    bcpl_INPUT_fp = (IN);

    {
      word CH = (*_RDCH)();

      while (!(CH == EOF)) {
        (*_WRCH)(CH);
        CH = (*_RDCH)();
      }

      if (bcpl_INPUT_fp != stdin) {
        fclose(bcpl_INPUT_fp);
      }

      bcpl_INPUT_fp = (stdin);
    }
  }
}

static void NAMESCOM() {
  check(EOL);
  if (ERRORFLAG) {
    return;
  }

  if (SCRIPT == NIL) {
    DISPLAYALL(false);
  } else {
    SCRIPTLIST(SCRIPT);
    FIND_UNDEFS();
  }
}

// searches the script for names used but not defined
static void FIND_UNDEFS() {
  LIST S = SCRIPT, UNDEFS = NIL;
  while (!(S == NIL)) {
    LIST EQNS = TL(VAL((ATOM)HD(S)));
    while (!(EQNS == NIL)) {
      LIST CODE = TL(HD(EQNS));
      while (iscons(CODE)) {
        LIST A = HD(CODE);

        if (isatom(A) && !ISDEFINED((ATOM)A) && !MEMBER(UNDEFS, A)) {
          UNDEFS = cons(A, UNDEFS);
        }

        CODE = TL(CODE);
      }
      EQNS = TL(EQNS);
    }
    S = TL(S);
  }
  if (!(UNDEFS == NIL)) {
    bcpl_WRITES("\nNames used but not defined:\n");
    SCRIPTLIST(REVERSE(UNDEFS));
  }
}

static bool ISDEFINED(ATOM X) {
  return VAL(X) == NIL || TL(VAL(X)) == NIL ? false : true;
}

static void LIBCOM() {
  check(EOL);

  if (ERRORFLAG) {
    return;
  }

  if (LIBSCRIPT == NIL) {
    bcpl_WRITES("library = empty\n");
  } else {
    SCRIPTLIST(LIBSCRIPT);
  }
}

static void CLEARCOM() {
  check(EOL);

  if (ERRORFLAG) {
    return;
  }

  CLEARMEMORY();
}

static void SCRIPTLIST(LIST S) {
  word COL = 0, I = 0;

// the minimum of various devices
#define LINEWIDTH 68

  while (!(S == NIL)) {
    char *N = PRINTNAME((ATOM)HD(S));

    if (PRIMITIVE((ATOM)HD(S))) {
      S = TL(S);
      continue;
    }

    COL = COL + strlen(N) + 1;
    if (COL > LINEWIDTH) {
      COL = 0;
      (*_WRCH)('\n');
    }

    bcpl_WRITES(N);
    (*_WRCH)(' ');
    I = I + 1, S = TL(S);
  }

  if (COL + 6 > LINEWIDTH) {
    (*_WRCH)('\n');
  }

  fprintf(bcpl_OUTPUT, " (%" W ")\n", I);
}

static void OPENLIBCOM() {
  check(EOL);

  if (ERRORFLAG) {
    return;
  }

  SAVED = SCRIPT == NIL;
  SCRIPT = APPEND(SCRIPT, LIBSCRIPT);
  LIBSCRIPT = NIL;
}

static void RENAMECOM() {
  LIST X = NIL, Y = NIL, Z = NIL;

  while (haveid()) {
    X = cons((LIST)THE_ID, X);
  }

  check((TOKEN)',');

  while (haveid()) {
    Y = cons((LIST)THE_ID, Y);
  }

  check(EOL);
  if (ERRORFLAG) {
    return;
  }

  // first check lists are of same length
  {
    LIST X1 = X, Y1 = Y;

    while (!(X1 == NIL || Y1 == NIL)) {
      Z = cons(cons(HD(X1), HD(Y1)), Z), X1 = TL(X1), Y1 = TL(Y1);
    }

    if (!(X1 == NIL && Y1 == NIL && Z != NIL)) {
      syntax();
      return;
    }
  }

  // now check legality of rename
  {
    LIST Z1 = Z, POSTDEFS = NIL, DUPS = NIL;

    while (!(Z1 == NIL)) {
      if (MEMBER(SCRIPT, HD(HD(Z1)))) {
        POSTDEFS = cons(TL(HD(Z1)), POSTDEFS);
      }

      if (ISDEFINED((ATOM)TL(HD(Z1))) &&
          (!MEMBER(X, TL(HD(Z1))) || !MEMBER(SCRIPT, TL(HD(Z1))))) {
        POSTDEFS = cons(TL(HD(Z1)), POSTDEFS);
      }

      Z1 = TL(Z1);
    }

    while (!(POSTDEFS == NIL)) {

      if (MEMBER(TL(POSTDEFS), HD(POSTDEFS)) && !MEMBER(DUPS, HD(POSTDEFS))) {
        DUPS = cons(HD(POSTDEFS), DUPS);
      }

      POSTDEFS = TL(POSTDEFS);
    }

    if (!(DUPS == NIL)) {
      bcpl_WRITES("/rename illegal because of conflicting uses of ");

      while (!(DUPS == NIL)) {
        bcpl_WRITES(PRINTNAME((ATOM)HD(DUPS)));
        (*_WRCH)(' ');
        DUPS = TL(DUPS);
      }

      (*_WRCH)('\n');
      return;
    }
  }

  HOLD_INTERRUPTS();
  CLEARMEMORY();

  // prepare for assignment to val fields
  {
    LIST X1 = X, XVALS = NIL, TARGETS = NIL;
    while (!(X1 == NIL)) {

      if (MEMBER(SCRIPT, HD(X1))) {
        XVALS = cons(VAL((ATOM)HD(X1)), XVALS), TARGETS = cons(HD(Y), TARGETS);
      }

      X1 = TL(X1), Y = TL(Y);
    }

    // now convert all occurrences in the script
    {
      LIST S = SCRIPT;
      while (!(S == NIL)) {
        LIST EQNS = TL(VAL((ATOM)HD(S)));
        word NARGS = (word)HD(HD(VAL((ATOM)HD(S))));
        while (!(EQNS == NIL)) {
          LIST CODE = TL(HD(EQNS));
          if (NARGS > 0) {
            LIST LHS = HD(HD(EQNS));
            word I;

            for (I = 2; I <= NARGS; I++) {
              LHS = HD(LHS);
            }

            HD(LHS) = SUBST(Z, HD(LHS));
          }

          while (iscons(CODE)) {
            HD(CODE) = SUBST(Z, HD(CODE)), CODE = TL(CODE);
          }

          EQNS = TL(EQNS);
        }

        if (MEMBER(X, HD(S))) {
          VAL((ATOM)HD(S)) = NIL;
        }

        HD(S) = SUBST(Z, HD(S));
        S = TL(S);
      }

      // now reassign val fields
      while (!(TARGETS == NIL)) {
        VAL((ATOM)HD(TARGETS)) = HD(XVALS);
        TARGETS = TL(TARGETS), XVALS = TL(XVALS);
      }

      RELEASE_INTERRUPTS();
    }
  }
}

static LIST SUBST(LIST Z, LIST A) {
  while (!(Z == NIL)) {
    if (A == HD(HD(Z))) {
      SAVED = false;
      return TL(HD(Z));
    }
    Z = TL(Z);
  }
  return A;
}

static void NEWEQUATION() {

  word EQNO = -1;

  if (havenum()) {
    EQNO = 100 * THE_NUM + THE_DECIMALS;
    check((TOKEN)')');
  }

  {
    LIST X = equation();
    if (ERRORFLAG) {
      return;
    }

    {
      ATOM SUBJECT = (ATOM)HD(X);
      word NARGS = (word)HD(TL(X));
      LIST EQN = TL(TL(X));
      if (ATOBJECT) {
        PRINTOB(EQN);
        (*_WRCH)('\n');
      }

      if (VAL(SUBJECT) == NIL) {
        VAL(SUBJECT) = cons(cons((LIST)NARGS, NIL), cons(EQN, NIL));
        ENTERSCRIPT(SUBJECT);
      } else if (PROTECTED(SUBJECT)) {
        return;
      } else if (TL(VAL(SUBJECT)) == NIL) {
        // subject currently defined only by a comment
        HD(HD(VAL(SUBJECT))) = (LIST)NARGS;
        TL(VAL(SUBJECT)) = cons(EQN, NIL);
      } else if (NARGS != (word)HD(HD(VAL(SUBJECT)))) {

        // simple def silently overwriting existing EQNS -
        // removed DT 2015
        // if ( NARGS==0) {
        // VAL(SUBJECT)=cons(cons(0,TL(HD(VAL(SUBJECT)))),cons(EQN,NIL));
        //            CLEARMEMORY(); } else

        fprintf(bcpl_OUTPUT, "Wrong no of args for \"%s\"\n",
                PRINTNAME(SUBJECT));
        bcpl_WRITES("Equation rejected\n");
        return;
      } else if (EQNO == -1) {
        // unnumbered EQN
        LIST EQNS = TL(VAL(SUBJECT));
        LIST P = profile(EQN);

        do {
          if (EQUAL(P, profile(HD(EQNS)))) {
            LIST CODE = TL(HD(EQNS));
            if (HD(CODE) == (LIST)LINENO_C) {
              // if old EQN has line no,

              // new EQN inherits
              TL(TL(CODE)) = TL(EQN);

              HD(HD(EQNS)) = HD(EQN);
            } else {
              HD(EQNS) = EQN;
            }
            CLEARMEMORY();
            break;
          }
          if (TL(EQNS) == NIL) {
            TL(EQNS) = cons(EQN, NIL);
            break;
          }
          EQNS = TL(EQNS);
        } while (1);

      } else {
        // numbered EQN

        LIST EQNS = TL(VAL(SUBJECT));
        word N = 0;
        if (EQNO % 100 != 0 || EQNO == 0) {
          // if EQN has non standard lineno

          // mark with no.
          TL(EQN) = cons((LIST)LINENO_C, cons((LIST)EQNO, TL(EQN)));
        }

        do {
          N = HD(TL(HD(EQNS))) == (LIST)LINENO_C ? (word)HD(TL(TL(HD(EQNS))))
                                                 : (N / 100 + 1) * 100;
          if (EQNO == N) {
            HD(EQNS) = EQN;
            CLEARMEMORY();
            break;
          }

          if (EQNO < N) {
            LIST HOLD = HD(EQNS);
            HD(EQNS) = EQN;
            TL(EQNS) = cons(HOLD, TL(EQNS));
            CLEARMEMORY();
            break;
          }

          if (TL(EQNS) == NIL) {
            TL(EQNS) = cons(EQN, NIL);
            break;
          }
          EQNS = TL(EQNS);
        } while (1);
      }
      SAVED = false;
    }
  }
}

// called whenever eqns are destroyed,reordered or
// inserted (other than at the end of a definition)
static void CLEARMEMORY() {

  // memories holds a list of all vars whose memo
  while (!(MEMORIES == NIL)) {

    // fields have been set
    LIST X = VAL((ATOM)HD(MEMORIES));

    if (!(X == NIL)) {
      // unset memo field
      HD(HD(TL(X))) = 0;
    }

    MEMORIES = TL(MEMORIES);
  }
}

// enters "A" in the script
void ENTERSCRIPT(ATOM A) {
  if (SCRIPT == NIL) {
    SCRIPT = cons((LIST)A, NIL);
  } else {
    LIST S = SCRIPT;

    while (!(TL(S) == NIL)) {
      S = TL(S);
    }

    TL(S) = cons((LIST)A, NIL);
  }
}

static void COMMENT() {
  ATOM SUBJECT = (ATOM)TL(HD(TOKENS));
  LIST COMMENT = HD(TL(TOKENS));

  if (VAL(SUBJECT) == NIL) {
    VAL(SUBJECT) = cons(cons(0, NIL), NIL);
    ENTERSCRIPT(SUBJECT);
  }

  if (PROTECTED(SUBJECT)) {
    return;
  }

  TL(HD(VAL(SUBJECT))) = COMMENT;

  if (COMMENT == NIL && TL(VAL(SUBJECT)) == NIL) {
    REMOVE(SUBJECT);
  }

  SAVED = false;
}

static void EVALUATION() {
  LIST CODE = expression();
  word CH = (word)HD(TOKENS);

  // static SO INVISIBLE TO GARBAGE COLLECTOR
  LIST E = 0;

  if (!(have((TOKEN)'!'))) {
    check((TOKEN)'?');
  }

  if (ERRORFLAG) {
    return;
  }

  check(EOL);

  if (ATOBJECT) {
    PRINTOB(CODE);
    (*_WRCH)('\n');
  }

  E = buildexp(CODE);

  if (ATCOUNT) {
    RESETGCSTATS();
  }

  initstats();
  EVALUATING = true;
  FORMATTING = CH == '?';
  printval(E, FORMATTING);

  if (FORMATTING) {
    (*_WRCH)('\n');
  }

  CLOSECHANNELS();
  EVALUATING = false;

  if (ATCOUNT) {
    outstats();
  }
}

static void ABORDERCOM() { SCRIPT = SORT(SCRIPT), SAVED = false; }

static LIST SORT(LIST X) {

  if (X == NIL || TL(X) == NIL) {
    return X;
  }

  {
    // first split x
    LIST A = NIL, B = NIL, HOLD = NIL;

    while (!(X == NIL)) {
      HOLD = A, A = cons(HD(X), B), B = HOLD, X = TL(X);
    }

    A = SORT(A), B = SORT(B);

    // now merge the two halves back together
    while (!(A == NIL || B == NIL)) {
      if (ALFA_LS((ATOM)HD(A), (ATOM)HD(B))) {
        X = cons(HD(A), X), A = TL(A);
      } else {
        X = cons(HD(B), X), B = TL(B);
      }
    }

    if (A == NIL) {
      A = B;
    }

    while (!(A == NIL)) {
      X = cons(HD(A), X), A = TL(A);
    }

    return REVERSE(X);
  }
}

static void REORDERCOM() {
  if (isid(HD(TOKENS)) &&
      (isid(HD(TL(TOKENS))) || HD(TL(TOKENS)) == (LIST)DOTDOT_SY)) {
    SCRIPTREORDER();
  } else if (haveid() && HD(TOKENS) != EOL) {
    LIST NOS = NIL;
    word MAX = NO_OF_EQNS(THE_ID);

    while (havenum()) {
      word A = THE_NUM;
      word B = have(DOTDOT_SY) ? havenum() ? THE_NUM : MAX : A;
      word I;

      for (I = A; I <= B; I++) {
        if (!MEMBER(NOS, (LIST)I) && 1 <= I && I <= MAX) {
          NOS = cons((LIST)I, NOS);
        }
      }
      // nos out of range are silently ignored
    }

    check(EOL);
    if (ERRORFLAG) {
      return;
    }

    if (VAL(THE_ID) == NIL) {
      display(THE_ID, false, false);
      return;
    }

    if (PROTECTED(THE_ID)) {
      return;
    }

    {
      word I;
      for (I = 1; I <= MAX; I++) {
        if (!(MEMBER(NOS, (LIST)I))) {
          NOS = cons((LIST)I, NOS);
        }
      }
      // any eqns left out are tacked on at the end
    }

    // note that "NOS" are in reverse order
    {
      LIST NEW = NIL;
      LIST EQNS = TL(VAL(THE_ID));
      while (!(NOS == NIL)) {
        LIST EQN = ELEM(EQNS, (word)HD(NOS));
        removelineno(EQN);
        NEW = cons(EQN, NEW);
        NOS = TL(NOS);
      }

      // note that the EQNS in "NEW" are now in the correct order
      TL(VAL(THE_ID)) = NEW;
      display(THE_ID, true, false);
      SAVED = false;
      CLEARMEMORY();
    }
  } else
    syntax();
}

static void SCRIPTREORDER() {
  LIST R = NIL;
  while ((haveid())) {
    if (have(DOTDOT_SY)) {
      ATOM A = THE_ID, B = 0;
      LIST X = NIL;

      if (haveid()) {
        B = THE_ID;
      } else if (HD(TOKENS) == EOL) {
        B = (ATOM)EOL;
      }

      if (B == 0) {
        syntax();
      } else {
        X = EXTRACT(A, B);
      }

      if (X == NIL) {
        syntax();
      }

      R = SHUNT(X, R);
    } else if (MEMBER(SCRIPT, (LIST)THE_ID)) {
      R = cons((LIST)THE_ID, R);
    } else {
      fprintf(bcpl_OUTPUT, "\"%s\" not in script\n", PRINTNAME(THE_ID));
      syntax();
    }
  }

  check(EOL);
  if (ERRORFLAG) {
    return;
  }

  {
    LIST R1 = NIL;
    while (!(TL(R) == NIL)) {
      if (!(MEMBER(TL(R), HD(R))))
        SCRIPT = SUB1(SCRIPT, (ATOM)HD(R)), R1 = cons(HD(R), R1);
      R = TL(R);
    }
    SCRIPT = APPEND(EXTRACT((ATOM)HD(SCRIPT), (ATOM)HD(R)),
                    APPEND(R1, TL(EXTRACT((ATOM)HD(R), (ATOM)EOL))));
    SAVED = false;
  }
}

static word NO_OF_EQNS(ATOM A) {
  return VAL(A) == NIL ? 0 : LENGTH(TL(VAL(A)));
}

// library functions are recognisable by not being part of the script
static bool PROTECTED(ATOM A) {

  if (MEMBER(SCRIPT, (LIST)A)) {
    return false;
  }

  if (MEMBER(HOLDSCRIPT, (LIST)A)) {
    if (!(MEMBER(GET_HITS, (LIST)A))) {
      GET_HITS = cons((LIST)A, GET_HITS);
    }
    return false;
  }
  fprintf(bcpl_OUTPUT, "\"%s\" is predefined and cannot be altered\n",
          PRINTNAME(A));
  return true;
}

// removes "A" from the script
static void REMOVE(ATOM A) {
  SCRIPT = SUB1(SCRIPT, A);
  VAL(A) = NIL;
}

// returns a segment of the script
static LIST EXTRACT(ATOM A, ATOM B) {
  LIST S = SCRIPT, X = NIL;

  while (!(S == NIL || HD(S) == (LIST)A)) {
    S = TL(S);
  }

  while (!(S == NIL || HD(S) == (LIST)B)) {
    X = cons(HD(S), X), S = TL(S);
  }

  if (!(S == NIL)) {
    X = cons(HD(S), X);
  }

  if (S == NIL && B != (ATOM)EOL) {
    X = NIL;
  }

  if (X == NIL) {
    fprintf(bcpl_OUTPUT, "\"%s..%s\" not in script\n", PRINTNAME(A),
            B == (ATOM)EOL ? "" : PRINTNAME(B));
  }

  return REVERSE(X);
}

static void DELETECOM() {

  LIST DLIST = NIL;
  while (haveid()) {

    if (have(DOTDOT_SY)) {
      ATOM A = THE_ID, B = (ATOM)EOL;
      if (haveid()) {
        B = THE_ID;
      } else if (!(HD(TOKENS) == EOL)) {
        syntax();
      }
      DLIST = cons(cons((LIST)A, (LIST)B), DLIST);
    } else {
      word MAX = NO_OF_EQNS(THE_ID);
      LIST NLIST = NIL;

      while (havenum()) {
        word A = THE_NUM;
        word B = have(DOTDOT_SY) ? havenum() ? THE_NUM : MAX : A;
        word I;

        for (I = A; I <= B; I++) {
          NLIST = cons((LIST)I, NLIST);
        }
      }

      DLIST = cons(cons((LIST)THE_ID, NLIST), DLIST);
    }
  }

  check(EOL);
  if (ERRORFLAG) {
    return;
  }

  {
    word DELS = 0;

    // delete all
    if (DLIST == NIL) {
      if (SCRIPT == NIL) {
        DISPLAYALL(false);
      } else {
        if (!(MAKESURE())) {
          return;
        }

        while (!(SCRIPT == NIL)) {
          DELS = DELS + NO_OF_EQNS((ATOM)HD(SCRIPT));
          VAL((ATOM)HD(SCRIPT)) = NIL;
          SCRIPT = TL(SCRIPT);
        }
      }
    }
    while (!(DLIST == NIL)) {
      //"NAME..NAME"
      if (isatom(TL(HD(DLIST))) || TL(HD(DLIST)) == EOL) {
        LIST X = EXTRACT((ATOM)HD(HD(DLIST)), (ATOM)TL(HD(DLIST)));
        DLIST = TL(DLIST);
        while (!(X == NIL))
          DLIST = cons(cons(HD(X), NIL), DLIST), X = TL(X);
      } else {
        ATOM NAME = (ATOM)HD(HD(DLIST));
        LIST NOS = TL(HD(DLIST));
        LIST NEW = NIL;
        DLIST = TL(DLIST);
        if (VAL(NAME) == NIL) {
          display(NAME, false, false);
          continue;
        }

        if (PROTECTED(NAME)) {
          continue;
        }

        if (NOS == NIL) {
          DELS = DELS + NO_OF_EQNS(NAME);
          REMOVE(NAME);
          continue;
        } else {
          word I;
          for (I = NO_OF_EQNS(NAME); I >= 1; I = I - 1)
            if (MEMBER(NOS, (LIST)I)) {
              DELS = DELS + 1;
            } else {
              LIST EQN = ELEM(TL(VAL(NAME)), I);
              removelineno(EQN);
              NEW = cons(EQN, NEW);
            }
        }

        TL(VAL(NAME)) = NEW;

        // comment field
        if (NEW == NIL && TL(HD(VAL(NAME))) == NIL) {
          REMOVE(NAME);
        }
      }
    }

    fprintf(bcpl_OUTPUT, "%" W " equations deleted\n", DELS);
    if (DELS > 0) {
      SAVED = false;
      CLEARMEMORY();
    }
  }
}
