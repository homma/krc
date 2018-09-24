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
static void enterargv(int userargc, list userargv);
static void setup_commands();
static void command();
static void displayall(bool DOUBLESPACING);
static bool makesure();
static void filename();
static bool okfile(FILE *STR, char *FILENAME);
static void check_hits();
static bool getfile(char *FILENAME);
static void find_undefs();
static bool isdefined(atom X);
static void scriptlist(list S);
static list subst(list Z, list A);
static void newequation();
static void clearmemory();
static void comment();
static void evaluation();
static list sort(list X);
static void scriptreorder();
static word no_of_eqns(atom A);
static bool protected(atom A);
static bool primitive(atom A);
static void remove_atom(atom A);
static list extract(atom A, atom B);

// bases
static list COMMANDS = NIL;
static list SCRIPT = NIL;
static list OUTFILES = NIL;

static atom LASTFILE = 0;
static list LIBSCRIPT = NIL;
static list HOLDSCRIPT = NIL;
static list GET_HITS = NIL;

static bool SIGNOFF = false;
static bool SAVED = true;
static bool EVALUATING = false;

// flags used in debugging system
static bool ATOBJECT = false;
static bool ATCOUNT = false;

// for calling emas
static char PARAMV[256];

// global variables owned by main.c

// set by -z option
bool LEGACY = false;

list FILECOMMANDS = NIL;

// set by -s option
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

  wrch = TRUEWRCH;
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
  wrch = TRUEWRCH;

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

  rdch = str_rdch;
  unrdch = str_unrdch;

  readline();

  rdch = bcpl_rdch;
  unrdch = bcpl_unrdch;
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
  bool loadprelude = true;

  // use legacy prelude?
  bool oldlib = false;

  // script given on command line
  char *userscript = NULL;

  // reversed list of args after script name
  list userargv = NIL;

  // how many items in userargv?
  int userargc = 0;

  // list the script as we read it?
  // bool listscript = false;

  if (!isatty(0)) {
    QUIET = true;
  }

  setup_primfns_etc();

  for (int i = 1; i < ARGC; i++) {

    if (ARGV[i][0] == '-') {
      switch (ARGV[i][1]) {
      case 'n':
        loadprelude = false;
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
        ++i;    // handled in listlib.c
      case 'g': // handled in listlib.c
        break;
      case 'e':
        if (++i >= ARGC || ARGV[i][0] == '-') {
          bcpl_writes("krc: -e What?\n");
          exit(0);
        }
        if (EVALUATE) {
          bcpl_writes("krc: Only one -e flag allowed\n");
          exit(0);
        }
        EVALUATE = ARGV[i];
        QUIET = true;
        break;
      case 'z':
        LISTBASE = 1;
        LEGACY = true;
        bcpl_writes("LISTBASE=1\n");
        break;
      case 'L':
        oldlib = 1;
        break;
        // case 'v': listscript=true; break;
        // other parameters may be detected using haveparam()
      case 'C':
      case 'N':
      case 'O': // used only by testcomp, disabled
      default:
        fprintf(bcpl_OUTPUT, "krc: invalid option -%c\n", ARGV[i][1]);
        exit(0);
        break;
      }
    } else {

      // filename of script to load, or arguments for script
      if (userscript == NULL) {
        // was if ... else
        userscript = ARGV[i];
      }

      userargv = cons((list)mkatom(ARGV[i]), userargv), userargc++;
    }
  }

  if (EVALUATE) {

    enterargv(userargc, userargv);

  } else if (userargc > 1) {

    bcpl_writes("krc: too many arguments\n");
    exit(0);
  }

  if (loadprelude) {
    if (USERLIB) {

      // -l option was used
      getfile(USERLIB);

    } else {

      struct stat buf;

      if (stat("krclib", &buf) == 0) {

        getfile(oldlib ? "krclib/lib1981" : "krclib/prelude");

      } else {

        getfile(oldlib ? LIBDIR "/lib1981" : LIBDIR "/prelude");
      }
    }

  } else {

    // if ( USERLIB || oldlib )
    // { bcpl_writes("krc: invalid combination -n and -l or -L\n"); exit(0); }
    // else
    bcpl_writes("\"PRELUDE\" suppressed\n");
  }

  // effective only for prelude
  SKIPCOMMENTS = false;

  LIBSCRIPT = sort(SCRIPT), SCRIPT = NIL;

  if (userscript) {

    // if ( listscript ) rdch = echo_rdch;
    getfile(userscript);
    SAVED = true;

    // if ( listscript ) rdch = bcpl_rdch;
    LASTFILE = mkatom(userscript);
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
static void enterargv(int userargc, list userargv) {

  atom a = mkatom("argv");
  list code =
      cons((list)FORMLIST_C, cons((list)userargc, cons((list)STOP_C, NIL)));

  for (; userargv != NIL; userargv = TL(userargv)) {
    code = cons((list)LOAD_C, cons(cons((list)QUOTE, HD(userargv)), code));
  }

  VAL(a) = cons(cons((list)0, NIL), cons(cons((list)0, code), NIL));
  enterscript(a);
}

void space_error(char *message) {

  wrch = TRUEWRCH;
  closechannels();

  if (EVALUATING) {

    fprintf(bcpl_OUTPUT, "\n**%s**\n**evaluation abandoned**\n", message);
    escapetonextcommand();

  } else if (MEMORIES == NIL) {

    fprintf(bcpl_OUTPUT, "\n%s - recovery impossible\n", message);
    exit(0);

  } else {

    // let go of memos and try to carry on
    clearmemory();
  }
}

void bases(void (*F)(list *)) {

  // in reducer.c
  extern list S;

  F(&COMMANDS);
  F(&FILECOMMANDS);
  F(&SCRIPT);
  F(&LIBSCRIPT);
  F(&HOLDSCRIPT);
  F(&GET_HITS);
  F((list *)&LASTFILE);
  F(&OUTFILES);
  F(&MEMORIES);
  F(&S);
  F(&TOKENS);
  F((list *)&THE_ID);
  F(&THE_CONST);
  F(&LASTLHS);
  F(&TRUTH);
  F(&FALSITY);
  F(&INFINITY);
  compiler_bases(F);
  reducer_bases(F);
}

static void setup_commands() {

  // F creates commands
  // FF creates file commands (save, get, list, file, f)

#define F(S, R)                                                                \
  { COMMANDS = cons(cons((list)mkatom(S), (list)R), COMMANDS); }
#define FF(S, R)                                                               \
  {                                                                            \
    FILECOMMANDS = cons((list)mkatom(S), FILECOMMANDS);                        \
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
      wrch('\n');
    }

    if (bcpl_OUTPUT != stdout) {
      fclose(bcpl_INPUT_fp);
    }

    OUTFILES = TL(OUTFILES);
  }

  bcpl_OUTPUT_fp = (stdout);
}

