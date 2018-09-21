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
static void dircom(), displaycom(), quitcom(), objectcom();
static void resetcom(), gccom(), countcom(), savecom(), filecom(), getcom();
static void listcom(), namescom(), libcom(), clearcom(), openlibcom();
static void helpcom(), renamecom(), abordercom(), reordercom(), deletecom();
static bool startdisplaycom();

static void parseline(char *line);
static void initialise();
static void enterargv(int USERARGC, LIST USERARGV);
static void setup_commands();
static void command();
static void displayall(bool DOUBLESPACING);
static bool makesure();
static void filename();
static bool okfile(FILE *STR, char *FILENAME);
static void check_hits();
static bool getfile(char *FILENAME);
static void find_undefs();
static bool isdefined(ATOM X);
static void scriptlist(LIST S);
static LIST subst(LIST Z, LIST A);
static void newequation();
static void clearmemory();
static void comment();
static void evaluation();
static LIST sort(LIST X);
static void scriptreorder();
static word no_of_eqns(ATOM A);
static bool protected(ATOM A);
static bool primitive(ATOM A);
static void remove_atom(ATOM A);
static LIST extract(ATOM A, ATOM B);

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

void escapetonextcommand();

// are we ignoring interrupts?
static bool INTERRUPTS_ARE_HELD = false;

// was an interrupt delivered while we were ignoring them?
static bool INTERRUPT_OCCURRED = false;

static void catchinterrupt(int signum) {
  if (INTERRUPTS_ARE_HELD) {

    // can't be 0
    INTERRUPT_OCCURRED = signum;
    return;
  }

  // in case interrupt struck while reduce was dissecting a constant
  fixup_s();

  _WRCH = TRUEWRCH;
  closechannels();

  // die quietly if running as script or ABORT() called
  // bcpl_writes("\n**break in - return to KRC command level**\n");
  if (!(QUIET || ABORTED)) {
    bcpl_writes("<interrupt>\n");
  }

  ABORTED = false;
  escapetonextcommand();
}

void hold_interrupts() { INTERRUPTS_ARE_HELD = true; }

void release_interrupts() {

  INTERRUPTS_ARE_HELD = false;

  if (INTERRUPT_OCCURRED) {
    INTERRUPT_OCCURRED = false;
    catchinterrupt(INTERRUPT_OCCURRED);
  }
}

// essential that definitions of the above should be provided if
// the package is to be used in an interactive program

// where to jump back to on runtime errors or keyboard interrupts
static jmp_buf nextcommand;

void escapetonextcommand() {
  _WRCH = TRUEWRCH;

  if (bcpl_INPUT != stdin) {
    if (bcpl_INPUT_fp != stdin) {
      fclose(bcpl_INPUT_fp);
    }
    bcpl_INPUT_fp = (stdin);
  }

  closechannels();

  if (EVALUATING) {
    if (ATCOUNT) {
      outstats();
    }

    // in case some pointers have been left reversed
    clearmemory();

    EVALUATING = false;
  }

  if (HOLDSCRIPT != NIL) {

    SCRIPT = HOLDSCRIPT, HOLDSCRIPT = NIL;
    check_hits();
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
    initialise();

    // set up the interrupt handler
    act.sa_handler = catchinterrupt;

    // because the interrupt handler never returns
    act.sa_flags = SA_NODEFER;

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
    force_gc();
    ATGC = HOLDATGC;
  }

  // both initially and on longjump, continue here.
  if (EVALUATE && !SIGNOFF) {

    // quit on errors or interrupts
    SIGNOFF = true;

    parseline(EVALUATE);
    if (EXPFLAG) {
      evaluation();
    } else {
      bcpl_writes("-e takes an expression followed by ? or !\n");
    }

    if (ERRORFLAG) {
      syntax_error("malformed expression after -e\n");
    }
  }

  while (!(SIGNOFF)) {
    command();
  }

  quitcom();

  // exit(0);
  // moved inside quitcom()
}

// ----- parseline:
// a version of readline that gets its input from a string

static char *input_line;

// alternative version of rdch that gets its chars from a string
static int str_rdch(void) {

  if (input_line == NULL) {
    return EOF;
  }

  if (*input_line == '\0') {
    input_line = NULL;
    return '\n';
  }
  return *input_line++;
}

