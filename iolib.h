// iolib.h: compatible i/o functions

#ifndef IOLIB_H

#include <stdio.h>  // For printf etc
#include <stdlib.h> // For exit()

// implement BCPL i/o system using stdio
extern FILE *bcpl_INPUT_fp;
extern FILE *bcpl_OUTPUT_fp;
#define bcpl_INPUT (bcpl_INPUT_fp ? bcpl_INPUT_fp : stdin)
#define bcpl_OUTPUT (bcpl_OUTPUT_fp ? bcpl_OUTPUT_fp : stdout)
extern FILE *bcpl_findinput(char *file);
extern FILE *bcpl_findoutput(char *file);

// RDCH/UNRDCH and WRCH need to be redirectable
extern int bcpl_rdch(void);
extern int echo_rdch(void);
extern int bcpl_unrdch(int c);
extern void bcpl_wrch(word c);

// and these are the variables that people can redirect if they want to
extern int (*rdch)(void);
extern int (*unrdch)(int c);
extern void (*wrch)(word d);

// output functions must go through WRCH
extern void bcpl_writes(char *s);
extern void bcpl_writen(word n);

// other stuff;
//
// APTOVEC(function, size)
//	allocates size+1 bytes on the stack and calls the function
//	passing the address of the vector and the "size" parameter.
// FILES(DESCRIPTOR(), PARAMS)
//      lists the names of your files

#define IOLIB_H
#endif
