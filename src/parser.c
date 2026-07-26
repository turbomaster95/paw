#include <stdio.h>
#include <stdlib.h>
#include <comp.h>
#include <etc.h>
#include <nu.h>
#include <lang.h>
#include <glog.h>
#include <string.h>
#include <nus.h>

extern int yylex(void);
extern char *yytext;
extern int yylineno;
extern nu_ast_t* g_ast;
extern nu_mm_t* g_mm;
extern char* current_filename;

static int current_tok;
extern int tok_col;

typedef enum {
    VAR_START = 0,
    VAR_INT,
    VAR_STRING,
} var_type_t;

struct Symb {
    char *name;
    var_type_t type;
    int scope; /* local or global, in future tho.. */
    struct Symb *next;
};

typedef struct Symb symb;

typedef struct symbol_table {
    symb *head;
} symbt;

static symbt* SymTable;

symb *symtab_add(symbt *table, const char *name, var_type_t type) {
    if (!table) return NULL;
    symb *sym = nu_alloc(g_mm, sizeof(symb));
    sym->name = nu_strdup(name);
    sym->type = type;
    sym->next = table->head;
    table->head = sym;
    return sym;
}

symb *symtab_lookup(symbt *table, const char *name) {
    if (!table) return NULL;
    for (symb *curr = table->head; curr != NULL; curr = curr->next) {
        if (strcmp(curr->name, name) == 0) {
            return curr;
        }
    }
    return NULL; /* Undeclared variable */
}

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
    glog_log(current_filename, line, col, GLOG_ERROR, "Syntax Error: %s", msg);
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
        char errm[128];
        
        snprintf(errm, sizeof(errm), "%s (got %s: \"%s\")\n", msg, tokname(current_tok), yytext);
        synerr(yylineno, tok_col, errm);
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
    
    if (match(CONSTANT)) {
        newstrnode(ret_node, AST_CONST, yytext);
        advance();
    }

    if (match(IDENTIFIER)) {
        symb *sym = symtab_lookup(SymTable, yytext);
        if (sym == NULL) {
            char errm[128];
            snprintf(errm, sizeof(errm), "Unknown Variable '%s'", yytext);
            synerr(yylineno, tok_col, errm);
        }
        newstrnode(ret_node, AST_IDENT, yytext);
        advance();
    }

    expect(';', "Expected ';' after return value");
}

void parse_integer_decl(nu_ast_node_t* root) {
    expect(INT, "Expected 'int'");

    nu_ast_node_t* int_node = newnode(root, AST_INT_DECL);

    if (!match(IDENTIFIER)) {
        synerr(yylineno, tok_col, "Expected an identifier for the int!");
    }

    char* varname = nu_strdup(yytext);
    newstrnode(int_node, AST_VAR_DECL, yytext);
    advance();

    symtab_add(SymTable, varname, VAR_INT);    
    
    if (match('=')) {
        advance();

        if (match(CONSTANT) || match(IDENTIFIER)) {
            uint32_t val_type = match(CONSTANT) ? AST_CONST : AST_IDENT;
            newstrnode(int_node, val_type, yytext);
            advance();
        } else {
            synerr(yylineno, tok_col, "Expected a constant or identifier after '='");
        }
    }
        
    expect(';', "Expected ';' after declaration");
}

static void parse_statement(nu_ast_node_t* root) {
    switch (current_tok) {
        case RETURN:
            parse_return_stmt(root);
            break;

        case INT:
            parse_integer_decl(root);
            break;
            
        default:
            char errm[128];        
            snprintf(errm, sizeof(errm),
                 "Unexpected token '%s' (\"%s\")\n", tokname(current_tok), yytext);
            synerr(yylineno, tok_col, errm);
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

    SymTable = nu_alloc(g_mm, sizeof(symbt));
    SymTable->head = NULL;
   
    while (current_tok != 0) {
        parse_statement(root);
    }
    print_ast(root, 0);
    walk_ast(root);
}
