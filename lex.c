#include "bcpl.h"
#include "emas.h"
#include "listhdr.h"
#include "comphdr.h"

#include <ctype.h>

// KRC LEX ANALYSER

//----------------------------------------------------------------------
//The KRC system is Copyright (c) D. A. Turner 1981
//All  rights reserved.  It is distributed as free software under the
//terms in the file "COPYING", which is included in the distribution.
//----------------------------------------------------------------------

#define DECIMALS 0	// code for reading decimals in equation numbers is ill

// Global variables owned by lex.c
WORD ERRORFLAG, EQNFLAG, EXPFLAG, COMMENTFLAG;
BOOL SKIPCOMMENTS;
LIST TOKENS = 0;
ATOM THE_ID = 0;
LIST THE_CONST = 0;
WORD THE_NUM, THE_DECIMALS;

// Local function declarations
#ifdef DECIMALS
static WORD	PEEKDIGIT(void);
#endif
static TOKEN	READTOKEN(void);
static WORD	READ_DECIMALS(void); //RETURNS VALUE IN HUNDREDTHS
static WORD	PEEKALPHA(void);

// Local variables
static BOOL EXPECTFILE=FALSE;
static TOKEN MISSING;

// READS THE NEXT LINE INTO "TOKENS"
void
READLINE()
{  do
   {  LIST *P=&TOKENS;
      WRITES(emas_PROMPT);
      TOKEN T=0;
      MISSING=0;
      TOKENS=NIL;
      THE_DECIMALS=0;
      ERRORFLAG=FALSE;
      EXPFLAG=FALSE;  // WILL GET SET IF THE LINE CONTAINS "?" OR "!"
      COMMENTFLAG=0;  //>0 records number of lines in the comment
      EXPECTFILE=FALSE;
      EQNFLAG=FALSE;  //WILL GET SET IF THE LINE CONTAINS "="
      do { T=READTOKEN();
           *P=CONS((LIST)T,NIL); P=&(TL(*P)); // GCC
         } while (!(T==(TOKEN)EOL || T==(TOKEN)ENDSTREAMCH ||
			T==(TOKEN)BADTOKEN));
      // Ignore first line of Unix script file
      if ( HD(TOKENS)==(LIST)'#' && ISCONS(TL(TOKENS)) &&
         HD(TL(TOKENS))==(LIST)'!' ) continue;
      if ( T==(TOKEN)EOL || T==(TOKEN)ENDSTREAMCH ) return;
      WRITES("Closing quote missing - line ignored\n");
      ERRORFLAG=TRUE; return;
   } while(1);
}

#define NOTCH(CH) (CH=='\\'||CH=='~' && LEGACY)

