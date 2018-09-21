// helper functions for iolib.h

#include "common.h"
#include "iolib.h"

#include <ctype.h>  // for isdigit()
#include <string.h> // for strcmp()

// which file descriptors should BCPL's input and output use?
// we use (FILE *)0 as a synonym for stdin / stdout since
// we can't initialise statically because stdin and stdout
// are not constants on some systems.
FILE *bcpl_INPUT_fp = (FILE *)0;
FILE *bcpl_OUTPUT_fp = (FILE *)0;

FILE *bcpl_findinput(char *file) {

  if (strcmp(file, ".IN") == 0) {
    file = "/dev/stdin";
  }

  return fopen(file, "r");
}

FILE *bcpl_findoutput(char *file) {

  if (strcmp(file, ".OUT") == 0) {
    file = "/dev/stdout";
  }

  if (strcmp(file, ".ERR") == 0) {
    file = "/dev/stderr";
  }

  return fopen(file, "w");
}

// the character that has been UNRDCH-ed. -1 means none are pending.
static int UNREADCH = -1;

// the standard function for RDCH(c)
int bcpl_rdch() {

  if (UNREADCH >= 0) {
    int CH = UNREADCH;
    UNREADCH = -1;
    return CH;
  }

  return getc(bcpl_INPUT);
}

// a version of rdch that echoes what it reads
int echo_rdch() {
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

int bcpl_unrdch(int c) { return (UNREADCH = c & 0xff); }

// the standard function for WRCH(c)
void bcpl_wrch(word C) { putc(C, bcpl_OUTPUT); }

// _RDCH and _WRCH are the function pointers used to perform
// RDCH() and WRCH() and may be modified to attain special effects.
// normally in BCPL you would say "WRCH=WHATEVER"
// but in C, WRCH and WRCH() would conflict so
// say _WRCH=WHATEVER to change it,

int (*_RDCH)() = bcpl_rdch;
int (*_UNRDCH)(int) = bcpl_unrdch;
void (*_WRCH)(word C) = bcpl_wrch;

// other output functions must go through WRCH so that
// callers may redirect it to some other function.
void bcpl_writes(char *s) {

  while (*s) {
    (*_WRCH)(*s++);
  }
}

// helper function writes positive integers
static void bcpl_writep(word n) {

  if (n / 10) {
    bcpl_writep(n / 10);
  }

  (*_WRCH)(n % 10 + '0');
}

void bcpl_writen(word n) {

  if (n < 0) {
    (*_WRCH)('-');
    n = -n;
  }

  bcpl_writep(n);
}
