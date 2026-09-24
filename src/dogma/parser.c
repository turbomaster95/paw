#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <comp.h>
#include <dogma.h>
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
extern nu_ast_t *g_ast;
extern nu_mm_t *g_mm;
extern char *current_filename;

static int current_tok;
extern int tok_col;

nu_ast_node_t *g_root_node = NULL;
static var_type_t g_current_func_ret_type = VAR_UNKNOWN;
static int g_loop_depth = 0;

symbt *SymTable;

nu_ast_node_t *parse_expression_prec(int min_prec);
nu_ast_node_t *parse_expression(nu_ast_node_t *parent);
static int is_type_token(int token);

static nu_ast_node_t *parse_primary(nu_ast_node_t *parent);
static nu_ast_node_t *parse_postfix(void);
static nu_ast_node_t *parse_identifier_expr(void);
static void parse_statement(nu_ast_node_t *root);

const char *tokname(int token) {
    switch (token) {
        case FUNC: return _("FUNC");
        case IF: return _("IF");
        case INT: return _("INT");
        case ELSE: return _("ELSE");
        case CHAR: return _("CHAR");
        case CONST: return _("CONST");
        case RETURN: return _("RETURN");
        case PRINT: return _("PRINT");
        case PRINTF: return _("PRINTF");
        case EXTERN: return _("EXTERN");
        case LIB: return _("LIB");
        case IDENTIFIER: return _("IDENTIFIER");
        case CONSTANT: return _("CONSTANT");
        case STRING_LITERAL: return _("STRING_LITERAL");
        case WHILE: return _("WHILE");
        case BREAK: return _("BREAK");
        case CONTINUE: return _("CONTINUE");
        default:
            if (token > 0 && token < 256) {
                static char buf[2] = {0};
                buf[0] = (char)token;
                return buf;
            }
            return _("UNKNOWN");
    }
}

void synerr(int line, int col, const char *msg) {
    glog_log(current_filename, line - 1, col, GLOG_ERROR, _("Syntax Error: %s"), msg);
    exit(EXIT_FAILURE);
}

void err(const char *msg) {
    glog_log(current_filename, 0, 0, GLOG_ERROR, _("Error: %s"), msg);
    exit(EXIT_FAILURE);
}

static void removequotes(const char *in, char *out, size_t out_size) {
    if (!out || out_size == 0) return;

    if (!in) {
        out[0] = '\0';
        return;
    }

    size_t len = strlen(in);

    if (len >= 2) {
        char first = in[0];
        char last = in[len - 1];

        if ((first == '\'' && last == '\'') ||
            (first == '"' && last == '"')) {
            size_t inner = len - 2;

            if (inner >= out_size) {
                inner = out_size - 1;
            }

            memcpy(out, in + 1, inner);
            out[inner] = '\0';
            return;
        }
    }

    strncpy(out, in, out_size - 1);
    out[out_size - 1] = '\0';
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
        return;
    }

    char errm[128];

    snprintf(
        errm,
        sizeof(errm),
        "%s (got %s: \"%s\")\n",
        msg,
        tokname(current_tok),
        yytext
    );

    synerr(yylineno, tok_col, errm);
}

static nu_ast_node_t *newnode(nu_ast_node_t *parent, uint32_t type) {
    nu_ast_node_t *node = nu_ast_new_node(g_ast, type);

    if (parent) {
        nu_ast_add_child(parent, node);
    }

    return node;
}

static nu_ast_node_t *newstrnode(nu_ast_node_t *parent, uint32_t type, const char *str) {
    if (!str) return parent;

    nu_ast_node_t *node = newnode(parent, type);
    char *strd = nu_strdup(str);

    if (strd) {
        nu_ast_set_str(g_ast, node, strd, strlen(str));
    }

    return node;
}

