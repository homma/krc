
// global functions in reducer
void setup_primfns_etc(void);
void printval(LIST E, bool FORMAT);
LIST buildexp(LIST CODE);
void init_argspace(void);
void ESCAPETONEXTCOMMAND();
void initstats();
void outstats();
void fixup_s(void);
char *scaseconv(char *S);

// global functions in main
void CLOSECHANNELS();
FILE *FINDCHANNEL(char *F);
void ENTERSCRIPT(ATOM A);

// global variables in reducer
extern LIST MEMORIES;
extern word LISTBASE;
extern word ABORTED;

// ----------------------------------------------------------------------
// The KRC system is Copyright (c) D. A. Turner 1981
// All  rights reserved.  It is distributed as free software under the
// terms in the file "COPYING", which is included in the distribution.
// ----------------------------------------------------------------------