static TOKEN
READTOKEN(void)
// TOKEN ::= CHAR | <CERTAIN DIGRAPHS, REPRESENTED BY NOS ABOVE 256> |
//          | CONS(IDENT,ATOM) | CONS(CONST,<ATOM|NUM>)
{  WORD CH=RDCH();
   while ( (CH==' '||CH=='\t') ) CH=RDCH();
   if ( CH=='\n' ) return (TOKEN)EOL;
   if ( CH==EOF  ) return (TOKEN)ENDSTREAMCH;
   if ( ('a'<=CH && CH<='z') || ('A'<=CH && CH<='Z')
      // || (CH=='_' && PEEKALPHA()) //expt to allow _ID, discontinued
	|| (EXPECTFILE && !isspace(CH))
   ) {do{  BUFCH(CH);
            CH=RDCH();
         } while ( ('a'<=CH&&CH<='z') || ('A'<=CH&&CH<='Z') ||
			isdigit(CH)||CH=='\''||CH=='_'||
			(EXPECTFILE && !isspace(CH)));
         UNRDCH(CH);
      {  LIST X=(LIST)PACKBUFFER();
         if ( TOKENS!=NIL && HD(TOKENS)==(TOKEN)'/' &&
	     TL(TOKENS)==NIL && MEMBER(FILECOMMANDS,X)
         ) EXPECTFILE=TRUE;
         return CONS((LIST)IDENT,X);  }  }
#if DECIMALS
   // EMAS's READN GOBBLES THE FIRST CHAR AFTER THE NUMBER AND LEAVES IT IN
   // GLOBAL VARIABLE "TERMINATOR". obcpl ALSO EATS THE FOLLOWING CHAR BUT
   // DOESN'T HAVE "TERMINATOR" WHILE Richards' 2013 BCPL DOESN'T GOBBLE IT.
   // CONCLUSION: DON'T USE READN()
   if ( isdigit(CH) || CH=='.' && TOKENS==NIL && PEEKDIGIT()
   ) {  if ( CH=='.'
         ) {  THE_NUM==0;
                 TERMINATOR=='.';  }
         else {  UNRDCH(CH) ; THE_NUM=READN();  }
         if ( TOKENS==NIL && TERMINATOR=='.'  //LINE NUMBERS (ONLY) ARE
         ) THE_DECIMALS==READ_DECIMALS();  //ALLOWED A DECIMAL PART
         else UNRDCH(CH);
         return CONS(CONST,STONUM(THE_NUM)); }
#else
   if ( isdigit(CH)
   ) {  THE_NUM  = 0;
         while( isdigit(CH)
         ) {  THE_NUM = THE_NUM * 10 + CH - '0';
               if ( THE_NUM < 0
               ) {  WRITES("\n**integer overflow**\n");
                     ESCAPETONEXTCOMMAND();  }
	       CH = RDCH();  }
         if ( CH != EOF ) UNRDCH(CH);
         return CONS((TOKEN)CONST,STONUM(THE_NUM)); }
#endif
   if ( CH=='"'
   ) {  ATOM A;
         CH=RDCH();
         while(! (CH=='"'||CH=='\n'||CH==EOF)
         ) {  if ( CH=='\\' //add C escape chars, DT 2015
               ) { CH=RDCH();
                      switch(CH)
                      { case 'a': BUFCH('\a'); break;
                        case 'b': BUFCH('\b'); break;
                        case 'f': BUFCH('\f'); break;
                        case 'n': BUFCH('\n'); break;
                        case 'r': BUFCH('\r'); break;
                        case 't': BUFCH('\t'); break;
                        case 'v': BUFCH('\v'); break;
                        case '\\': BUFCH('\\'); break;
                        case '\'': BUFCH('\''); break;
                        case '\"': BUFCH('\"'); break;
                        case '\n': return (TOKEN)BADTOKEN;
                        default: if ( '0'<=CH&&CH<='9'
                                 ) { int i=3,n=CH-'0',n1;
                                      CH=RDCH();
                                      while ( --i && '0'<=CH&&CH<='9' && (n1=10*n+CH-'0')<256
                                      ) n=n1, CH=RDCH();
                                      BUFCH(n);
                                      UNRDCH(CH); }
                    } }
               else BUFCH(CH);
               CH=RDCH();  }
         A=PACKBUFFER();
         return CH!='"' ? (TOKEN)BADTOKEN : CONS(CONST,(LIST)A);  }
{  WORD CH2=RDCH();
   if ( CH==':' && CH2=='-' && TOKENS!=NIL && ISCONS(HD(TOKENS)) &&
      HD(HD(TOKENS))==IDENT && TL(TOKENS)==NIL
   ) {  LIST C=NIL;
         LIST SUBJECT=TL(HD(TOKENS));
         COMMENTFLAG=1;
         //SUPPRESSPROMPTS(); FIXME
         CH=RDCH();
         while ( CH=='\n' ) COMMENTFLAG++,CH=RDCH(); //IGNORE BLANK LINES
         if ( SKIPCOMMENTS  //option -s
         ) { while(!( CH==';' || CH==EOF
              )) { if ( CH=='\n' ) COMMENTFLAG++;
                   CH=RDCH(); }
              return NIL; }
         if ( CH==';' ) return NIL;
         while(!( CH==';' || CH==EOF
         )) if ( CH=='\n'
            ) {  C=CONS((LIST)PACKBUFFER(),C);
                    do { COMMENTFLAG++;
                         CH=RDCH(); } while (CH=='\n');
                                    //IGNORE BLANK LINES
                 }
            else {  BUFCH(CH); CH=RDCH();  }
         if ( CH==EOF
         ) { WRITEF("%s :- ...",PRINTNAME((ATOM)SUBJECT)),
	        WRITES(" missing \";\"\n");
	        COMMENTFLAG--;
	        SYNTAX(); }
         else C=CONS((LIST)PACKBUFFER(),C);
         return REVERSE(C); }
   if ( CH==CH2
   ) {  if ( CH=='+' ) return PLUSPLUS_SY;
         if ( CH=='.' ) return DOTDOT_SY;
         if ( CH=='-' ) return DASHDASH_SY;
         if ( CH=='*' ) return STARSTAR_SY;
         if ( CH=='=' ) return EQ_SY; // ADDED DT 2015
         if ( CH=='|' ) // COMMENT TO END OF LINE (NEW)
         do{ CH=RDCH();
             if ( CH=='\n' ) return EOL;
             if ( CH==EOF  ) return ENDSTREAMCH;
         } while(1);
      }
   if ( CH=='<' && '-'==CH2 ) return BACKARROW_SY;
   if ( CH2=='='
   ) {  if ( CH=='>' ) return GE_SY;
         if ( CH=='<' ) return LE_SY;
         if ( NOTCH(CH) ) return NE_SY;
      }
   UNRDCH(CH2);
   if ( CH=='?'||CH=='!' ) EXPFLAG=TRUE;
   if ( CH=='=' && !LEGACY ) EQNFLAG=TRUE;
   return (TOKEN)(NOTCH(CH) ? '\\' : CH);  // GCC WARNING EXPECTED
}  }

