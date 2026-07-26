#include <stdio.h>
#include <stdlib.h>
#include <comp.h>
#include <etc.h>
#include <nu.h>
#include <lang.h>
#include <string.h>

extern int yylex(void);
extern char *yytext;
extern int yylineno;
extern nu_ast_t* g_ast;
extern nu_mm_t* g_mm;

static int current_tok;

const char *tokname(int token) {
    switch (token) {
        case FUNC:           return "FUNC";
        case IF:             return "IF";
        case INT:            return "INT";
        case RETURN:         return "RETURN";
        case IDENTIFIER:     return "IDENTIFIER";
        case CONSTANT:       return "CONSTANT";
        case STRING_LITERAL: return "STRING_LITERAL";
        default:
            if (token > 0 && token < 256) {
                static char buf[2] = {0};
                buf[0] = (char)token;
                return buf;
            }
            return "UNKNOWN";
    }
}

void synerr(int line, int col, const char* msg) {
    fprintf(stderr, "Syntax Error [%d:%d]: %s", line, col, msg);
    exit(EXIT_FAILURE);
}

static void advance(void) {
    current_tok = yylex();
}

static int match(int expected) {
    return current_tok == expected;
}

static void expect(int expected, const char *msg) {
    if (match(expected)) {
        advance();
    } else {
        fprintf(stderr, "Syntax Error [Line %d]: %s (got %s: \"%s\")\n", 
                yylineno, msg, tokname(current_tok), yytext);
        char errm[128];
        
        snprintf(errm, sizeof(errm), "%s (got %s: \"%s\")\n", msg, tokname(current_tok), yytext);
        synerr(yylineno, 0, errm);
    }
}

static nu_ast_node_t *newnode(nu_ast_node_t *parent, uint32_t type) {
    nu_ast_node_t *node = nu_ast_new_node(g_ast, type);
    if (parent) {
        nu_ast_add_child(parent, node);
    }
    return node;
}

static nu_ast_node_t *newstrnode(nu_ast_node_t *parent, uint32_t type, const char *str) {
    nu_ast_node_t *node = newnode(parent, type);
    if (str) {
        nu_ast_set_str(g_ast, node, str, strlen(str));
    }
    return node;
}

void parse_return_stmt(nu_ast_node_t* root) {
    expect(RETURN, "Expected 'return'");

    nu_ast_node_t* ret_node = newnode(root, AST_RETURN_STMT);
    
    if (match(CONSTANT) || match(IDENTIFIER)) {
        printf("Parsed return value: %s\n", yytext);
        uint32_t val_type = match(CONSTANT) ? AST_CONST : AST_IDENT;
        newstrnode(ret_node, val_type, yytext);
        advance();
    }

    expect(';', "Expected ';' after return value");
}

static void parse_statement(nu_ast_node_t* root) {
    switch (current_tok) {
        case RETURN:
            parse_return_stmt(root);
            break;

        default:
            char errm[128];        
            snprintf(errm, sizeof(errm),
                 "Unexpected token '%s' (\"%s\")\n", tokname(current_tok), yytext);
            synerr(yylineno, 0, errm);
            advance(); /* Skip token to prevent infinite loop */
            break;
    }
}

void print_ast(nu_ast_node_t *node, int depth) {
    if (!node) return;

    for (int i = 0; i < depth; i++) printf("  ");

    printf("- [%s]", ast_type_name(node->type));
    if (node->val.str) {
        printf(" \"%s\"", node->val.str);
    } else if (node->val.i64) {
        printf(" %ld", node->val.i64);
    }
    printf("\n");

    for (nu_ast_node_t *child = node->first_child; child != NULL; child = child->next_sibling) {
        print_ast(child, depth + 1);
    }
}

void parse(void) {
    advance(); /* prime the stream */
    nu_ast_node_t *root = newnode(NULL, AST_ROOT);
    g_ast->root = root;
   
    while (current_tok != 0) {
        parse_statement(root);
    }
    print_ast(root, 0);
}
