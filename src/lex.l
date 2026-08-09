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
"#"[^\n]*               { /* consume preprocessor? / single line comment */ }

"func"                  { return(FUNC); }
"if"                    { return(IF); }
"int"                   { return(INT); }
"char"                  { return(CHAR); }
"const"                 { return(CONST); }
"return"                { return(RETURN); }
"printf"                { return(PRINTF); }

{IDENT_HEAD}({IDENT_HEAD}|{DIGIT})* {
    yylval.str_val = strdup(yytext);
    return(IDENTIFIER);
}

0[xX]{HEX_DIGIT}+{INT_SUFFIX}? {
    yylval.int_val = strtoll(yytext, NULL, 16);
    return(CONSTANT);
}

0[0-7]*{INT_SUFFIX}? {
    yylval.int_val = strtoll(yytext, NULL, 8);
    return(CONSTANT);
}

[1-9]{DIGIT}*{INT_SUFFIX}? {
    yylval.int_val = strtoll(yytext, NULL, 10);
    return(CONSTANT);
}

L?\'(\\.|[^\'\\\n])+\' {
    if (yytext[0] == 'L') {
        yylval.int_val = yytext[2];
    } else {
        yylval.int_val = yytext[1];
    }
    return(CONSTANT);
}

L?\"(\\.|[^\"\\\n])*\" {
    yylval.str_val = strdup(yytext);
    return(STRING_LITERAL);
}

\n                      { tok_col = 1; }
[ \t\v\f]+              { /* Skip whitespace */ }

";"|"="|"+"|"-"|"*"|"/"|"("|")"|"{"|"}"|","|"<"|">"|"!"|"&"|"|" {
    return yytext[0];
}

. {
    char msg[64];
    snprintf(msg, sizeof(msg), "unknown character '%c' (0x%02x)\n", yytext[0], (unsigned char)yytext[0]);
    error(msg);
    errcnt++;
    exit(EXIT_FAILURE);
}

%%


void error(const char *s)
{
    fprintf(stderr, "Error: %i:%i:%s %s", yylineno, tok_col, current_filename ? current_filename : "<input>", s);
}

void complain(void)
{
    printf("W: Unmatched Characters!\n");
}
