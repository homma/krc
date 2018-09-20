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
extern word bcpl_READN(void);

// RDCH/UNRDCH and WRCH need to be redirectable
extern int bcpl_RDCH(void);
extern int echo_RDCH(void);
extern int bcpl_UNRDCH(int c);
extern void bcpl_WRCH(word C);

// and these are the variables that people can redirect if they want to
extern int (*_RDCH)(void);
extern int (*_UNRDCH)(int c);
extern void (*_WRCH)(word D);

// output functions must go through WRCH
extern void bcpl_WRITES(char *s);
extern void bcpl_WRITEN(word N);

// the character gobbled after a READN()
extern int TERMINATOR;

// other stuff;
//
// APTOVEC(function, size)
//	allocates size+1 bytes on the stack and calls the function
//	passing the address of the vector and the "size" parameter.
// FILES(DESCRIPTOR(), PARAMS)
//      lists the names of your files

#define IOLIB_H
#endif
