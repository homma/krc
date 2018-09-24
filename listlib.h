// header for list processing package    DAT 23/11/79

//----------------------------------------------------------------------
// The KRC system is Copyright (c) D. A. Turner 1981
// All  rights reserved.  It is distributed as free software under the
// terms in the file "COPYING", which is included in the distribution.
//----------------------------------------------------------------------

#include "common.h"
#include "iolib.h"

// include code to check validity of pointers handed to HD() and TL()
// #define INSTRUMENT_KRC_GC

/* an element in list space */
typedef struct list {
  struct list *hd;
  struct list *tl;
} * list;

// hd can contain:
//
// - the memory address of another cell in the CONS space
//   (cons . next_cell)
//   (cons . nil)
//
// - the memory address of a cell in the atom space
//   (atom . next_cell)
//   (atom . nil)
//
// - improbable pointer values of HD() for special values:
//
//   FULLWORD or GONETO (see listlib.c)
//   (FULLWORD . number)
//   (GONETO . _)

// this causes problems
// #define NIL ((list)0)

// from oldbcpl/listhdr, may need changing
#define NIL ((list)0x40000000)

// list operations

#ifdef INSTRUMENT_KRC_GC
extern list isokcons(list);
#define HD(p) (isokcons(p)->hd)
#define TL(p) (isokcons(p)->tl)
#else
#define HD(p) ((p)->hd)
#define TL(p) ((p)->tl)
#endif

// list processing macros for easier understandings

// (_ . next_token)
#define next(e) TL(e)

// when unaware of the kind of the token
// (any_token . next_token)
#define gettoken(e) HD(e)

// (cons . next_token)
#define getcons(e) HD(e)

// (atom . next_token)
#define getatom(e) HD(e)

// (FULLWORD . num)
#define getint(e) TL(e)

// (IDENT . _)
// (CONST . _)
#define kind(e) HD(e)

// (IDENT . val)
// (CONST . val)
// (CONST . (FULLWORD . num)) => (FULLWORD . num)
// (QUOTE . atom) : ex. (QUOTE . "TRUE")
#define getval(e) TL(e)

// (op . _) : ex. (ALPHA . _)
#define getop(e) HD(e)

/* an element in atom space */
typedef struct atom {
  struct atom *link;
  struct list *val;
  char len;
  char name[];
} * atom;

// atom operations
//
// LINK points to the next item in the linked list of values,
//      or has value 0 if it is the end of this list.
// VAL  points to the item's value in the CONS space.
//      NOTE: VAL is used on items in both atom and CONS spaces.
// NAME is a C string, i.e. followed by a nul character.
// LENA is the address of len.
// LEN  returns length of name.

#define LINK(p) ((p)->link)
#define VAL(p) ((p)->val)
#define NAME(p) ((p)->name)
#define LENA(p) ((p)->len)
#define LEN(p) ((p)->len & 0xff)

// offset of "len" field in pointer words
#define OFFSET 2

// unit of allocation for ATOMSPACE
#define atomsize (sizeof(struct atom))

// unused
// word haveparam(word ch);

extern int ARGC;
extern char **ARGV;
// for picking up system parameters passed to program

extern bool ATGC;

// package specifications:

// "GO", "bases", "space_error" must be defined by the user
// all the other functions etc are defined by the package

// "GO()" is the main routine of the users program (necessary because
// the package has its own "START" routine)

// "bases" is used to inform the package which of the users off-stack
// variables are bases for garbage collection - it should be defined
// thus - "let bases(F) be $( F(@A); F(@B); ... $)" where A, B etc.
// are the relevant variables. see NOTE 1 below.

// "space_error()" defines the action the user wishes to take when list
// space is exhausted (e.g. print a message and call finish)

extern void GO(void);
extern void bases(void (*f)(list *));
extern void space_error(char *message);

list cons(list x, list y);

list stonum(word n);
word getnum(list x);

atom mkatom(char *s);
atom mkatomn(char *s, int len);

void bufch(word ch);
atom packbuffer(void);

// the functions "iscons(X)", "isatom(X)", "isnum(X)" distinguish
// the three different kinds of constructed list object.
// note that the special object "NIL" is neither an atom nor a list.
// (so remember that "NIL" has no printname and no "VAL" field.)
// there is a fifth kind of value which can be stored in a list field
// namely a small integer (where "small" is an implementation dependent
// adjective meaning small enough not to be confused with one of the
// three above mentioned types of list object).
//
// NOTES for 2960/EMAS implementation at UKC:
// "small" here means less than 8 meg -- 2^23?

word iscons(list x);
word isatom(list x);
word isnum(list x);

bool alfa_ls(atom a, atom b);
word length(list x);
word member(list x, list a);
list append(list x, list y);
word equal(list x, list y);
list elem(list x, word n);
void printobj(list x);
void resetgcstats(void);
void force_gc(void);
void reportdic(void);
void listpm(void);
list reverse(list x);
list shunt(list x, list y);
list sub1(list x, atom a);

// NOTES for 2960/EMAS implementation at UKC:

// NOTE 1
// at garbage collection time the bcpl stack is searched and any value
// within the address range of list objects is treated as a base and
// possibly relocated.  it is therefore essential that there should be
// no integers on the stack large enough to be confused with a BCPL
// address - integers less than 8 meg are safe.
