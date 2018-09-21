// HEADER FOR LIST  PROCESSING  PACKAGE    DAT 23/11/79

//----------------------------------------------------------------------
// The KRC system is Copyright (c) D. A. Turner 1981
// All  rights reserved.  It is distributed as free software under the
// terms in the file "COPYING", which is included in the distribution.
//----------------------------------------------------------------------

#include "common.h"
#include "iolib.h"

// Include code to check validity of pointers handed to HD() and TL
// #define INSTRUMENT_KRC_GC

/* An element in LIST space */
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
extern LIST ISOKCONS(LIST);
#define HD(p) (ISOKCONS(p)->hd)
#define TL(p) (ISOKCONS(p)->tl)
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
// link points to the next item in the linked list of values,
//	or has value 0 if it is the end of this list.
// val  points to the item's value in the CONS space.
//      NOTE: VAL is used on items in both ATOM and CONS spaces.
// name is a combined BCPL- and C-like string, i.e. the length in the
//	first byte, then the string itself followed by a nul character.
#define LINK(p) ((p)->link)
#define VAL(p) ((p)->val)
#define NAME(p) ((p)->name)
#define LEN(p) ((p)->name[0] & 0xff)
#define OFFSET 2 // Offset of "name" field in pointer words

#define atomsize (sizeof(struct ATOM)) // unit of allocation for ATOMSPACE

// The C string version of the name
#define PRINTNAME(X) (NAME(X) + 1)

// unused
// word haveparam(word CH);

extern int ARGC;
extern char **ARGV;
// for picking up system parameters passed to program

extern bool ATGC;

// PACKAGE SPECIFICATIONS:

// "GO" "BASES" "SPACE.ERROR"      must be defined by the user
// all the other functions etc are defined by the package

// "GO()"   is the main routine of the users program (necessary because
// the package has its own  "START" routine)
// "BASES" is used to inform the package which of the users off-stack
// variables are bases for garbage collection - it should be defined
// thus -  "let BASES(F) be $( F(@A); F(@B); ... $)" where A, B etc.
// are the relevant variables.  see NOTE 1 below.
// "SPACE.ERROR()" defines the action the user wishes to take when list
// space is exhausted (e.g. print a message and call finish)
extern void GO(void);
extern void BASES(void (*f)(LIST *));
extern void SPACE_ERROR(char *MESSAGE);

// "cons(X,Y)" creates a list cell, Z say, with X and Y for its fields
// and "HD!Z", "TL!Z" give access to the fields
LIST cons(LIST X, LIST Y);

// "STONUM(N)" stores away the number N as a list object and "GETNUM(X)"
// gets it out again.  see NOTE 2 below.
LIST STONUM(word N);
word GETNUM(LIST X);

// "MKATOM(S)" creates an atom from BCPL string S  - atoms are stored
// uniquely, MKATOM uses a hashing algorithm to accomplish this
// efficiently.  "PRINTNAME(X)" recovers the BCPL string. there is a list
// valued field "VAL!A" associated with each atom A, initially
// containing "NIL".
ATOM MKATOM(char *s);
ATOM MKATOMN(char *s, int len);

// "BUFCH(CH)" puts the character ch into a buffer, "PACKBUFFER()"
// empties the buffer and returns an atom formed from the characters
// which had been placed in it(by calling "MKATOM")
void BUFCH(word CH);
ATOM PACKBUFFER(void);

// the functions "ISCONS(X)", "ISATOM(X)", "ISNUM(X)" distinguish
// the three different kinds of constructed list object.
// note that the special object "NIL" is neither an atom nor a list.
// (so remember that "NIL" has no printname and no "VAL" field.)
// there is a fifth kind of value which can be stored in a list field
// namely a small integer (where "small" is an implementation dependent
// adjective meaning small enough not to be confused with one of the
// three above mentioned types of list object - see NOTE 3, below).
word ISCONS(LIST X);
word ISATOM(LIST X);
word ISNUM(LIST X);

// "ALFA.LS(A,B)" tests atoms for alphabetical order
// "LENGTH(X)" gives the length of list X
// "MEMBER(X,A)" says if "A" is = an element of X
// "APPEND(X,Y)" appends (a copy of) list X to the front of list Y
// "EQUAL(X,Y)" determines if list objects X and Y are isomorphic
// "ELEM(X,N)" returns the n'th element of list X
// "PRINTOB(X)" prints an arbitrary list object X
// "FORCE.GC()" forces a garbage collection
// "REVERSE(X)" reverses the list X
// "SHUNT(X,Y)" appends REVERSE(X) to the list Y
// "SUB1(X,A)" removes A from the list X (destructively) if present
bool ALFA_LS(ATOM A, ATOM B);
word LENGTH(LIST X);
word MEMBER(LIST X, LIST A);
LIST APPEND(LIST X, LIST Y);
word EQUAL(LIST X, LIST Y);
LIST ELEM(LIST X, word N);
void PRINTOB(LIST X);
void RESETGCSTATS(void);
void FORCE_GC(void);
void REPORTDIC(void);
void LISTPM(void);
LIST REVERSE(LIST X);
LIST SHUNT(LIST X, LIST Y);
LIST SUB1(LIST X, ATOM A);

// NOTES for 2960/EMAS implementation at UKC:

// NOTE 1
// at garbage collection time the bcpl stack is searched and any value
// within the address range of list objects is treated as a base and
// possibly relocated.  it is therefore essential that there should be
// no integers on the stack large enough to be confused with a BCPL
// address - integers less than 8 meg are safe.

// NOTE 2
// the numbers stored and recovered by mknum and getnum are 32 bit
// integers - take care not to leave them on the stack.

// NOTE 3
// "small" here means less than 8 meg
