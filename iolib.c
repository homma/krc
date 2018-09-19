// helper functions for iolib.h

#include "common.h"
#include "iolib.h"

#include <ctype.h>  // for isdigit()
#include <string.h> // for strcmp()

// EMAS emulation stubs

// on EMAS prompts remain in effect until cancelled
char *emas_PROMPT = "";

// The extra character gobbled up at the end of a call to READN()
int TERMINATOR;

// which file descriptors should BCPL's input and output use?
// we use (FILE *)0 as a synonym for stdin / stdout since
// we can't initialise statically because stdin and stdout
// are not constants on some systems.
FILE *bcpl_INPUT_fp = (FILE *)0;
FILE *bcpl_OUTPUT_fp = (FILE *)0;

FILE *bcpl_FINDINPUT(char *file) {

  if (strcmp(file, ".IN") == 0) {
    file = "/dev/stdin";
  }

  return fopen(file, "r");
}

FILE *bcpl_FINDOUTPUT(char *file) {

  if (strcmp(file, ".OUT") == 0) {
    file = "/dev/stdout";
  }

  if (strcmp(file, ".ERR") == 0) {
    file = "/dev/stderr";
  }

  return fopen(file, "w");
}

// EMAS's READN() gobbles up the following character
// and deposits it in a global variable TERMINATOR.
// so we do the same.

// KRC only needs positive numbers, and an initial digit is guaranteed,
WORD bcpl_READN() {
  WORD D = 0;
  int CH = (*_RDCH)();

  while (isdigit(CH)) {
    D = D * 10 + (CH - '0');
    CH = (*_RDCH)();
  }

  TERMINATOR = CH;

  return D;
}

// the character that has been UNRDCH-ed. -1 means none are pending.
static int UNREADCH = -1;

// the standard function for RDCH(c)
int bcpl_RDCH() {

  if (UNREADCH >= 0) {
    int CH = UNREADCH;
    UNREADCH = -1;
    return CH;
  }

  return getc(bcpl_INPUT);
}

// a version of RDCH that echoes what it reads
int echo_RDCH() {
  int CH;

  if (UNREADCH >= 0) {
    CH = UNREADCH;
    UNREADCH = -1;
    return CH;
  }

  CH = getc(bcpl_INPUT);
  (*_WRCH)(CH);

  return CH;
}

int bcpl_UNRDCH(int c) { return (UNREADCH = c & 0xff); }

// the standard function for WRCH(c)
void bcpl_WRCH(WORD C) { putc(C, bcpl_OUTPUT); }

// _RDCH and _WRCH are the function pointers used to perform
// RDCH() and WRCH() and may be modified to attain special effects.
// normally in BCPL you would say "WRCH=WHATEVER"
// but in C, WRCH and WRCH() would conflict so
// say _WRCH=WHATEVER to change it,

int (*_RDCH)() = bcpl_RDCH;
int (*_UNRDCH)(int) = bcpl_UNRDCH;
void (*_WRCH)(WORD C) = bcpl_WRCH;

// other output functions must go through WRCH so that
// callers may redirect it to some other function.
void bcpl_WRITES(char *s) {

  while (*s) {
    (*_WRCH)(*s++);
  }
}

// helper function writes positive integers
static void bcpl_WRITEP(WORD n) {

  if (n / 10) {
    bcpl_WRITEP(n / 10);
  }

  (*_WRCH)(n % 10 + '0');
}

void bcpl_WRITEN(WORD n) {

  if (n < 0) {
    (*_WRCH)('-');
    n = -n;
  }

  bcpl_WRITEP(n);
}