static int str_unrdch(int c) {

  if (input_line == NULL && c == '\n') {
    input_line = "\n";
  } else {
    *(--input_line) = c;
  }

  return c;
}

// same as readline, but gets its input from a C string
static void parseline(char *line) {

  input_line = line;

  _RDCH = str_rdch, _UNRDCH = str_unrdch;

  readline();

  _RDCH = bcpl_rdch, _UNRDCH = bcpl_unrdch;
}

// ----- end of parseline

static char TITLE[] = "Kent Recursive Calculator 1.0";

// where to look for "prelude" and other files KRC needs
#ifndef LIBDIR
#define LIBDIR "/usr/local/lib/krc"
#endif
// but use krclib in current directory if present, see below

static void initialise() {

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
          bcpl_writes("krc: -e What?\n");
          exit(0);
        }
        if (EVALUATE) {
          bcpl_writes("krc: Only one -e flag allowed\n");
          exit(0);
        }
        EVALUATE = ARGV[I];
        QUIET = true;
        break;
      case 'z':
        LISTBASE = 1;
        LEGACY = true;
        bcpl_writes("LISTBASE=1\n");
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

      USERARGV = cons((LIST)mkatom(ARGV[I]), USERARGV), USERARGC++;
    }
  }

  if (EVALUATE) {

    enterargv(USERARGC, USERARGV);

  } else if (USERARGC > 1) {

    bcpl_writes("krc: too many arguments\n");
    exit(0);
  }

  if (LOADPRELUDE) {
    if (USERLIB) {

      // -l option was used
      getfile(USERLIB);

    } else {

      struct stat buf;

      if (stat("krclib", &buf) == 0) {

        getfile(OLDLIB ? "krclib/lib1981" : "krclib/prelude");

      } else {

        getfile(OLDLIB ? LIBDIR "/lib1981" : LIBDIR "/prelude");
      }
    }

  } else {

    // if ( USERLIB || OLDLIB )
    // { bcpl_writes("krc: invalid combination -n and -l or -L\n"); exit(0); }
    // else
    bcpl_writes("\"PRELUDE\" suppressed\n");
  }

  // effective only for prelude
  SKIPCOMMENTS = false;

  LIBSCRIPT = sort(SCRIPT), SCRIPT = NIL;

  if (USERSCRIPT) {

    // if ( LISTSCRIPT ) _RDCH=echo_rdch;
    getfile(USERSCRIPT);
    SAVED = true;

    // if ( LISTSCRIPT ) _RDCH=bcpl_rdch;
    LASTFILE = mkatom(USERSCRIPT);
  }

  setup_commands();
  release_interrupts();

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
static void enterargv(int USERARGC, LIST USERARGV) {

  ATOM A = mkatom("argv");
  LIST CODE =
      cons((LIST)FORMLIST_C, cons((LIST)USERARGC, cons((LIST)STOP_C, NIL)));

  for (; USERARGV != NIL; USERARGV = TL(USERARGV)) {
    CODE = cons((LIST)LOAD_C, cons(cons((LIST)QUOTE, HD(USERARGV)), CODE));
  }

  VAL(A) = cons(cons((LIST)0, NIL), cons(cons((LIST)0, CODE), NIL));
  enterscript(A);
}

void SPACE_ERROR(char *MESSAGE) {

  _WRCH = TRUEWRCH;
  closechannels();

  if (EVALUATING) {

    fprintf(bcpl_OUTPUT, "\n**%s**\n**evaluation abandoned**\n", MESSAGE);
    escapetonextcommand();

  } else if (MEMORIES == NIL) {

    fprintf(bcpl_OUTPUT, "\n%s - recovery impossible\n", MESSAGE);
    exit(0);

  } else {

    // let go of memos and try to carry on
    clearmemory();
  }
}