static nu_ast_node_t *parse_identifier_expr(void) {
    char name[64];

    snprintf(name, sizeof(name), "%s", yytext);
    advance();

    if (match('(')) {
        advance();

        nu_ast_node_t *call_node = newstrnode(NULL, AST_FUNC_CALL, name);

        while (!match(')') && current_tok != 0) {
            parse_expression(call_node);

            if (match(',')) {
                advance();
            } else if (!match(')')) {
                synerr(yylineno, tok_col, _("Expected ',' or ')' in function arguments"));
            }
        }

        expect(')', _("Expected ')' after function arguments"));
        return call_node;
    }

    if (match('.')) {
        advance();

        if (!match(IDENTIFIER)) {
            synerr(yylineno, tok_col, _("Expected library function name after '.'"));
        }

        char function_name[64];

        snprintf(function_name, sizeof(function_name), "%s", yytext);
        advance();

        if (!match('(')) {
            synerr(yylineno, tok_col, _("Expected '(' after library function name"));
        }

        advance();

        nu_ast_node_t *call_node = newstrnode(NULL, AST_FFI_CALL, function_name);

        newstrnode(call_node, AST_IDENT, name);

        while (!match(')') && current_tok != 0) {
            parse_expression(call_node);

            if (match(',')) {
                advance();
            } else if (!match(')')) {
                synerr(yylineno, tok_col, _("Expected ',' or ')' in library function arguments"));
            }
        }

        expect(')', _("Expected ')' after library function arguments"));
        return call_node;
    }

    return newstrnode(NULL, AST_IDENT, name);
}

static nu_ast_node_t *parse_primary(nu_ast_node_t *parent) {
    (void)parent;

    if (match('(')) {
        advance();

        nu_ast_node_t *expr = parse_expression_prec(1);

        expect(')', _("Expected ')' after parenthesized expression"));
        return expr;
    }

    if (match(CONSTANT)) {
        nu_ast_node_t *node = newstrnode(NULL, AST_CONST, yytext);
        advance();
        return node;
    }

    if (match(STRING_LITERAL)) {
        nu_ast_node_t *node = newstrnode(NULL, AST_CONST, yytext);
        advance();
        return node;
    }

    if (match(IDENTIFIER)) {
        return parse_identifier_expr();
    }

    synerr(yylineno, tok_col, _("Expected expression"));
    return NULL;
}

static nu_ast_node_t *parse_postfix(void) {
    nu_ast_node_t *left = parse_primary(NULL);

    while (match('[')) {
        advance();

        if (match(']')) {
            synerr(yylineno, tok_col, _("Expected array index expression"));
        }

        nu_ast_node_t *index = parse_expression_prec(1);

        expect(']', _("Expected ']' after array index"));

        nu_ast_node_t *index_node = newnode(NULL, AST_ARRAY_INDEX);

        nu_ast_add_child(index_node, left);
        nu_ast_add_child(index_node, index);

        left = index_node;
    }

    return left;
}

static var_type_t infer_node_type(nu_ast_node_t *node) {
    if (!node) return VAR_UNKNOWN;

    if (node->type == AST_CAST && node->val.str) {
        if (strcmp(node->val.str, "char") == 0) return VAR_CHAR;
        if (strcmp(node->val.str, "int") == 0) return VAR_INT;
    }

    if (node->type == AST_IDENT && node->val.str) {
        symb *sym = symtab_lookup(SymTable, node->val.str);

        if (sym) {
            return sym->type;
        }
    }

    if (node->type == AST_ARRAY_INDEX && node->first_child) {
        return infer_node_type(node->first_child);
    }

    if (node->type == AST_CONST && node->val.str) {
        if (node->val.str[0] == '\'') return VAR_CHAR;
        if (node->val.str[0] == '"') return VAR_STRING;
        return VAR_INT;
    }

    if (node->first_child) {
        return infer_node_type(node->first_child);
    }

    return VAR_UNKNOWN;
}

