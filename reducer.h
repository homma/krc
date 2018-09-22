
// global functions in reducer
void setup_primfns_etc(void);
void printval(list e, bool format);
list buildexp(list code);
void init_argspace(void);
void escapetonextcommand();
void initstats();
void outstats();
void fixup_s(void);
char *scaseconv(char *s);

// global functions in main
void closechannels();
FILE *findchannel(char *f);
void enterscript(atom a);

// global variables in reducer
extern list MEMORIES;
extern word LISTBASE;
extern word ABORTED;

// ----------------------------------------------------------------------
// The KRC system is Copyright (c) D. A. Turner 1981
// All  rights reserved.  It is distributed as free software under the
// terms in the file "COPYING", which is included in the distribution.
// ----------------------------------------------------------------------
