// Header file for EMAS emulation stubs

extern char *emas_PROMPT;

// the character gobbled after a READN()
extern int TERMINATOR;

#define PROMPT(S)         emas_PROMPT=S
#define SUPPRESSPROMPTS() emas_PROMPT=""

// other stuff;
//
// APTOVEC(function, size)
//	allocates size+1 bytes on the stack and calls the function
//	passing the address of the vector and the "size" parameter.
// FILES(DESCRIPTOR(), PARAMS)
//      lists the names of your files
