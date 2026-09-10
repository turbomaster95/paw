#include <stdio.h>
#include <stdlib.h>
#include <comp.h>
#include <etc.h>
#include <nu.h>
#include <glog.h>
#include <string.h>
#include <nus.h>
#include <type.h>

#define NEED_TYPENAME
#include <lang.h>

extern int yylex(void);
extern char *yytext;
extern int yylineno;
extern nu_ast_t* g_ast;
extern nu_mm_t* g_mm;
extern char* current_filename;

static int current_tok;
extern int tok_col;
nu_ast_node_t *g_root_node = NULL;

symbt* SymTable;

nu_ast_node_t* parse_expression(nu_ast_node_t* parent);

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

void err(const char* msg) {
    glog_log(current_filename, 0, 0, GLOG_ERROR, "Error: %s", msg);
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

nu_ast_node_t* parse_primary(nu_ast_node_t* parent) {
    if (match('-')) {
        advance();
        nu_ast_node_t* neg_node = newnode(parent, AST_NEGATIVE);
        parse_primary(neg_node);
        return neg_node;
    }

    if (match(CONSTANT)) {
        nu_ast_node_t* node = newstrnode(parent, AST_CONST, yytext);
        advance();
        return node;
    }

    if (match(STRING_LITERAL)) {
        nu_ast_node_t* node = newstrnode(parent, AST_CONST, yytext);
        advance();
        return node;
    }

    if (match(IDENTIFIER)) {
        char name[64];
        snprintf(name, sizeof(name), "%s", yytext);
        advance();

        if (match('(')) {
            advance();

            nu_ast_node_t* call_node = newstrnode(parent, AST_FUNC_CALL, name);

            while (!match(')') && current_tok != 0) {
                parse_expression(call_node);
                if (match(',')) {
                    advance();
                }
            }

            expect(')', "Expected ')' after function arguments");
            return call_node;
        }

        return newstrnode(parent, AST_IDENT, name);
    }

    synerr(yylineno, 0, "Expected expression");
    return NULL;
}

static int get_tok_precedence(int tok) {
    switch (tok) {
        case '|': return 1;                  // Bitwise OR
        case '^': return 2;                  // Bitwise XOR
        case '&': return 3;                  // Bitwise AND
        case '<<':
        case '>>': return 4;
        case '+':
        case '-': return 5;                  // Additive +, -
        case '*':
        case '/':
        case '%': return 6;                  // Multiplicative *, /, %
        default:  return 0;                  // Not a binary operator
    }
}

static uint32_t get_ast_op_type(int tok) {
    switch (tok) {
        case '+': return AST_ADD;
	case '-': return AST_SUB;
        case '*': return AST_MUL;
	case '/': return AST_DIV;
	case '%': return AST_MOD;
        case '&': return AST_BAND; 
	case '|': return AST_BOR;  
	case '^': return AST_BXOR;
        case '<<': return AST_SHL;
        case '>>': return AST_SHR;
        default:  return 0;
    }
}

nu_ast_node_t* parse_unary(void) {
    if (current_tok == '-' || current_tok == '~' || current_tok == '!') {
        int op = current_tok;
        uint32_t op_type = (op == '-') ? AST_NEGATIVE : (op == '~') ? AST_BNOT : AST_LNOT;
        
        char op_str[2] = {(char)op, '\0'};
        nu_ast_node_t* op_node = newstrnode(NULL, op_type, op_str);
        advance();

        nu_ast_node_t* operand = parse_unary();
        if (operand) {
            nu_ast_add_child(op_node, operand);
        }
        return op_node;
    }

    return parse_primary(NULL);
}

nu_ast_node_t* parse_expression_prec(int min_prec) {
    nu_ast_node_t* left = parse_unary();

    while (1) {
        int prec = get_tok_precedence(current_tok);
        if (prec < min_prec) break;

        int op_tok = current_tok;
        uint32_t op_type = get_ast_op_type(op_tok);
        
        char op_buf[32];
        snprintf(op_buf, sizeof(op_buf), "%s", yytext);
        advance();

        nu_ast_node_t* right = parse_expression_prec(prec + 1);

        nu_ast_node_t* op_node = newstrnode(NULL, op_type, op_buf);
        if (left)  nu_ast_add_child(op_node, left);
        if (right) nu_ast_add_child(op_node, right);

        left = op_node;
    }

    return left;
}

nu_ast_node_t* parse_expression(nu_ast_node_t* parent) {
    nu_ast_node_t* expr = parse_expression_prec(1);

    if (expr && parent) {
        nu_ast_add_child(parent, expr);
    }

    return expr;
}

void parse_return_stmt(nu_ast_node_t* root) {
    expect(RETURN, "Expected 'return'");

    nu_ast_node_t* ret_node = newnode(root, AST_RETURN_STMT);
    
    if (!match(';')) {
        parse_expression(ret_node);
    }
    
    expect(';', "Expected ';' after return value");
}

void parse_integer_decl(nu_ast_node_t* root) {
    expect(INT, "Expected 'int'");

    nu_ast_node_t* int_node = newnode(root, AST_INT_DECL);

    if (!match(IDENTIFIER)) {
        synerr(yylineno, tok_col, "Expected an identifier for the integer!");
    }

    char* varname = nu_strdup(yytext);
    newstrnode(int_node, AST_VAR_DECL, yytext);
    advance();

    symtab_add(SymTable, varname, VAR_INT);
    if (match('=')) {
        advance();
        parse_expression(int_node);
    }

    expect(';', "Expected ';' after declaration");
}

static int is_type_token(int token) {
    return token == INT || token == CHAR;
}

void parse_constvar_decl(nu_ast_node_t* root) {
    expect(CONST, "Expected 'const'");

    if (!is_type_token(current_tok)) {
        synerr(yylineno, tok_col, "Expected type after 'const'");
    }

    var_type_t var_type;
    if (current_tok == CHAR) {
        var_type = VAR_CHAR;
    } else if (current_tok == STRING_LITERAL) {
        var_type = VAR_STRING;
    } else {
        var_type = VAR_INT;
    }
    advance();
    
    nu_ast_node_t* const_node = newnode(root, AST_CONST_DECL);

    if (!match(IDENTIFIER)) {
        synerr(yylineno, tok_col, "Expected an identifier for the constant!");
    }

    char* varname = nu_strdup(yytext);
    newstrnode(const_node, AST_VAR_DECL, yytext);
    advance();

    symtab_add(SymTable, varname, var_type);
    
    if (match('=')) {
        advance();
        parse_expression(const_node);
    } else {
        synerr(yylineno, tok_col, "Constants must be initialized at declaration");
    }

    expect(';', "Expected ';' after declaration");
}

void parse_print_stmt(nu_ast_node_t* parent) {
    expect(PRINT, "Expected 'print'");
    expect('(', "Expected '(' after 'print'");

    if (match(STRING_LITERAL)) {
       nu_ast_node_t* print_node = newnode(parent, AST_PRINT_STMT);
       newstrnode(print_node, AST_CONST, yytext);
       advance();
    } else {
       nu_ast_node_t* print_node = newnode(parent, AST_PRINT_STMT);
       parse_expression(print_node);
    }

    expect(')', "Expected ')' after 'print' statement");
    expect(';', "Expected ';' after print statement");
}

void parse_printf_stmt(nu_ast_node_t* parent) {
    expect(PRINTF, "Expected 'printf'");
    expect('(', "Expected '(' after 'printf'");

    if (!match(STRING_LITERAL)) {
        synerr(yylineno, tok_col, "Expected format string in printf");
    }

    nu_ast_node_t* printf_node = newnode(parent, AST_PRINTF_STMT);
    newstrnode(printf_node, AST_CONST, yytext);
    advance();

    while (match(',')) {
        advance();
        parse_expression(printf_node);
    }

    expect(')', "Expected ')' after printf arguments");
    expect(';', "Expected ';' after printf statement");
}

void parse_block(nu_ast_node_t* parent);

static int parse_type_specifier(nu_ast_node_t* parent) {
    if (match(INT) || match(CHAR)) {
        newstrnode(parent, AST_TYPE_SPEC, yytext);
        advance();
        return 1;
    }
    return 0;
}

void parse_function_decl(nu_ast_node_t* root) {
    expect(FUNC, "Expected 'func'");

    char func_name[64];
    snprintf(func_name, sizeof(func_name), "%s", yytext);
    expect(IDENTIFIER, "Expected function name");

    nu_ast_node_t* fn_node = newstrnode(root, AST_FUNC_DECL, func_name);

    expect('(', "Expected '(' after function name");
    nu_ast_node_t* param_list = newnode(fn_node, AST_PARAM_LIST);

    while (!match(')') && current_tok != 0) {
	nu_ast_node_t* param_node = newnode(param_list, AST_PARAM);

	if (!parse_type_specifier(param_node)) {
            synerr(yylineno, tok_col, "Expected type for parameter");
        }

	if (match(IDENTIFIER)) {
            newstrnode(param_node, AST_PARAM_NAME, yytext);
            advance();
        } else {
            synerr(yylineno, tok_col, "Expected parameter name after type");
        }

        if (match(',')) advance();
    }
    expect(')', "Expected ')' after parameters");

    if (match(RARROW)) {
        advance();
        
        nu_ast_node_t* ret_type_node = newnode(fn_node, AST_FUNC_RETURN_TYPE);
        if (!parse_type_specifier(ret_type_node)) {
            synerr(yylineno, tok_col, "Expected return type after '->'");
        }
    }

    parse_block(fn_node);
}

void parse_char_decl(nu_ast_node_t* root) {
    expect(CHAR, "Expected 'char'");

    nu_ast_node_t* char_node = newnode(root, AST_CHAR_DECL);

    if (!match(IDENTIFIER)) {
        synerr(yylineno, tok_col, "Expected an identifier for the char!");
        return;
    }

    char* varname = nu_strdup(yytext);
    newstrnode(char_node, AST_VAR_DECL, yytext);
    advance();

    symtab_add(SymTable, varname, VAR_CHAR);
    nu_free(g_mm, varname);

    if (match('=')) {
        advance();
        parse_expression(char_node);
    }

    expect(';', "Expected ';' after declaration");
}

void parse_assignment_stmt(nu_ast_node_t* parent) {
    // Current token is IDENTIFIER
    char varname[64];
    snprintf(varname, sizeof(varname), "%s", yytext);
    advance();

    expect('=', "Expected '=' in assignment");

    // Create the assignment AST node
    nu_ast_node_t* assign_node = newnode(parent, AST_ASSIGN_STMT);
    
    // Add target variable node
    newstrnode(assign_node, AST_IDENT, varname);

    // Parse the RHS expression and attach to assignment node
    parse_expression(assign_node);

    expect(';', "Expected ';' after assignment");
}

static void parse_statement(nu_ast_node_t* root) {
    switch (current_tok) {
        case RETURN:
            parse_return_stmt(root);
            break;

        case INT:
            parse_integer_decl(root);
            break;

        case FUNC:
            parse_function_decl(root);
            break;

        case CONST:
            parse_constvar_decl(root);
            break;

        case PRINTF:
            parse_printf_stmt(root);
            break;

        case PRINT:
            parse_print_stmt(root);
            break;

        case CHAR:
            parse_char_decl(root);
            break;

        case IDENTIFIER: {
            char name[64];
            snprintf(name, sizeof(name), "%s", yytext);
            advance();

            if (match('=')) {
                advance();
                nu_ast_node_t* assign_node = newnode(root, AST_ASSIGN_STMT);
                newstrnode(assign_node, AST_IDENT, name);
                parse_expression(assign_node);
                expect(';', "Expected ';' after assignment");
            } else {
                parse_expression(root);
                expect(';', "Expected ';' after statement");
            }
            break;
    }
    
        default: {
            char errm[128];
            snprintf(errm, sizeof(errm),
                 "Unexpected token '%s' (\"%s\")\n", tokname(current_tok), yytext);
            synerr(yylineno, tok_col, errm);
            advance(); /* Skip token to prevent infinite loop */
            break;
	}
    }
}

void parse_block(nu_ast_node_t* parent) {
    expect('{', "Expected '{' to start block");
    nu_ast_node_t* block_node = newnode(parent, AST_BLOCK);

    while (!match('}') && current_tok != 0) {
        parse_statement(block_node);
    }

    expect('}', "Expected '}' at end of block");
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

void parse(const char* output_file) {
    advance(); /* prime the stream */
    nu_ast_node_t *root = newnode(NULL, AST_ROOT);
    g_ast->root = root;
    g_root_node = root;

    SymTable = nu_alloc(g_mm, sizeof(symbt));
    SymTable->head = NULL;

    while (current_tok != 0) {
        parse_statement(root);
    }

    print_ast(root, 0);

    if (!output_file) {
	err("Output File is NULL!");
    }

    const char ext[] = ".pawv";
    size_t len = strlen(output_file) + sizeof(ext);
    char *filename = nu_alloc(g_mm, len);

    snprintf(filename, len, "%s%s", output_file, ext);
    walk_ast_to_file(root, filename);
}