WORD
CASECONV(WORD CH)
{
   return tolower(CH);
}

#ifdef DECIMALS
WORD
PEEKDIGIT()
{  WORD CH=RDCH();
   UNRDCH(CH);
   return (isdigit(CH));
}

static WORD
READ_DECIMALS(void)         //RETURNS VALUE IN HUNDREDTHS
{  WORD N=0,F=10,D;
   do {
      D=RDCH()-'0';
      while(! (0<=D && D<=9)
      ) {  D=D+'0';
            while ( D==' ' ) D=RDCH();
            while(!( D==')' )) SYNTAX();
            UNRDCH(D);
            return N;  }
      N=N+F*D; //NOTE THAT DECIMAL PLACES AFTER THE 2ND WILL HAVE NO
      F=F/10;  //EFFECT ON THE ANSWER
   } while(1);
}
#endif

static WORD
PEEKALPHA()
{  WORD CH=RDCH();
   UNRDCH(CH);
   return (('a'<=CH && CH<='z') || ('A'<=CH && CH<='Z'));
}

void
WRITETOKEN(TOKEN T)
{  if ( T<(TOKEN)256 && T>(TOKEN)32 ) WRCH((WORD)T); OR
   switch( (WORD)T )
   {  case (WORD)'\n':   WRITES("newline"); break;
      case (WORD)PLUSPLUS_SY: WRITES("++"); break;
      case (WORD)DASHDASH_SY: WRITES("--"); break;
      case (WORD)STARSTAR_SY: WRITES("**"); break;
      case (WORD)GE_SY:       WRITES(">="); break;
      case (WORD)LE_SY:       WRITES("<="); break;
      case (WORD)NE_SY:       WRITES("\\="); break;
      case (WORD)EQ_SY:       WRITES("=="); break; 
      case (WORD)BACKARROW_SY: WRITES("<-"); break;
      case (WORD)DOTDOT_SY: WRITES(".."); break;
      default: if ( !(ISCONS(T) && (HD(T)==IDENT || HD(T)==CONST))
	       ) WRITEF("<UNKNOWN TOKEN<%p>>",T); OR
	       if ( HD(T)==IDENT
	       ) WRITES(PRINTNAME((ATOM)(
			ISCONS(TL(T)) && HD(TL(T))==(LIST)ALPHA
				 ? TL(TL(T)) : TL(T)))); OR
	       if ( ISNUM(TL(T))
	       ) WRITEN(GETNUM(TL(T)));
	       else WRITEF("\"%s\"",PRINTNAME((ATOM)TL(T)));
}  }

BOOL
HAVE(TOKEN T)
{  if ( TOKENS==NIL || HD(TOKENS)!=T ) return FALSE;
   TOKENS=TL(TOKENS);
   return TRUE; }

void
CHECK(TOKEN T)
{ if ( HAVE(T) ) return;
  ERRORFLAG=TRUE;
  if ( MISSING==0 ) MISSING=T; }

void
SYNTAX()
{  ERRORFLAG=TRUE; }

WORD
HAVEID()
{  while(!( ISCONS(HD(TOKENS)) && HD(HD(TOKENS))==IDENT
   )) return FALSE;
   THE_ID=(ATOM) TL(HD(TOKENS));
   TOKENS=TL(TOKENS);
   return TRUE; }

WORD
HAVECONST()
{  while(!( ISCONS(HD(TOKENS)) && HD(HD(TOKENS))==CONST
   )) return FALSE;
   THE_CONST=TL(HD(TOKENS));
   TOKENS=TL(TOKENS);
   return TRUE; }

WORD
HAVENUM()
{  while(!( ISCONS(HD(TOKENS)) && HD(HD(TOKENS))==CONST &&
          ISNUM(TL(HD(TOKENS))) )) return FALSE;
   THE_NUM=GETNUM(TL(HD(TOKENS)));
   TOKENS=TL(TOKENS);
   return TRUE;  }

void
SYNTAX_ERROR(char *message) //syntax error diagnosis (needs refining)
{  if ( ISCONS(TOKENS) && HD(TOKENS)!=BADTOKEN //unclosed string quotes
   ) { WRITES("**unexpected `"),WRITETOKEN(HD(TOKENS)),WRCH('\'');
        if ( MISSING && MISSING!=EOL && MISSING!=(TOKEN)';' && MISSING!=(TOKEN)'\''
        ) { WRITES(", missing `"),WRITETOKEN(MISSING),WRCH('\'');
             if ( MISSING==(TOKEN)'?' ) WRITES(" or `!'"); }
        WRCH('\n'); }
   WRITES(message);
}