void BASES(void (*F)(LIST *)) {

  // in reducer.c
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

static void setup_commands() {

#define F(S, R)                                                                \
  { COMMANDS = cons(cons((LIST)mkatom(S), (LIST)R), COMMANDS); }
#define FF(S, R)                                                               \
  {                                                                            \
    FILECOMMANDS = cons((LIST)mkatom(S), FILECOMMANDS);                        \
    F(S, R);                                                                   \
  }

  F("delete", deletecom);
  F("d", deletecom); // synonym
  F("reorder", reordercom);
  FF("save", savecom);
  FF("get", getcom);
  FF("list", listcom);
  FF("file", filecom);
  FF("f", filecom);
  F("dir", dircom);
  F("quit", quitcom);
  F("q", quitcom); // synonym
  F("names", namescom);
  F("lib", libcom);
  F("aborder", abordercom);
  F("rename", renamecom);
  F("openlib", openlibcom);
  F("clear", clearcom);
  F("help", helpcom);
  F("h", helpcom);        // synonym
  F("object", objectcom); // these last commands are for use in
  F("reset", resetcom);   // debugging the system
  F("gc", gccom);
  F("dic", reportdic);
  F("count", countcom);
  F("lpm", listpm);
#undef FF
#undef F
}

static void dircom() {

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

void closechannels() {

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

FILE *findchannel(char *F) {
  LIST P = OUTFILES;

  while (!(P == NIL || strcmp((char *)HD(HD(P)), F) == 0)) {
    P = TL(P);
  }

  if (P == NIL) {
    FILE *OUT = bcpl_findoutput(F);

    if (OUT != NULL) {
      OUTFILES = cons(cons((LIST)F, (LIST)OUT), OUTFILES);
    }

    return OUT;
  } else {
    return (FILE *)TL(HD(P));
  }
}

// command interpreter
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

static void helpcom() {

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
    bcpl_writes("/h What? `/h' for options\n");
    return;
  }

  strncpy(strbuf, local ? HELPLOCAL : HELP, BUFLEN);
  strncat(strbuf, topic, BUFLEN - strlen(strbuf));

  r = system(strbuf);
}

static void command() {

  static char prompt[] = "krc> ";

  char *line = linenoise(QUIET ? "" : prompt);

  if (line && line[0] == '\0') {
    // otherwise the interpreter exits
    return;
  }

  // handles NULL->EOF OK
  parseline(line);

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

      displayall(false);
      // if ( have((TOKEN)'@') && have(EOL) ) listpm(); else
      // for debugging the system

    } else {

      LIST P = COMMANDS;

      if (haveid()) {
        THE_ID = mkatom(scaseconv(PRINTNAME(THE_ID)));
        // always accept commands in either case
      } else {
        P = NIL;
      }

      while (!(P == NIL || THE_ID == (ATOM)HD(HD(P)))) {
        P = TL(P);
      }

      if (P == NIL) {

        bcpl_writes("command not recognised\nfor help type /h\n");

      } else {

        // see "setup_commands()"
        ((void (*)())TL(HD(P)))();
      }
    }

  } else if (startdisplaycom()) {

    displaycom();

  } else if (COMMENTFLAG > 0) {

    comment();

  } else if (EQNFLAG) {

    newequation();

  } else {

    evaluation();
  }

  if (ERRORFLAG) {
    syntax_error("**syntax error**\n");
  }
}

static bool startdisplaycom() {

  LIST HOLD = TOKENS;
  word R = haveid() && (have(EOL) || have((TOKEN)DOTDOT_SY));
  TOKENS = HOLD;
  return R;
}

