%option noyywrap

DIGIT                   [0-9]
HEX_DIGIT               [a-fA-F0-9]
IDENT_HEAD              [a-zA-Z_]

%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void comment(void);
void error(const char *s);
void complain(void);

%}

%%
/* ""                { comment(); } /*

"//"[^\n]*          { /* consume single line comment */ }
"#"[^\n]*          { /* consume single line comment */ }

"func"		    { return(FUNC);
"if"                { return(IF); }
"int"               { return(INT); }
"return"            { return(RETURN); }

{L}({L}|{D})*        { 
    int token = check_type();
    yylval.str_val = nu_strdup(yytext);
    return token;
}

0[xX]{H}+{IS}?        { 
    yylval.int_val = strtoll(yytext, NULL, 16); 
    return(CONSTANT); 
}
0[0-7]*{IS}?        { 
    yylval.int_val = strtoll(yytext, NULL, 8); 
    return(CONSTANT); 
}
[1-9]{D}*{IS}?        { 
    yylval.int_val = strtoll(yytext, NULL, 10); 
    return(CONSTANT); 
}
L?'(\\.|[^\\'\n])+'    { 
    if (yytext[0] == 'L') {
        yylval.int_val = yytext[2];
    } else {
        yylval.int_val = yytext[1];
    }
    return(CONSTANT); 
}

{D}+{E}{FS}?                { yylval.int_val = strtoll(yytext, NULL, 10); return(CONSTANT); }
{D}*"."{D}+{E}?{FS}?        { yylval.int_val = (int64_t)strtod(yytext, NULL); return(CONSTANT); }
{D}+"."{D}*{E}?{FS}?        { yylval.int_val = (int64_t)strtod(yytext, NULL); return(CONSTANT); }
0[xX]{H}+{P}{FS}?           { yylval.int_val = strtoll(yytext, NULL, 16); return(CONSTANT); }
0[xX]{H}*"."{H}+{P}?{FS}?   { yylval.int_val = strtoll(yytext, NULL, 16); return(CONSTANT); }
0[xX]{H}+"."{H}*{P}?{FS}?   { yylval.int_val = strtoll(yytext, NULL, 16); return(CONSTANT); }

L?\"(\\.|[^\\"\n])*\"    { 
    yylval.str_val = nu_strdup(yytext);
    return(STRING_LITERAL); 
}

"..."             { return(ELLIPSIS); }
">>="             { return(RIGHT_ASSIGN); }
"<<="             { return(LEFT_ASSIGN); }
"+="              { return(ADD_ASSIGN); }
"-="              { return(SUB_ASSIGN); }
"*="              { return(MUL_ASSIGN); }
"/="              { return(DIV_ASSIGN); }
"%="              { return(MOD_ASSIGN); }
"&="              { return(AND_ASSIGN); }
"^="              { return(XOR_ASSIGN); }
"|="              { return(OR_ASSIGN); }
">>"              { return(RIGHT_OP); }
"<<"              { return(LEFT_OP); }
"++"              { return(INC_OP); }
"--"              { return(DEC_OP); }
"->"              { return(PTR_OP); }
"&&"              { return(AND_OP); }
"||"              { return(OR_OP); }
"<="              { return(LE_OP); }
">="              { return(GE_OP); }
"=="              { return(EQ_OP); }
"!="              { return(NE_OP); }
";"               { return(';'); }
("{"|"<%")        { return('{'); }
("}"|"%>")        { return('}'); }
","               { return(','); }
":"               { return(':'); }
"="               { return('='); }
"("               { return('('); }
")"               { return(')'); }
("["|"<:")        { return('['); }
("]"|":>")        { return(']'); }
"."               { return('.'); }
"&"               { return('&'); }
"!"               { return('!'); }
"~"               { return('~'); }
"-"               { return('-'); }
"+"               { return('+'); }
"*"               { return('*'); }
"/"               { return('/'); }
"%"               { return('%'); }
"<"               { return('<'); }
">"               { return('>'); }
"^"               { return('^'); }
"|"               { return('|'); }
"?"               { return('?'); }

[ \t\v\n\f]+      { /* Handled */}

.                 {
                    char msg[64];
                    snprintf(msg, sizeof(msg), "unmatched character '%c'", yytext[0]);
                    cerror(msg);
                  }
%%

#undef yylex

int yylex(void) {
    prev_tok_line = tok_line;
    prev_tok_col  = tok_col;
    prev_tok_len  = yyleng;

    return yylex_internal();
}

void comment(void)
{
    int c;
    int prev = 0;

    while ((c = input()) != 0) {
        if (c == '\n') {
            line_no++;
            column = 1;
        } else if (c == '\t') {
            column += 8 - ((column - 1) % 8);
        } else {
            column++;
        }

        if (prev == '*' && c == '/')
            return;

        prev = c;
    }

    cerror("unterminated comment");
}

int check_type(void)
{
    if (is_registered_type(yytext)) {
        return TYPE_NAME; 
    }
    return IDENTIFIER;
}

int errcnt = 0;

void cerror(const char *s)
{
    glog_log(current_filename ? current_filename : "<input>", 
             tok_line, tok_col, GLOG_ERROR, "%s", s);

    errcnt++;
    if (errcnt >= errlimt) {
      printf("\x1b[91mError:\x1b[0m Error Count reached!\n");
      printf("\x1b[32mTip:\x1b[0m Set -el or --error-limit to increase/decrease this limit.\n");
      exit(0);
    }
}

void complain(void)
{
    printf("W: Unmatched Characters!");
}
