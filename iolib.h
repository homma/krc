// iolib.h: compatible i/o functions

#ifndef IOLIB_H

#include <stdio.h>  // For printf etc
#include <stdlib.h> // For exit()

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

#define IOLIB_H
#endif
