#pragma once

#include <vector>
#include <array>
#include <iostream>
#include <string>
#define e 2.7182818
#define pi 3.141592

#define Expression std::vector<Stack>

// Used to classify the raw string that was input
typedef enum {
    TOKEN_NUMBER,
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_SLASH,
    TOKEN_ARROW,
    TOKEN_PERCENT,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_ABS,
    TOKEN_LN,
    TOKEN_LOG,
    TOKEN_SQRT,
    TOKEN_SIN,
    TOKEN_COS,
    TOKEN_TAN,
    TOKEN_ASIN,
    TOKEN_ACOS,
    TOKEN_ATAN,
    TOKEN_FAC,
    TOKEN_FLOOR,
    TOKEN_VAR,
    TOKEN_END
} TokenType;

// Used to classify operations
enum Operations {
    OP_NEG, OP_ABS, OP_SQRT, OP_LN, OP_LOG, 
    OP_SIN, OP_COS, OP_TAN, OP_ASIN, OP_ACOS, OP_ATAN, OP_FLOOR, OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_POW, OP_MOD
};

typedef struct {
    TokenType type;
    double value;  // Only used for numbers
    char var;      // Only used for variables
} Token;

struct Stack {
    // EXPR_NUMBER -> just a number on the stack, EXPR_VAR -> a variable on the stack, EXPR_UNARY -> pop 1 object off the stack
    // EXPR_BINARY -> pop 2 objects off the stack
    enum { EXPR_NUMBER, EXPR_BINARY, EXPR_VAR, EXPR_UNARY } kind;
    double number;
    char var;
    Operations op;
};

typedef struct Expr {
    enum { EXPR_NUMBER, EXPR_BINARY, EXPR_VAR, EXPR_UNARY } kind;
    union {
        double number;
        char var;
        struct {
            Operations op;        // '+' or '-' or functions like sin cos
            struct Expr* rhs;
        } unary;
        struct {
            Operations op;
            struct Expr* left;
            struct Expr* right;
        } binary;
    };
} Expr;

// Accept an input and parse it 
Expression parseInput(std::string input);

// Evaluate the input and return a number
double eval(const Expression& exprStack);

// Variable functions
void assignValue(unsigned char variable, double value);
void setToConstant(unsigned char variable);
void setToVariable(unsigned char variable);
void resetVariables();