static int get_tok_precedence(int tok) {
    switch (tok) {
        case '|': return 1;
        case '^': return 2;
        case '&': return 3;
        case EQ:                    /* == */
        case NEQ: return 4;         /* != */
        case '<':
        case '>':
        case LEQ:                   /* <= */
        case GEQ: return 5;         /* >= */
        case SHL:
        case SHR: return 6;
        case '+':
        case '-': return 7;
        case '*':
        case '/':
        case '%': return 8;
        default: return 0;
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
        case SHL: return AST_SHL;
        case SHR: return AST_SHR;
        case '<': return AST_LT;
        case '>': return AST_GT;
        case LEQ: return AST_LEQ;
        case GEQ: return AST_GEQ;
        case EQ:  return AST_EQ;
        case NEQ: return AST_NEQ;
        default: return 0;
    }
}

nu_ast_node_t *parse_unary(void) {
    if (match('[')) {
        advance();

        if (!is_type_token(current_tok)) {
            synerr(yylineno, tok_col, _("Expected type specifier inside cast brackets '[' ']'"));
        }

        char type_str[32];

        snprintf(type_str, sizeof(type_str), "%s", yytext);
        advance();

        expect(']', _("Expected ']' after cast type specifier"));

        nu_ast_node_t *cast_node = newstrnode(NULL, AST_CAST, type_str);
        nu_ast_node_t *operand = parse_unary();

        if (operand) {
            nu_ast_add_child(cast_node, operand);
        }

        return cast_node;
    }

    if (current_tok == '-' || current_tok == '~' || current_tok == '!') {
        int op = current_tok;

        uint32_t op_type;

        if (op == '-') {
            op_type = AST_NEGATIVE;
        } else if (op == '~') {
            op_type = AST_BNOT;
        } else {
            op_type = AST_LNOT;
        }

        char op_str[2] = {(char)op, '\0'};

        nu_ast_node_t *op_node = newstrnode(NULL, op_type, op_str);
        advance();

        nu_ast_node_t *operand = parse_unary();

        if (operand) {
            nu_ast_add_child(op_node, operand);
        }

        return op_node;
    }

    return parse_postfix();
}

nu_ast_node_t *parse_expression_prec(int min_prec) {
    nu_ast_node_t *left = parse_unary();

    while (1) {
        int prec = get_tok_precedence(current_tok);

        if (prec < min_prec) {
            break;
        }

        int op_tok = current_tok;
        uint32_t op_type = get_ast_op_type(op_tok);

        char op_buf[32];

        snprintf(op_buf, sizeof(op_buf), "%s", yytext);
        advance();

        nu_ast_node_t *right = parse_expression_prec(prec + 1);

        nu_ast_node_t *op_node = newstrnode(NULL, op_type, op_buf);

        if (left) {
            nu_ast_add_child(op_node, left);
        }

        if (right) {
            nu_ast_add_child(op_node, right);
        }

        left = op_node;
    }

    return left;
}

nu_ast_node_t *parse_expression(nu_ast_node_t *parent) {
    nu_ast_node_t *expr = parse_expression_prec(1);

    if (expr && parent) {
        nu_ast_add_child(parent, expr);
    }

    return expr;
}

static size_t parse_array_size(void) {
    if (!match(CONSTANT)) {
        synerr(yylineno, tok_col, _("Expected constant array size"));
    }

    char *end = NULL;
    unsigned long long value = strtoull(yytext, &end, 0);

    if (value == 0 || end == yytext) {
        synerr(yylineno, tok_col, _("Array size must be a positive constant"));
    }

    if (value > (unsigned long long)SIZE_MAX) {
        synerr(yylineno, tok_col, _("Array size is too large"));
    }

    size_t size = (size_t)value;

    advance();
    return size;
}

