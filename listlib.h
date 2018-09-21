// header for list processing package    DAT 23/11/79

//----------------------------------------------------------------------
// The KRC system is Copyright (c) D. A. Turner 1981
// All  rights reserved.  It is distributed as free software under the
// terms in the file "COPYING", which is included in the distribution.
//----------------------------------------------------------------------

#include "common.h"
#include "iolib.h"

// include code to check validity of pointers handed to HD() and TL
// #define INSTRUMENT_KRC_GC

/* an element in LIST space */
typedef struct LIST {
  struct LIST *hd;
  struct LIST *tl;
} * LIST;

// HD can contain:
// - the memory address of another cell in the CONS space
// - the memory address of a cell in the ATOM space
// - improbable pointer values of HD() for special values:
//   FULLword (see above) or GONETO (see listlib.c)
//#define NIL ((LIST)0) //causes problems

// from oldbcpl/listhdr, may need changing
#define NIL ((LIST)0x40000000)

#ifdef INSTRUMENT_KRC_GC
extern LIST isokcons(LIST);
#define HD(p) (isokcons(p)->hd)
#define TL(p) (isokcons(p)->tl)
#else
#define HD(p) ((p)->hd)
#define TL(p) ((p)->tl)
#endif

/* An element in ATOM space */
typedef struct ATOM {
  struct ATOM *link;
  struct LIST *val;
  char name[];
} * ATOM;

// LINK points to the next item in the linked list of values,
//	or has value 0 if it is the end of this list.
// VAL  points to the item's value in the CONS space.
//      NOTE: VAL is used on items in both ATOM and CONS spaces.
// NAME is a combined BCPL- and C-like string, i.e. the length in the
//	first byte, then the string itself followed by a nul character.

#define LINK(p) ((p)->link)
#define VAL(p) ((p)->val)
#define NAME(p) ((p)->name)
#define LEN(p) ((p)->name[0] & 0xff)

// offset of "name" field in pointer words
#define OFFSET 2

// unit of allocation for ATOMSPACE
#define atomsize (sizeof(struct ATOM))

// The C string version of the name
#define PRINTNAME(X) (NAME(X) + 1)

// unused
// word haveparam(word CH);

extern int ARGC;
extern char **ARGV;
// for picking up system parameters passed to program

extern bool ATGC;

// package specifications:

// "GO", "BASES", "SPACE.ERROR" must be defined by the user
// all the other functions etc are defined by the package

// "GO()" is the main routine of the users program (necessary because
// the package has its own "START" routine)

// "BASES" is used to inform the package which of the users off-stack
// variables are bases for garbage collection - it should be defined
// thus - "let BASES(F) be $( F(@A); F(@B); ... $)" where A, B etc.
// are the relevant variables. see NOTE 1 below.

// "SPACE_ERROR()" defines the action the user wishes to take when list
// space is exhausted (e.g. print a message and call finish)

extern void GO(void);
extern void BASES(void (*f)(LIST *));
extern void SPACE_ERROR(char *MESSAGE);

// "cons(X,Y)" creates a list cell, Z say, with X and Y for its fields
// and "HD!Z", "TL!Z" give access to the fields
LIST cons(LIST X, LIST Y);

LIST stonum(word N);
word getnum(LIST X);

ATOM mkatom(char *s);
ATOM mkatomn(char *s, int len);

void bufch(word ch);
ATOM packbuffer(void);

// the functions "iscons(X)", "isatom(X)", "isnum(X)" distinguish
// the three different kinds of constructed list object.
// note that the special object "NIL" is neither an atom nor a list.
// (so remember that "NIL" has no printname and no "VAL" field.)
// there is a fifth kind of value which can be stored in a list field
// namely a small integer (where "small" is an implementation dependent
// adjective meaning small enough not to be confused with one of the
// three above mentioned types of list object).
// "small" here means less than 8 meg
word iscons(LIST X);
word isatom(LIST X);
word isnum(LIST X);

bool alfa_ls(ATOM A, ATOM B);
word length(LIST X);
word member(LIST X, LIST A);
LIST append(LIST X, LIST Y);
word equal(LIST X, LIST Y);
LIST elem(LIST X, word N);
void printobj(LIST X);
void resetgcstats(void);
void force_gc(void);
void reportdic(void);
void listpm(void);
LIST reverse(LIST X);
LIST shunt(LIST X, LIST Y);
LIST sub1(LIST X, ATOM A);

// NOTES for 2960/EMAS implementation at UKC:

// NOTE 1
// at garbage collection time the bcpl stack is searched and any value
// within the address range of list objects is treated as a base and
// possibly relocated.  it is therefore essential that there should be
// no integers on the stack large enough to be confused with a BCPL
// address - integers less than 8 meg are safe.
