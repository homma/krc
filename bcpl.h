// bcpl.h: map BCPL constructs to C equivalents

#ifndef BCPL_H

#include <stdio.h>	// For printf etc
#include <stdlib.h>	// For exit()
#include <limits.h>	// for __WORDSIZE

#if __WORDSIZE==64

// Type for machine words, used for all integer variables.
// 64-bit value.
typedef long long WORD;

// Printf/scanf format to use with WORDs
#define W "lld"

#else

// 32-bit value.
typedef int WORD;
#define W "d"

#endif

// Trap broken compiler versions here, as it is included by everything
// Definitely doesn't work with gcc-4.9.[012]
#if __GNUC__ == 4 && __GNUC_MINOR__ == 9
// && __GNUC_PATCHLEVEL__  < 3
# error "KRC is broken when compiled with GCC 4.9. Earlier GCCs, clang and TinyC work.".
#endif


// bool
typedef WORD BOOL;
#define FALSE 0
#define TRUE 1

// implement BCPL i/o system using stdio
extern FILE *bcpl_INPUT_fp;
extern FILE *bcpl_OUTPUT_fp;
#define bcpl_INPUT (bcpl_INPUT_fp ? bcpl_INPUT_fp : stdin)
#define bcpl_OUTPUT (bcpl_OUTPUT_fp ? bcpl_OUTPUT_fp : stdout)
extern FILE *bcpl_FINDINPUT(char *file);
extern FILE *bcpl_FINDOUTPUT(char *file);
extern WORD bcpl_READN(void);

// RDCH/UNRDCH and WRCH need to be redirectable
extern int bcpl_RDCH(void);
extern int echo_RDCH(void);
extern int bcpl_UNRDCH(int c);
extern void bcpl_WRCH(WORD C);

// and these are the variables that people can redirect if they want to
extern int (*_RDCH)(void);
extern int (*_UNRDCH)(int c);
extern void (*_WRCH)(WORD D);

// output functions must go through WRCH
extern void bcpl_WRITES(char *s);
extern void bcpl_WRITEN(WORD N);

#define BCPL_H
#endif