static void parse_array_suffix(nu_ast_node_t *decl_node, symb *sym, int require_init) {
    expect('[', _("Expected '[' in array declaration"));

    size_t size = parse_array_size();

    expect(']', _("Expected ']' after array size"));

    nu_ast_node_t *size_node = newnode(decl_node, AST_ARRAY_SIZE);
    nu_ast_set_int(size_node, (int64_t)size);

    sym->is_array = 1;
    sym->array_size = size;
    sym->elem_size = (sym->type == VAR_CHAR) ? 1 : 4;

    if (match('=')) {
        advance();

        expect('{', _("Expected '{' before array initializer"));

        if (!match('}')) {
            while (1) {
                parse_expression(decl_node);

                if (match(',')) {
                    advance();

                    if (match('}')) {
                        break;
                    }

                    continue;
                }

                break;
            }
        }

        expect('}', _("Expected '}' after array initializer"));
    } else if (require_init) {
        synerr(yylineno, tok_col, _("Constant arrays must be initialized"));
    }

    expect(';', _("Expected ';' after array declaration"));
}

void parse_break_stmt(nu_ast_node_t *root) {
    if (g_loop_depth == 0) {
        synerr(yylineno, tok_col, _("'break' statement not within a loop"));
    }
    expect(BREAK, _("Expected 'break'"));
    newnode(root, AST_BREAK_STMT);
    expect(';', _("Expected ';' after 'break'"));
}

void parse_continue_stmt(nu_ast_node_t *root) {
    if (g_loop_depth == 0) {
        synerr(yylineno, tok_col, _("'continue' statement not within a loop"));
    }
    expect(CONTINUE, _("Expected 'continue'"));
    newnode(root, AST_CONTINUE_STMT);
    expect(';', _("Expected ';' after 'continue'"));
}

void parse_return_stmt(nu_ast_node_t *root) {
    expect(RETURN, _("Expected 'return'"));

    nu_ast_node_t *ret_node = newnode(root, AST_RETURN_STMT);

    if (!match(';')) {
        nu_ast_node_t *expr = parse_expression(ret_node);
        var_type_t expr_type = infer_node_type(expr);

        if (g_current_func_ret_type != VAR_UNKNOWN &&
            expr_type != VAR_UNKNOWN &&
            expr_type != g_current_func_ret_type) {
            synerr(yylineno, tok_col, _("Return type mismatch in function"));
        }
    } else if (g_current_func_ret_type != VAR_UNKNOWN) {
        synerr(yylineno, tok_col, _("Non-void function must return a value"));
    }

    expect(';', _("Expected ';' after return value"));
}

void parse_integer_decl(nu_ast_node_t *root) {
    expect(INT, _("Expected 'int'"));

    nu_ast_node_t *int_node = newnode(root, AST_INT_DECL);

    if (!match(IDENTIFIER)) {
        synerr(yylineno, tok_col, _("Expected an identifier for the integer"));
    }

    char *varname = nu_strdup(yytext);

    newstrnode(int_node, AST_VAR_DECL, yytext);
    advance();

    symb *sym = symtab_add(SymTable, varname, VAR_INT);

    nu_free(g_mm, varname);

    if (!sym) {
        err(_("Failed to add integer symbol"));
    }

    if (match('[')) {
        parse_array_suffix(int_node, sym, 0);
        return;
    }

    if (match('=')) {
        advance();
        parse_expression(int_node);
    }

    expect(';', _("Expected ';' after declaration"));
}

static int is_type_token(int token) {
    return token == INT || token == CHAR;
}

void parse_constvar_decl(nu_ast_node_t *root) {
    expect(CONST, _("Expected 'const'"));

    if (!is_type_token(current_tok)) {
        synerr(yylineno, tok_col, _("Expected type after 'const'"));
    }

    var_type_t var_type = (current_tok == CHAR) ? VAR_CHAR : VAR_INT;

    advance();

    nu_ast_node_t *const_node = newnode(root, AST_CONST_DECL);

    if (!match(IDENTIFIER)) {
        synerr(yylineno, tok_col, _("Expected an identifier for the constant"));
    }

    char *varname = nu_strdup(yytext);

    newstrnode(const_node, AST_VAR_DECL, yytext);
    advance();

    symb *sym = symtab_add(SymTable, varname, var_type);

    nu_free(g_mm, varname);

    if (!sym) {
        err(_("Failed to add constant symbol"));
    }

    sym->is_const = 1;

    if (match('[')) {
        parse_array_suffix(const_node, sym, 1);
        return;
    }

    if (match('=')) {
        advance();
        parse_expression(const_node);
    } else {
        synerr(yylineno, tok_col, _("Constants must be initialized at declaration"));
    }

    expect(';', _("Expected ';' after constant declaration"));
}