FILE *findchannel(char *f) {
  list p = OUTFILES;

  while (!(p == NIL || strcmp((char *)HD(HD(p)), f) == 0)) {
    p = TL(p);
  }

  if (p == NIL) {
    FILE *out = bcpl_findoutput(f);

    if (out != NULL) {
      OUTFILES = cons(cons((list)f, (list)out), OUTFILES);
    }

    return out;
  } else {
    return (FILE *)TL(HD(p));
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
  char strbuf[BUFLEN + 1];
  char *topic;
  int local = stat("krclib", &buf) == 0;
  int r;

  if (have(EOL)) {

    if (local) {
      r = system(HELPLOCAL "menu");
    } else {
      r = system(HELP "menu");
    }

    return;
  }

  // avoid global variables
  // topic = haveid() ? NAME(THE_ID) : NULL;

  topic = haveid() ? NAME(the_id()) : NULL;

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

  if (have((token)EOF)) {

    SIGNOFF = true;

  } else if (have((token)'/')) {
    // KRC shell commands

    if (have(EOL)) {

      displayall(false);

    } else {

      // if ( have((token)'@') && have(EOL) ) { listpm(); }
      // for debugging the system

      list p = COMMANDS;

      if (haveid()) {

        // avoid global variables
        // THE_ID = mkatom(scaseconv(NAME(THE_ID)));

        // change it to upper-case
        // always accept commands in either case
        set_the_id(mkatom(scaseconv(NAME(the_id()))));

      } else {

        p = NIL;
      }

      // avoid global variables
      // while (!(p == NIL || THE_ID == (atom)HD(HD(p)))) {
      //   p = TL(p);
      // }

      atom id = the_id();
      while (!(p == NIL || id == (atom)HD(HD(p)))) {
        p = TL(p);
      }

      if (p == NIL) {

        bcpl_writes("command not recognised\nfor help type /h\n");

      } else {

        // see "setup_commands()"
        ((void (*)())TL(HD(p)))();
      }
    }

  } else if (startdisplaycom()) {

    displaycom();

  } else if (COMMENTFLAG > 0) {
    // comment

    comment();

  } else if (EQNFLAG) {
    // equation

    newequation();

  } else {
    // expression

    evaluation();
  }

  if (ERRORFLAG) {
    syntax_error("**syntax error**\n");
  }
}

static bool startdisplaycom() {

  list hold = TOKENS;
  bool r = haveid() && (have(EOL) || have((token)DOTDOT_SY));
  TOKENS = hold;
  return r;
}

static void displaycom() {

  if (haveid()) {

    if (have(EOL)) {

      // avoid global variables
      // display(THE_ID, true, false);

      display(the_id(), true, false);

    } else if (have((token)DOTDOT_SY)) {

      // avoid global variables
      // atom a = THE_ID;

      atom a = the_id();
      list x = NIL;

      // avoid global variables
      // atom b = have(EOL) ? (atom)EOL : haveid() && have(EOL) ? THE_ID : 0;

      // bug?
      atom b = have(EOL) ? (atom)EOL : haveid() && have(EOL) ? the_id() : 0;

      if (b == 0) {
        syntax();
      } else {
        x = extract(a, b);
      }

      while (x != NIL) {
        display((atom)HD(x), false, false);
        x = TL(x);
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
static void displayall(bool doublespacing) {

  list p = SCRIPT;
  if (p == NIL) {
    bcpl_writes("Script=empty\n");
  }

  while (p != NIL) {

    // don't display builtin fns (relevant only in /openlib)
    if (!(primitive((atom)HD(p)))) {
      display((atom)HD(p), false, false);
    }

    p = TL(p);

    // extra line between groups
    if (doublespacing && p != NIL) {
      wrch('\n');
    }
  }
}

static bool primitive(atom a) {

  if (TL(VAL(a)) == NIL) {
    // a has comment but no eqns
    return false;
  }

  return HD(TL(HD(TL(VAL(a))))) == (list)CALL_C;
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
    word ch = rdch();
    word c;
    unrdch(ch);
    while (!((c = rdch()) == '\n' || c == EOF)) {
      continue;
    }

    if (ch == 'y' || ch == 'Y') {
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

        // avoid global variables
        // execlp("mv", "mv", "T#SCRIPT", NAME(THE_ID), (char *)0);

        execlp("mv", "mv", "T#SCRIPT", NAME(the_id()), (char *)0);
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

      // avoid global variables
      // THE_ID = LASTFILE;

      set_the_id(LASTFILE);
    }

  } else if (haveid() && have(EOL)) {

    // avoid global variables
    // LASTFILE = THE_ID;

    LASTFILE = the_id();

  } else {

    // avoid global variables
    // if (haveconst() && have(EOL) && !isnum(THE_CONST)) {

    if (haveconst() && have(EOL) && !isnum(the_const())) {
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
      fprintf(bcpl_OUTPUT, "File = %s\n", NAME(LASTFILE));
    }
  } else {
    filename();
  }
}

static bool okfile(FILE *STR, char *filename) {

  if (STR != NULL) {
    return true;
  }

  fprintf(bcpl_OUTPUT, "Cannot open \"%s\"\n", filename);
  return false;
}

static void getcom() {

  bool clean = SCRIPT == NIL;

  filename();
  if (ERRORFLAG) {
    return;
  }

  HOLDSCRIPT = SCRIPT;
  SCRIPT = NIL;
  GET_HITS = NIL;

  // avoid global variables
  // getfile(NAME(THE_ID));

  getfile(NAME(the_id()));
  check_hits();

  SCRIPT = append(HOLDSCRIPT, SCRIPT), SAVED = clean, HOLDSCRIPT = NIL;
}

static void check_hits() {

  if (!(GET_HITS == NIL)) {
    bcpl_writes("Warning - /get has overwritten or modified:\n");
    scriptlist(reverse(GET_HITS));
    GET_HITS = NIL;
  }
}

static bool getfile(char *filename) {

  FILE *in = bcpl_findinput(filename);
  if (!(okfile(in, filename))) {
    return false;
  }

  bcpl_INPUT_fp = (in);

  {
    // to locate line number of error in file
    int line = 0;

    do {
      line++;
      readline();

      if (ferror(in)) {
        ERRORFLAG = true;
        break;
      }

      if (have(EOL)) {
        continue;
      }

      if (HD(TOKENS) == EOFTOKEN) {
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
        fprintf(bcpl_OUTPUT, "%s at line %d\n", filename, line);
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

    // avoid global variables
    // char *fname = NAME(THE_ID);

    char *fname = NAME(the_id());

    FILE *in = bcpl_findinput(fname);

    if (!(okfile(in, fname))) {
      return;
    }

    bcpl_INPUT_fp = (in);

    {
      word ch = rdch();

      while (!(ch == EOF)) {
        wrch(ch);
        ch = rdch();
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

  list s = SCRIPT;
  list undefs = NIL;

  while (!(s == NIL)) {
    list eqns = TL(VAL((atom)HD(s)));
    while (!(eqns == NIL)) {
      list code = TL(HD(eqns));
      while (iscons(code)) {
        list a = HD(code);

        if (isatom(a) && !isdefined((atom)a) && !member(undefs, a)) {
          undefs = cons(a, undefs);
        }

        code = TL(code);
      }
      eqns = TL(eqns);
    }
    s = TL(s);
  }

  if (!(undefs == NIL)) {
    bcpl_writes("\nNames used but not defined:\n");
    scriptlist(reverse(undefs));
  }
}

static bool isdefined(atom x) {
  return VAL(x) == NIL || TL(VAL(x)) == NIL ? false : true;
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

static void scriptlist(list s) {

  word col = 0;
  word i = 0;

// the minimum of various devices
#define LINEWIDTH 68

  while (!(s == NIL)) {
    char *n = NAME((atom)HD(s));

    if (primitive((atom)HD(s))) {
      s = TL(s);
      continue;
    }

    col = col + strlen(n) + 1;
    if (col > LINEWIDTH) {
      col = 0;
      wrch('\n');
    }

    bcpl_writes(n);
    wrch(' ');
    i = i + 1;
    s = TL(s);
  }

  if (col + 6 > LINEWIDTH) {
    wrch('\n');
  }

  fprintf(bcpl_OUTPUT, " (%" W ")\n", i);
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

  list x = NIL;
  list y = NIL;
  list z = NIL;

  while (haveid()) {

    // avoid global variables
    // x = cons((list)THE_ID, x);

    x = cons((list)the_id(), x);
  }

  check((token)',');

  while (haveid()) {

    // avoid global variables
    // y = cons((list)THE_ID, y);

    y = cons((list)the_id(), y);
  }

  check(EOL);
  if (ERRORFLAG) {
    return;
  }

  // first check lists are of same length
  {
    list x1 = x, y1 = y;

    while (!(x1 == NIL || y1 == NIL)) {
      z = cons(cons(HD(x1), HD(y1)), z);
      x1 = TL(x1);
      y1 = TL(y1);
    }

    if (!(x1 == NIL && y1 == NIL && z != NIL)) {
      syntax();
      return;
    }
  }

  // now check legality of rename
  {
    list z1 = z;
    list postdefs = NIL;
    list dups = NIL;

    while (!(z1 == NIL)) {
      if (member(SCRIPT, HD(HD(z1)))) {
        postdefs = cons(TL(HD(z1)), postdefs);
      }

      if (isdefined((atom)TL(HD(z1))) &&
          (!member(x, TL(HD(z1))) || !member(SCRIPT, TL(HD(z1))))) {
        postdefs = cons(TL(HD(z1)), postdefs);
      }

      z1 = TL(z1);
    }

    while (!(postdefs == NIL)) {

      if (member(TL(postdefs), HD(postdefs)) && !member(dups, HD(postdefs))) {
        dups = cons(HD(postdefs), dups);
      }

      postdefs = TL(postdefs);
    }

    if (!(dups == NIL)) {
      bcpl_writes("/rename illegal because of conflicting uses of ");

      while (!(dups == NIL)) {
        bcpl_writes(NAME((atom)HD(dups)));
        wrch(' ');
        dups = TL(dups);
      }

      wrch('\n');
      return;
    }
  }

  hold_interrupts();
  clearmemory();

  // prepare for assignment to val fields
  {
    list x1 = x;
    list xvals = NIL;
    list targets = NIL;

    while (!(x1 == NIL)) {

      if (member(SCRIPT, HD(x1))) {
        xvals = cons(VAL((atom)HD(x1)), xvals), targets = cons(HD(y), targets);
      }

      x1 = TL(x1);
      y = TL(y);
    }

    // now convert all occurrences in the script
    {
      list s = SCRIPT;

      while (!(s == NIL)) {
        list eqns = TL(VAL((atom)HD(s)));
        word nargs = (word)HD(HD(VAL((atom)HD(s))));
        while (!(eqns == NIL)) {
          list code = TL(HD(eqns));
          if (nargs > 0) {
            list lhs = HD(HD(eqns));

            for (word i = 2; i <= nargs; i++) {
              lhs = HD(lhs);
            }

            HD(lhs) = subst(z, HD(lhs));
          }

          while (iscons(code)) {
            HD(code) = subst(z, HD(code));
            code = TL(code);
          }

          eqns = TL(eqns);
        }

        if (member(x, HD(s))) {
          VAL((atom)HD(s)) = NIL;
        }

        HD(s) = subst(z, HD(s));
        s = TL(s);
      }

      // now reassign val fields
      while (!(targets == NIL)) {
        VAL((atom)HD(targets)) = HD(xvals);
        targets = TL(targets);
        xvals = TL(xvals);
      }

      release_interrupts();
    }
  }
}

static list subst(list z, list a) {

  while (!(z == NIL)) {
    if (a == HD(HD(z))) {
      SAVED = false;
      return TL(HD(z));
    }
    z = TL(z);
  }
  return a;
}

static void newequation() {

  word eqno = -1;

  if (havenum()) {

    // avoid global variables
    // eqno = 100 * THE_NUM;

    eqno = 100 * the_num();

    check((token)')');
  }

  {
    list x = equation();
    if (ERRORFLAG) {
      return;
    }

    {
      atom subject = (atom)HD(x);
      word nargs = (word)HD(TL(x));
      list eqn = TL(TL(x));
      if (ATOBJECT) {
        printobj(eqn);
        wrch('\n');
      }

      if (VAL(subject) == NIL) {
        VAL(subject) = cons(cons((list)nargs, NIL), cons(eqn, NIL));
        enterscript(subject);
      } else if (protected(subject)) {
        return;
      } else if (TL(VAL(subject)) == NIL) {
        // subject currently defined only by a comment
        HD(HD(VAL(subject))) = (list)nargs;
        TL(VAL(subject)) = cons(eqn, NIL);
      } else if (nargs != (word)HD(HD(VAL(subject)))) {

        // simple def silently overwriting existing eqns -
        // removed DT 2015
        // if ( nargs==0) {
        // VAL(subject)=cons(cons(0,TL(HD(VAL(subject)))),cons(eqn,NIL));
        //            clearmemory(); } else

        fprintf(bcpl_OUTPUT, "Wrong no of args for \"%s\"\n", NAME(subject));
        bcpl_writes("Equation rejected\n");
        return;
      } else if (eqno == -1) {
        // unnumbered EQN
        list eqns = TL(VAL(subject));
        list p = profile(eqn);

        do {
          if (equal(p, profile(HD(eqns)))) {
            list code = TL(HD(eqns));
            if (HD(code) == (list)LINENO_C) {
              // if old eqn has line no,

              // new eqn inherits
              TL(TL(code)) = TL(eqn);

              HD(HD(eqns)) = HD(eqn);
            } else {
              HD(eqns) = eqn;
            }
            clearmemory();
            break;
          }
          if (TL(eqns) == NIL) {
            TL(eqns) = cons(eqn, NIL);
            break;
          }
          eqns = TL(eqns);
        } while (1);

      } else {
        // numbered eqn

        list eqns = TL(VAL(subject));
        word n = 0;
        if (eqno % 100 != 0 || eqno == 0) {
          // if eqn has non standard lineno

          // mark with no.
          TL(eqn) = cons((list)LINENO_C, cons((list)eqno, TL(eqn)));
        }

        do {
          n = HD(TL(HD(eqns))) == (list)LINENO_C ? (word)HD(TL(TL(HD(eqns))))
                                                 : (n / 100 + 1) * 100;
          if (eqno == n) {
            HD(eqns) = eqn;
            clearmemory();
            break;
          }

          if (eqno < n) {
            list hold = HD(eqns);
            HD(eqns) = eqn;
            TL(eqns) = cons(hold, TL(eqns));
            clearmemory();
            break;
          }

          if (TL(eqns) == NIL) {
            TL(eqns) = cons(eqn, NIL);
            break;
          }
          eqns = TL(eqns);
        } while (1);
      }
      SAVED = false;
    }
  }
}

// called whenever eqns are destroyed, reordered or
// inserted (other than at the end of a definition)
static void clearmemory() {

  // MEMORIES holds a list of all vars whose memo
  while (!(MEMORIES == NIL)) {

    // fields have been set
    list x = VAL((atom)HD(MEMORIES));

    if (!(x == NIL)) {
      // unset memo field
      HD(HD(TL(x))) = 0;
    }

    MEMORIES = TL(MEMORIES);
  }
}

// enters a in the script
void enterscript(atom a) {

  if (SCRIPT == NIL) {
    SCRIPT = cons((list)a, NIL);
  } else {
    list s = SCRIPT;

    while (!(TL(s) == NIL)) {
      s = TL(s);
    }

    TL(s) = cons((list)a, NIL);
  }
}

static void comment() {

  atom subject = (atom)TL(HD(TOKENS));
  list comment = HD(TL(TOKENS));

  if (VAL(subject) == NIL) {
    VAL(subject) = cons(cons(0, NIL), NIL);
    enterscript(subject);
  }

  if (protected(subject)) {
    return;
  }

  TL(HD(VAL(subject))) = comment;

  if (comment == NIL && TL(VAL(subject)) == NIL) {
    remove_atom(subject);
  }

  SAVED = false;
}

static void evaluation() {

  list code = expression();
  word ch = (word)HD(TOKENS);

  // static so invisible to garbage collector
  list e = 0;

  if (!(have((token)'!'))) {
    check((token)'?');
  }

  if (ERRORFLAG) {
    return;
  }

  check(EOL);

  if (ATOBJECT) {
    printobj(code);
    wrch('\n');
  }

  e = buildexp(code);

  if (ATCOUNT) {
    resetgcstats();
  }

  initstats();
  EVALUATING = true;
  FORMATTING = ch == '?';
  printval(e, FORMATTING);

  if (FORMATTING) {
    wrch('\n');
  }

  closechannels();
  EVALUATING = false;

  if (ATCOUNT) {
    outstats();
  }
}

static void abordercom() { SCRIPT = sort(SCRIPT), SAVED = false; }

static list sort(list x) {

  if (x == NIL || TL(x) == NIL) {
    return x;
  }

  {
    // first split x
    list a = NIL;
    list b = NIL;
    list hold = NIL;

    while (!(x == NIL)) {
      hold = a;
      a = cons(HD(x), b);
      b = hold;
      x = TL(x);
    }

    a = sort(a);
    b = sort(b);

    // now merge the two halves back together
    while (!(a == NIL || b == NIL)) {
      if (alfa_ls((atom)HD(a), (atom)HD(b))) {
        x = cons(HD(a), x);
        a = TL(a);
      } else {
        x = cons(HD(b), x);
        b = TL(b);
      }
    }

    if (a == NIL) {
      a = b;
    }

    while (!(a == NIL)) {
      x = cons(HD(a), x);
      a = TL(a);
    }

    return reverse(x);
  }
}

static void reordercom() {

  if (isid(HD(TOKENS)) &&
      (isid(HD(TL(TOKENS))) || HD(TL(TOKENS)) == (list)DOTDOT_SY)) {

    scriptreorder();

  } else if (haveid() && HD(TOKENS) != EOL) {

    list nos = NIL;

    // avoid global variables
    // word max = no_of_eqns(THE_ID);

    word max = no_of_eqns(the_id());

    while (havenum()) {

      // avoid global variables
      // word a = THE_NUM;

      word a = the_num();

      // avoid global variables
      // word b = have(DOTDOT_SY) ? havenum() ? THE_NUM : max : a;

      word b = have(DOTDOT_SY) ? havenum() ? the_num() : max : a;

      for (word i = a; i <= b; i++) {
        if (!member(nos, (list)i) && 1 <= i && i <= max) {
          nos = cons((list)i, nos);
        }
      }
      // nos out of range are silently ignored
    }

    check(EOL);
    if (ERRORFLAG) {
      return;
    }

    // avoid global variables
    // if (VAL(THE_ID) == NIL) {
    //   display(THE_ID, false, false);
    //   return;
    // }
    //
    // if (protected(THE_ID)) {
    //   return;
    // }

    atom id = the_id();
    if (VAL(id) == NIL) {
      display(id, false, false);
      return;
    }

    if (protected(id)) {
      return;
    }

    {
      for (word i = 1; i <= max; i++) {
        if (!(member(nos, (list)i))) {
          nos = cons((list)i, nos);
        }
      }
      // any eqns left out are tacked on at the end
    }

    // note that "nos" are in reverse order
    {
      list new = NIL;

      // avoid global variables
      // list eqns = TL(VAL(THE_ID));

      list eqns = TL(VAL(id));

      while (!(nos == NIL)) {

        list eqn = elem(eqns, (word)HD(nos));
        removelineno(eqn);
        new = cons(eqn, new);
        nos = TL(nos);
      }

      // note that the eqns in "new" are now in the correct order

      // avoid global variables
      // TL(VAL(THE_ID)) = new;
      // display(THE_ID, true, false);

      TL(VAL(id)) = new;
      display(id, true, false);
      SAVED = false;
      clearmemory();
    }
  } else {
    syntax();
  }
}

static void scriptreorder() {

  list r = NIL;
  while ((haveid())) {

    if (have(DOTDOT_SY)) {

      // avoid global variables;
      // atom a = THE_ID;

      atom a = the_id();
      atom b = 0;
      list x = NIL;

      if (haveid()) {

        // avoid global vairables
        // b = THE_ID;

        b = the_id();
      } else if (HD(TOKENS) == EOL) {
        b = (atom)EOL;
      }

      if (b == 0) {
        syntax();
      } else {
        x = extract(a, b);
      }

      if (x == NIL) {
        syntax();
      }

      r = shunt(x, r);

      // avoid global variables
      // } else if (member(SCRIPT, (list)THE_ID)) {
      //
      //   r = cons((list)THE_ID, r);

    } else if (member(SCRIPT, (list)the_id())) {

      r = cons((list)the_id(), r);

    } else {

      // avoid global variables
      // fprintf(bcpl_OUTPUT, "\"%s\" not in script\n", NAME(THE_ID));

      fprintf(bcpl_OUTPUT, "\"%s\" not in script\n", NAME(the_id()));

      syntax();
    }
  }

  check(EOL);
  if (ERRORFLAG) {
    return;
  }

  {
    list r1 = NIL;
    while (!(TL(r) == NIL)) {
      if (!(member(TL(r), HD(r)))) {
        SCRIPT = sub1(SCRIPT, (atom)HD(r)), r1 = cons(HD(r), r1);
      }

      r = TL(r);
    }
    SCRIPT = append(extract((atom)HD(SCRIPT), (atom)HD(r)),
                    append(r1, TL(extract((atom)HD(r), (atom)EOL))));
    SAVED = false;
  }
}

static word no_of_eqns(atom a) {

  return VAL(a) == NIL ? 0 : length(TL(VAL(a)));
}

// library functions are recognisable by not being part of the script
static bool protected(atom a) {

  if (member(SCRIPT, (list)a)) {
    return false;
  }

  if (member(HOLDSCRIPT, (list)a)) {
    if (!(member(GET_HITS, (list)a))) {
      GET_HITS = cons((list)a, GET_HITS);
    }
    return false;
  }
  fprintf(bcpl_OUTPUT, "\"%s\" is predefined and cannot be altered\n", NAME(a));
  return true;
}

// removes "a" from the script
// renamed to avoid conflict with remove(3)
static void remove_atom(atom a) {

  SCRIPT = sub1(SCRIPT, a);
  VAL(a) = NIL;
}

// returns a segment of the script
static list extract(atom a, atom b) {

  list s = SCRIPT;
  list x = NIL;

  while (!(s == NIL || HD(s) == (list)a)) {
    s = TL(s);
  }

  while (!(s == NIL || HD(s) == (list)b)) {
    x = cons(HD(s), x);
    s = TL(s);
  }

  if (!(s == NIL)) {
    x = cons(HD(s), x);
  }

  if (s == NIL && b != (atom)EOL) {
    x = NIL;
  }

  if (x == NIL) {
    fprintf(bcpl_OUTPUT, "\"%s..%s\" not in script\n", NAME(a),
            b == (atom)EOL ? "" : NAME(b));
  }

  return reverse(x);
}

static void deletecom() {

  list dlist = NIL;
  while (haveid()) {

    if (have(DOTDOT_SY)) {

      // avoid global variables
      // atom a = THE_ID;

      atom a = the_id();
      atom b = (atom)EOL;

      if (haveid()) {

        // avoid global variables
        // b = THE_ID;

        b = the_id();

      } else if (!(HD(TOKENS) == EOL)) {

        syntax();
      }

      dlist = cons(cons((list)a, (list)b), dlist);

    } else {

      // avoid global variables
      // word max = no_of_eqns(THE_ID);

      word max = no_of_eqns(the_id());

      list nlist = NIL;

      while (havenum()) {

        // avoid global variables
        // word a = THE_NUM;
        // word b = have(DOTDOT_SY) ? havenum() ? THE_NUM : max : a;

        word a = the_num();
        word b = have(DOTDOT_SY) ? havenum() ? the_num() : max : a;

        for (word i = a; i <= b; i++) {
          nlist = cons((list)i, nlist);
        }
      }

      // avoid global variables
      // dlist = cons(cons((list)THE_ID, nlist), dlist);

      dlist = cons(cons((list)the_id(), nlist), dlist);
    }
  }

  check(EOL);
  if (ERRORFLAG) {
    return;
  }

  {
    word dels = 0;

    // delete all
    if (dlist == NIL) {
      if (SCRIPT == NIL) {
        displayall(false);
      } else {
        if (!(makesure())) {
          return;
        }

        while (!(SCRIPT == NIL)) {
          dels = dels + no_of_eqns((atom)HD(SCRIPT));
          VAL((atom)HD(SCRIPT)) = NIL;
          SCRIPT = TL(SCRIPT);
        }
      }
    }
    while (!(dlist == NIL)) {
      //"NAME..NAME"
      if (isatom(TL(HD(dlist))) || TL(HD(dlist)) == EOL) {

        list x = extract((atom)HD(HD(dlist)), (atom)TL(HD(dlist)));
        dlist = TL(dlist);

        while (!(x == NIL)) {
          dlist = cons(cons(HD(x), NIL), dlist);
          x = TL(x);
        }

      } else {

        atom name = (atom)HD(HD(dlist));
        list nos = TL(HD(dlist));
        list new = NIL;
        dlist = TL(dlist);

        if (VAL(name) == NIL) {
          display(name, false, false);
          continue;
        }

        if (protected(name)) {
          continue;
        }

        if (nos == NIL) {

          dels = dels + no_of_eqns(name);
          remove_atom(name);
          continue;

        } else {

          for (word i = no_of_eqns(name); i >= 1; i = i - 1) {
            if (member(nos, (list)i)) {
              dels = dels + 1;
            } else {
              list eqn = elem(TL(VAL(name)), i);
              removelineno(eqn);
              new = cons(eqn, new);
            }
          }
        }

        TL(VAL(name)) = new;

        // comment field
        if (new == NIL && TL(HD(VAL(name))) == NIL) {
          remove_atom(name);
        }
      }
    }

    fprintf(bcpl_OUTPUT, "%" W " equations deleted\n", dels);

    if (dels > 0) {
      SAVED = false;
      clearmemory();
    }
  }
}
