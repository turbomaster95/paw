%option noyywrap
%option noinput
%option nounput
%option yylineno

DIGIT                   [0-9]
HEX_DIGIT               [a-fA-F0-9]
IDENT_HEAD              [a-zA-Z_]
INT_SUFFIX              ([uU]|[uU]?(l|L|ll|LL)|(l|L|ll|LL)[uU])

%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <comp.h>

extern char* current_filename;

void comment(void);
void error(const char *s);
void complain(void);

/* Column tracking */
int tok_col = 1;

int errcnt = 0;

#define YY_USER_ACTION tok_col += yyleng;

%}

%%
"//"[^\n]*              { /* consume single line comment */ }
^"#line"[^\n]*          { /* consume preprocessor line marker */ }

"=="                    { return(EQ); }
"!="                    { return(NEQ); }
"<="                    { return(LEQ); }
">="                    { return(GEQ); }
"->"                    { return(RARROW); }
"<-"                    { return(LARROW); }
"<<"                    { return(SHL); }
">>"                    { return(SHR); }

"func"                  { return(FUNC); }
"int"                   { return(INT); }
"char"                  { return(CHAR); }
"const"                 { return(CONST); }
"return"                { return(RETURN); }
"printf"                { return(PRINTF); }
"print"                 { return(PRINT); }
"extern"		{ return(EXTERN); }
"lib"			{ return(LIB); }
"if"                    { return(IF); }
"while"                    { return(WHILE); }

{IDENT_HEAD}({IDENT_HEAD}|{DIGIT})* {
    return(IDENTIFIER);
}

0[xX]{HEX_DIGIT}+{INT_SUFFIX}? {
    return(CONSTANT);
}

0[0-7]*{INT_SUFFIX}? {
    return(CONSTANT);
}

[1-9]{DIGIT}*{INT_SUFFIX}? {
    return(CONSTANT);
}

L?\'(\\.|[^\'\\\n])+\' {
    return(CONSTANT);
}

L?\"(\\.|[^\"\\\n])*\" {
    return(STRING_LITERAL);
}

\n                      { tok_col = 1; }
[ \t\v\f]+              { /* Skip whitespace */ }

";"|"="|"+"|"-"|"*"|"/"|"("|")"|"{"|"}"|","|"<"|">"|"!"|"&"|"|"|"%"|"~"|"^"|"."|"#"|"["|"]" {
    return yytext[0];
}

. {
    char msg[64];
    snprintf(msg, sizeof(msg), _("unknown character '%c' (0x%02x)\n"), yytext[0], (unsigned char)yytext[0]);
    error(msg);
    errcnt++;
    exit(EXIT_FAILURE);
}

%%


void error(const char *s)
{
    fprintf(stderr, _("Error: %i:%i:%s %s"), yylineno, tok_col, current_filename ? current_filename : "<input>", s);
}

void complain(void)
{
    printf(_("W: Unmatched Characters!\n"));
}