void parse_print_stmt(nu_ast_node_t *parent) {
    expect(PRINT, _("Expected 'print'"));
    expect('(', _("Expected '(' after 'print'"));

    nu_ast_node_t *print_node = newnode(parent, AST_PRINT_STMT);

    parse_expression(print_node);

    expect(')', _("Expected ')' after 'print' statement"));
    expect(';', _("Expected ';' after 'print' statement"));
}

void parse_printf_stmt(nu_ast_node_t *parent) {
    expect(PRINTF, _("Expected 'printf'"));
    expect('(', _("Expected '(' after 'printf'"));

    if (!match(STRING_LITERAL)) {
        synerr(yylineno, tok_col, _("Expected format string in printf"));
    }

    nu_ast_node_t *printf_node = newnode(parent, AST_PRINTF_STMT);

    newstrnode(printf_node, AST_CONST, yytext);
    advance();

    while (match(',')) {
        advance();
        parse_expression(printf_node);
    }

    expect(')', _("Expected ')' after printf arguments"));
    expect(';', _("Expected ';' after printf"));
}

void parse_block(nu_ast_node_t *parent);

static int parse_type_specifier(nu_ast_node_t *parent) {
    if (match(INT) || match(CHAR)) {
        newstrnode(parent, AST_TYPE_SPEC, yytext);
        advance();
        return 1;
    }

    return 0;
}

void parse_function_decl(nu_ast_node_t *root) {
    expect(FUNC, _("Expected 'func'"));

    char func_name[64];

    snprintf(func_name, sizeof(func_name), "%s", yytext);
    expect(IDENTIFIER, _("Expected function name"));

    nu_ast_node_t *fn_node = newstrnode(root, AST_FUNC_DECL, func_name);

    expect('(', _("Expected '(' after function name"));

    nu_ast_node_t *param_list = newnode(fn_node, AST_PARAM_LIST);

    while (!match(')') && current_tok != 0) {
        nu_ast_node_t *param_node = newnode(param_list, AST_PARAM);

        if (!parse_type_specifier(param_node)) {
            synerr(yylineno, tok_col, _("Expected type for parameter"));
        }

        if (!match(IDENTIFIER)) {
            synerr(yylineno, tok_col, _("Expected parameter name after type"));
        }

        newstrnode(param_node, AST_PARAM_NAME, yytext);
        advance();

        if (match('[')) {
            synerr(yylineno, tok_col, _("Array parameters are not supported yet"));
        }

        if (match(',')) {
            advance();
        }
    }

    expect(')', _("Expected ')' after parameters"));

    var_type_t prev_ret_type = g_current_func_ret_type;

    g_current_func_ret_type = VAR_UNKNOWN;

    if (match(RARROW)) {
        advance();

        if (current_tok == INT) {
            g_current_func_ret_type = VAR_INT;
        } else if (current_tok == CHAR) {
            g_current_func_ret_type = VAR_CHAR;
        }

        nu_ast_node_t *ret_type_node = newnode(fn_node, AST_FUNC_RETURN_TYPE);

        if (!parse_type_specifier(ret_type_node)) {
            synerr(yylineno, tok_col, _("Expected return type after '->'"));
        }
    }

    parse_block(fn_node);
    g_current_func_ret_type = prev_ret_type;
}