static void displaycom() {

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
        X = extract(A, B);
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
static void displayall(bool DOUBLESPACING) {

  LIST P = SCRIPT;
  if (P == NIL) {
    bcpl_writes("Script=empty\n");
  }

  while (!(P == NIL)) {

    // don't display builtin fns (relevant only in /openlib)
    if (!(primitive((ATOM)HD(P)))) {
      display((ATOM)HD(P), false, false);
    }

    P = TL(P);

    // extra line between groups
    if (DOUBLESPACING && P != NIL) {
      (*_WRCH)('\n');
    }
  }
}

static bool primitive(ATOM A) {

  if (TL(VAL(A)) == NIL) {
    // A has comment but no eqns
    return false;
  }

  return HD(TL(HD(TL(VAL(A))))) == (LIST)CALL_C;
}

static void quitcom() {

  if (TOKENS != NIL) {
    check(EOL);
  }

  if (ERRORFLAG) {
    return;
  }

  if (makesure()) {
    bcpl_writes("krc logout\n");
    exit(0);
  }
}

static bool makesure() {

  if (SAVED || SCRIPT == NIL) {
    return true;
  }

  bcpl_writes("Are you sure? ");

  {
    word CH = (*_RDCH)(), C;
    (*_UNRDCH)(CH);
    while (!((C = (*_RDCH)()) == '\n' || C == EOF)) {
      continue;
    }

    if (CH == 'y' || CH == 'Y') {
      return true;
    }

    bcpl_writes("Command ignored\n");

    return false;
  }
}

static void objectcom() { ATOBJECT = true; }

static void resetcom() { ATOBJECT = false, ATCOUNT = false, ATGC = false; }

static void gccom() {

  ATGC = true;
  force_gc();
}

static void countcom() { ATCOUNT = true; }

static void savecom() {

  filename();
  if (ERRORFLAG) {
    return;
  }

  if (SCRIPT == NIL) {
    bcpl_writes("Cannot save empty script\n");
    return;
  }

  {
    FILE *OUT = bcpl_findoutput("T#SCRIPT");
    bcpl_OUTPUT_fp = (OUT);
    displayall(true);

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
          bcpl_writes("File saved in T#SCRIPT.\n");
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

static void filename() {

  if (have(EOL)) {
    if (LASTFILE == 0) {
      bcpl_writes("(No file set)\n");
      syntax();
    } else {
      THE_ID = LASTFILE;
    }
  } else if (haveid() && have(EOL)) {
    LASTFILE = THE_ID;
  } else {
    if (haveconst() && have(EOL) && !isnum(THE_CONST)) {
      bcpl_writes("(Warning - quotation marks no longer expected around "
                  "filenames in file commands - DT, Nov 81)\n");
    }
    syntax();
  }
}

static void filecom() {

  if (have(EOL)) {
    if (LASTFILE == 0) {
      bcpl_writes("No files used\n");
    } else {
      fprintf(bcpl_OUTPUT, "File = %s\n", PRINTNAME(LASTFILE));
    }
  } else {
    filename();
  }
}

static bool okfile(FILE *STR, char *FILENAME) {

  if (STR != NULL) {
    return true;
  }

  fprintf(bcpl_OUTPUT, "Cannot open \"%s\"\n", FILENAME);
  return false;
}

static void getcom() {

  bool CLEAN = SCRIPT == NIL;

  filename();
  if (ERRORFLAG) {
    return;
  }

  HOLDSCRIPT = SCRIPT, SCRIPT = NIL, GET_HITS = NIL;
  getfile(PRINTNAME(THE_ID));
  check_hits();

  SCRIPT = append(HOLDSCRIPT, SCRIPT), SAVED = CLEAN, HOLDSCRIPT = NIL;
}

static void check_hits() {

  if (!(GET_HITS == NIL)) {
    bcpl_writes("Warning - /get has overwritten or modified:\n");
    scriptlist(reverse(GET_HITS));
    GET_HITS = NIL;
  }
}

static bool getfile(char *FILENAME) {

  FILE *IN = bcpl_findinput(FILENAME);
  if (!(okfile(IN, FILENAME))) {
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
        comment();
      } else {
        newequation();
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

static void listcom() {

  filename();

  if (ERRORFLAG) {
    return;
  }

  {
    char *FNAME = PRINTNAME(THE_ID);
    FILE *IN = bcpl_findinput(FNAME);

    if (!(okfile(IN, FNAME))) {
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

static void namescom() {

  check(EOL);
  if (ERRORFLAG) {
    return;
  }

  if (SCRIPT == NIL) {
    displayall(false);
  } else {
    scriptlist(SCRIPT);
    find_undefs();
  }
}

// searches the script for names used but not defined
static void find_undefs() {

  LIST S = SCRIPT, UNDEFS = NIL;

  while (!(S == NIL)) {
    LIST EQNS = TL(VAL((ATOM)HD(S)));
    while (!(EQNS == NIL)) {
      LIST CODE = TL(HD(EQNS));
      while (iscons(CODE)) {
        LIST A = HD(CODE);

        if (isatom(A) && !isdefined((ATOM)A) && !member(UNDEFS, A)) {
          UNDEFS = cons(A, UNDEFS);
        }

        CODE = TL(CODE);
      }
      EQNS = TL(EQNS);
    }
    S = TL(S);
  }

  if (!(UNDEFS == NIL)) {
    bcpl_writes("\nNames used but not defined:\n");
    scriptlist(reverse(UNDEFS));
  }
}

static bool isdefined(ATOM X) {
  return VAL(X) == NIL || TL(VAL(X)) == NIL ? false : true;
}

static void libcom() {

  check(EOL);

  if (ERRORFLAG) {
    return;
  }

  if (LIBSCRIPT == NIL) {
    bcpl_writes("library = empty\n");
  } else {
    scriptlist(LIBSCRIPT);
  }
}

static void clearcom() {

  check(EOL);

  if (ERRORFLAG) {
    return;
  }

  clearmemory();
}

static void scriptlist(LIST S) {

  word COL = 0, I = 0;

// the minimum of various devices
#define LINEWIDTH 68

  while (!(S == NIL)) {
    char *N = PRINTNAME((ATOM)HD(S));

    if (primitive((ATOM)HD(S))) {
      S = TL(S);
      continue;
    }

    COL = COL + strlen(N) + 1;
    if (COL > LINEWIDTH) {
      COL = 0;
      (*_WRCH)('\n');
    }

    bcpl_writes(N);
    (*_WRCH)(' ');
    I = I + 1, S = TL(S);
  }

  if (COL + 6 > LINEWIDTH) {
    (*_WRCH)('\n');
  }

  fprintf(bcpl_OUTPUT, " (%" W ")\n", I);
}

static void openlibcom() {

  check(EOL);

  if (ERRORFLAG) {
    return;
  }

  SAVED = SCRIPT == NIL;
  SCRIPT = append(SCRIPT, LIBSCRIPT);
  LIBSCRIPT = NIL;
}

static void renamecom() {

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
      if (member(SCRIPT, HD(HD(Z1)))) {
        POSTDEFS = cons(TL(HD(Z1)), POSTDEFS);
      }

      if (isdefined((ATOM)TL(HD(Z1))) &&
          (!member(X, TL(HD(Z1))) || !member(SCRIPT, TL(HD(Z1))))) {
        POSTDEFS = cons(TL(HD(Z1)), POSTDEFS);
      }

      Z1 = TL(Z1);
    }

    while (!(POSTDEFS == NIL)) {

      if (member(TL(POSTDEFS), HD(POSTDEFS)) && !member(DUPS, HD(POSTDEFS))) {
        DUPS = cons(HD(POSTDEFS), DUPS);
      }

      POSTDEFS = TL(POSTDEFS);
    }

    if (!(DUPS == NIL)) {
      bcpl_writes("/rename illegal because of conflicting uses of ");

      while (!(DUPS == NIL)) {
        bcpl_writes(PRINTNAME((ATOM)HD(DUPS)));
        (*_WRCH)(' ');
        DUPS = TL(DUPS);
      }

      (*_WRCH)('\n');
      return;
    }
  }

  hold_interrupts();
  clearmemory();

  // prepare for assignment to val fields
  {
    LIST X1 = X, XVALS = NIL, TARGETS = NIL;
    while (!(X1 == NIL)) {

      if (member(SCRIPT, HD(X1))) {
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

            HD(LHS) = subst(Z, HD(LHS));
          }

          while (iscons(CODE)) {
            HD(CODE) = subst(Z, HD(CODE)), CODE = TL(CODE);
          }

          EQNS = TL(EQNS);
        }

        if (member(X, HD(S))) {
          VAL((ATOM)HD(S)) = NIL;
        }

        HD(S) = subst(Z, HD(S));
        S = TL(S);
      }

      // now reassign val fields
      while (!(TARGETS == NIL)) {
        VAL((ATOM)HD(TARGETS)) = HD(XVALS);
        TARGETS = TL(TARGETS), XVALS = TL(XVALS);
      }

      release_interrupts();
    }
  }
}

static LIST subst(LIST Z, LIST A) {

  while (!(Z == NIL)) {
    if (A == HD(HD(Z))) {
      SAVED = false;
      return TL(HD(Z));
    }
    Z = TL(Z);
  }
  return A;
}

static void newequation() {

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
        printobj(EQN);
        (*_WRCH)('\n');
      }

      if (VAL(SUBJECT) == NIL) {
        VAL(SUBJECT) = cons(cons((LIST)NARGS, NIL), cons(EQN, NIL));
        enterscript(SUBJECT);
      } else if (protected(SUBJECT)) {
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
        //            clearmemory(); } else

        fprintf(bcpl_OUTPUT, "Wrong no of args for \"%s\"\n",
                PRINTNAME(SUBJECT));
        bcpl_writes("Equation rejected\n");
        return;
      } else if (EQNO == -1) {
        // unnumbered EQN
        LIST EQNS = TL(VAL(SUBJECT));
        LIST P = profile(EQN);

        do {
          if (equal(P, profile(HD(EQNS)))) {
            LIST CODE = TL(HD(EQNS));
            if (HD(CODE) == (LIST)LINENO_C) {
              // if old EQN has line no,

              // new EQN inherits
              TL(TL(CODE)) = TL(EQN);

              HD(HD(EQNS)) = HD(EQN);
            } else {
              HD(EQNS) = EQN;
            }
            clearmemory();
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
            clearmemory();
            break;
          }

          if (EQNO < N) {
            LIST HOLD = HD(EQNS);
            HD(EQNS) = EQN;
            TL(EQNS) = cons(HOLD, TL(EQNS));
            clearmemory();
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

// called whenever eqns are destroyed, reordered or
// inserted (other than at the end of a definition)
static void clearmemory() {

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
void enterscript(ATOM A) {

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

static void comment() {

  ATOM SUBJECT = (ATOM)TL(HD(TOKENS));
  LIST COMMENT = HD(TL(TOKENS));

  if (VAL(SUBJECT) == NIL) {
    VAL(SUBJECT) = cons(cons(0, NIL), NIL);
    enterscript(SUBJECT);
  }

  if (protected(SUBJECT)) {
    return;
  }

  TL(HD(VAL(SUBJECT))) = COMMENT;

  if (COMMENT == NIL && TL(VAL(SUBJECT)) == NIL) {
    remove_atom(SUBJECT);
  }

  SAVED = false;
}

static void evaluation() {

  LIST CODE = expression();
  word CH = (word)HD(TOKENS);

  // static so invisible to garbage collector
  LIST E = 0;

  if (!(have((TOKEN)'!'))) {
    check((TOKEN)'?');
  }

  if (ERRORFLAG) {
    return;
  }

  check(EOL);

  if (ATOBJECT) {
    printobj(CODE);
    (*_WRCH)('\n');
  }

  E = buildexp(CODE);

  if (ATCOUNT) {
    resetgcstats();
  }

  initstats();
  EVALUATING = true;
  FORMATTING = CH == '?';
  printval(E, FORMATTING);

  if (FORMATTING) {
    (*_WRCH)('\n');
  }

  closechannels();
  EVALUATING = false;

  if (ATCOUNT) {
    outstats();
  }
}

static void abordercom() { SCRIPT = sort(SCRIPT), SAVED = false; }

static LIST sort(LIST X) {

  if (X == NIL || TL(X) == NIL) {
    return X;
  }

  {
    // first split x
    LIST A = NIL, B = NIL, HOLD = NIL;

    while (!(X == NIL)) {
      HOLD = A, A = cons(HD(X), B), B = HOLD, X = TL(X);
    }

    A = sort(A), B = sort(B);

    // now merge the two halves back together
    while (!(A == NIL || B == NIL)) {
      if (alfa_ls((ATOM)HD(A), (ATOM)HD(B))) {
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

    return reverse(X);
  }
}

static void reordercom() {

  if (isid(HD(TOKENS)) &&
      (isid(HD(TL(TOKENS))) || HD(TL(TOKENS)) == (LIST)DOTDOT_SY)) {
    scriptreorder();
  } else if (haveid() && HD(TOKENS) != EOL) {
    LIST NOS = NIL;
    word MAX = no_of_eqns(THE_ID);

    while (havenum()) {
      word A = THE_NUM;
      word B = have(DOTDOT_SY) ? havenum() ? THE_NUM : MAX : A;
      word I;

      for (I = A; I <= B; I++) {
        if (!member(NOS, (LIST)I) && 1 <= I && I <= MAX) {
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

    if (protected(THE_ID)) {
      return;
    }

    {
      word I;
      for (I = 1; I <= MAX; I++) {
        if (!(member(NOS, (LIST)I))) {
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
        LIST EQN = elem(EQNS, (word)HD(NOS));
        removelineno(EQN);
        NEW = cons(EQN, NEW);
        NOS = TL(NOS);
      }

      // note that the EQNS in "NEW" are now in the correct order
      TL(VAL(THE_ID)) = NEW;
      display(THE_ID, true, false);
      SAVED = false;
      clearmemory();
    }
  } else {
    syntax();
  }
}

static void scriptreorder() {

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
        X = extract(A, B);
      }

      if (X == NIL) {
        syntax();
      }

      R = shunt(X, R);

    } else if (member(SCRIPT, (LIST)THE_ID)) {

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
      if (!(member(TL(R), HD(R))))
        SCRIPT = sub1(SCRIPT, (ATOM)HD(R)), R1 = cons(HD(R), R1);
      R = TL(R);
    }
    SCRIPT = append(extract((ATOM)HD(SCRIPT), (ATOM)HD(R)),
                    append(R1, TL(extract((ATOM)HD(R), (ATOM)EOL))));
    SAVED = false;
  }
}

static word no_of_eqns(ATOM A) {

  return VAL(A) == NIL ? 0 : length(TL(VAL(A)));
}

// library functions are recognisable by not being part of the script
static bool protected(ATOM A) {

  if (member(SCRIPT, (LIST)A)) {
    return false;
  }

  if (member(HOLDSCRIPT, (LIST)A)) {
    if (!(member(GET_HITS, (LIST)A))) {
      GET_HITS = cons((LIST)A, GET_HITS);
    }
    return false;
  }
  fprintf(bcpl_OUTPUT, "\"%s\" is predefined and cannot be altered\n",
          PRINTNAME(A));
  return true;
}

// removes "A" from the script
// renamed to avoid conflict with remove(3)
static void remove_atom(ATOM A) {

  SCRIPT = sub1(SCRIPT, A);
  VAL(A) = NIL;
}

// returns a segment of the script
static LIST extract(ATOM A, ATOM B) {

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

  return reverse(X);
}

static void deletecom() {

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
      word MAX = no_of_eqns(THE_ID);
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
        displayall(false);
      } else {
        if (!(makesure())) {
          return;
        }

        while (!(SCRIPT == NIL)) {
          DELS = DELS + no_of_eqns((ATOM)HD(SCRIPT));
          VAL((ATOM)HD(SCRIPT)) = NIL;
          SCRIPT = TL(SCRIPT);
        }
      }
    }
    while (!(DLIST == NIL)) {
      //"NAME..NAME"
      if (isatom(TL(HD(DLIST))) || TL(HD(DLIST)) == EOL) {

        LIST X = extract((ATOM)HD(HD(DLIST)), (ATOM)TL(HD(DLIST)));
        DLIST = TL(DLIST);

        while (!(X == NIL)) {
          DLIST = cons(cons(HD(X), NIL), DLIST), X = TL(X);
        }

      } else {

        ATOM NAME = (ATOM)HD(HD(DLIST));
        LIST NOS = TL(HD(DLIST));
        LIST NEW = NIL;
        DLIST = TL(DLIST);

        if (VAL(NAME) == NIL) {
          display(NAME, false, false);
          continue;
        }

        if (protected(NAME)) {
          continue;
        }

        if (NOS == NIL) {

          DELS = DELS + no_of_eqns(NAME);
          remove_atom(NAME);
          continue;

        } else {

          word I;

          for (I = no_of_eqns(NAME); I >= 1; I = I - 1) {
            if (member(NOS, (LIST)I)) {
              DELS = DELS + 1;
            } else {
              LIST EQN = elem(TL(VAL(NAME)), I);
              removelineno(EQN);
              NEW = cons(EQN, NEW);
            }
          }
        }

        TL(VAL(NAME)) = NEW;

        // comment field
        if (NEW == NIL && TL(HD(VAL(NAME))) == NIL) {
          remove_atom(NAME);
        }
      }
    }

    fprintf(bcpl_OUTPUT, "%" W " equations deleted\n", DELS);
    if (DELS > 0) {
      SAVED = false;
      clearmemory();
    }
  }
}