void parse_char_decl(nu_ast_node_t *root) {
    expect(CHAR, _("Expected 'char'"));

    nu_ast_node_t *char_node = newnode(root, AST_CHAR_DECL);

    if (!match(IDENTIFIER)) {
        synerr(yylineno, tok_col, _("Expected an identifier for the char"));
    }

    char *varname = nu_strdup(yytext);

    newstrnode(char_node, AST_VAR_DECL, yytext);
    advance();

    symb *sym = symtab_add(SymTable, varname, VAR_CHAR);

    nu_free(g_mm, varname);

    if (!sym) {
        err(_("Failed to add char symbol"));
    }

    if (match('[')) {
        parse_array_suffix(char_node, sym, 0);
        return;
    }

    if (match('=')) {
        advance();
        parse_expression(char_node);
    }

    expect(';', _("Expected ';' after declaration"));
}

void parse_assignment_stmt(nu_ast_node_t *parent) {
    nu_ast_node_t *lhs = parse_expression_prec(1);

    if (!lhs) {
        synerr(yylineno, tok_col, _("Expected assignment target"));
    }

    if (lhs->type != AST_IDENT && lhs->type != AST_ARRAY_INDEX) {
        synerr(yylineno, tok_col, _("Invalid assignment target"));
    }

    expect('=', _("Expected '=' in assignment"));

    nu_ast_node_t *assign_node = newnode(parent, AST_ASSIGN_STMT);

    nu_ast_add_child(assign_node, lhs);
    parse_expression(assign_node);

    expect(';', _("Expected ';' after assignment"));
}

void parse_lib_decl(nu_ast_node_t *parent) {
    expect(LIB, _("Expected 'lib'"));
    expect('(', _("Expected '(' after 'lib'"));

    if (!match(STRING_LITERAL)) {
        synerr(yylineno, tok_col, _("Expected library path or library name"));
    }

    char library[1024];
    char clean[1024];

    removequotes(yytext, clean, sizeof(clean));
    snprintf(library, sizeof(library), "%s", clean);

    printf(_("The libname is this: %s\n"), clean);

    newstrnode(parent, AST_LIB_DECL, library);

    advance();

    expect(')', _("Expected ')' after library path"));
}

void parse_extern_decl(nu_ast_node_t *root) {
    expect(EXTERN, _("Expected 'extern'"));

    if (!match(IDENTIFIER)) {
        synerr(yylineno, tok_col, _("Expected library alias after 'extern'"));
    }

    char alias[64];

    snprintf(alias, sizeof(alias), "%s", yytext);
    advance();

    expect('=', _("Expected '=' after extern library alias"));

    if (!match(LIB)) {
        synerr(yylineno, tok_col, _("Expected lib(...) after extern alias"));
    }

    nu_ast_node_t *extern_node = newstrnode(root, AST_EXTERN_DECL, alias);

    parse_lib_decl(extern_node);

    expect(';', _("Expected ';' after extern library declaration"));

    symb *existing = symtab_lookup(SymTable, alias);

    if (existing) {
        synerr(yylineno, tok_col, _("Duplicate library alias"));
    }

    symb *sym = symtab_add(SymTable, alias, VAR_STRING);

    if (!sym) {
        err(_("Failed to register library alias"));
    }

    sym->is_ffi = 1;

    nu_ast_node_t *lib_node = extern_node->first_child;

    if (lib_node && lib_node->val.str) {
        sym->ffi_library = nu_strdup(lib_node->val.str);
    }
}

void parse_while_stmt(nu_ast_node_t *root) {
    expect(WHILE, _("Expected 'while'"));
    expect('(', _("Expected '(' after 'while'"));

    nu_ast_node_t *while_node = newnode(root, AST_WHILE_STMT);

    parse_expression(while_node);

    expect(')', _("Expected ')' after condition"));

    g_loop_depth++;
    parse_block(while_node);
    g_loop_depth--;
}

void parse_if_stmt(nu_ast_node_t *root) {
    expect(IF, _("Expected 'if'"));
    expect('(', _("Expected '(' after 'if'"));

    nu_ast_node_t *if_node = newnode(root, AST_IF_STMT);

    parse_expression(if_node);

    expect(')', _("Expected ')' after condition"));

    if (match('{')) {
        parse_block(if_node);
    } else {
        parse_statement(if_node);
    }

    if (match(ELSE)) {
        advance();
        if (match('{')) {
            parse_block(if_node);
        } else if (match(IF)) {
            parse_if_stmt(if_node);
        } else {
            parse_statement(if_node);
        }
    }
}

static void parse_statement(nu_ast_node_t *root) {
    switch (current_tok) {
        case EXTERN:
            parse_extern_decl(root);
            break;

        case RETURN:
            parse_return_stmt(root);
            break;

        case INT:
            parse_integer_decl(root);
            break;

        case FUNC:
            parse_function_decl(root);
            break;

        case WHILE:
            parse_while_stmt(root);
            break;

	case BREAK:
            parse_break_stmt(root);
            break;

        case CONTINUE:
            parse_continue_stmt(root);
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

	case IF:
            parse_if_stmt(root);
            break;

        case IDENTIFIER: {
            nu_ast_node_t *expr = parse_expression_prec(1);

            if (match('=')) {
                if (expr->type != AST_IDENT &&
                    expr->type != AST_ARRAY_INDEX) {
                    synerr(yylineno, tok_col, _("Invalid assignment target"));
                }

                advance();

                nu_ast_node_t *assign_node = newnode(root, AST_ASSIGN_STMT);

                nu_ast_add_child(assign_node, expr);
                parse_expression(assign_node);

                expect(';', _("Expected ';' after assignment"));
            } else {
                if (expr) {
                    nu_ast_add_child(root, expr);
                }

                expect(';', _("Expected ';' after statement"));
            }

            break;
        }

        default: {
            char errm[128];

            snprintf(
                errm,
                sizeof(errm),
                _("Unexpected token '%s' (\"%s\")\n"),
                tokname(current_tok),
                yytext
            );

            synerr(yylineno, tok_col, errm);
            advance();
            break;
        }
    }
}

void parse_block(nu_ast_node_t *parent) {
    expect('{', _("Expected '{' to start block"));

    nu_ast_node_t *block_node = newnode(parent, AST_BLOCK);

    while (!match('}') && current_tok != 0) {
        parse_statement(block_node);
    }

    expect('}', _("Expected '}' at end of block"));
}

void dogma_print_ast(nu_ast_node_t *node, int depth) {
    if (!node) return;

    for (int i = 0; i < depth; i++) {
        printf("  ");
    }

    printf("- [%s]", ast_type_name(node->type));

    if (node->type == AST_ARRAY_SIZE) {
        printf(" %ld", (long)node->val.i64);
    } else if (node->val.str) {
        printf(" \"%s\"", node->val.str);
    } else if (node->val.i64) {
        printf(" %ld", (long)node->val.i64);
    }

    printf("\n");

    for (nu_ast_node_t *child = node->first_child;
         child != NULL;
         child = child->next_sibling) {
        dogma_print_ast(child, depth + 1);
    }
}

dogma_status_t dogma_parse(const char *output_file) {
    if (!output_file) {
        err(_("Output File is NULL!"));
        return DOGMA_ERR_IO;
    }

    advance();

    nu_ast_node_t *root = newnode(NULL, AST_ROOT);

    g_ast->root = root;
    g_root_node = root;

    SymTable = nu_alloc(g_mm, sizeof(symbt));

    if (!SymTable) {
        err(_("Failed to allocate symbol table"));
    }

    SymTable->head = NULL;

    while (current_tok != 0) {
        parse_statement(root);
    }

    dogma_print_ast(root, 0);

    const char ext[] = ".pawv";
    size_t len = strlen(output_file) + sizeof(ext);

    char *filename = nu_alloc(g_mm, len);

    if (!filename) {
        err(_("Failed to allocate output filename"));
    }

    snprintf(filename, len, "%s%s", output_file, ext);

    dogma_ast2file(root, filename);

    nu_free(g_mm, filename);

    return DOGMA_OK;
}
