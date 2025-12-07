/*
HOW TO USE: 
-parseInput: pass the string into this function, and it will return an expression that is easy to evaluate by the computer
-eval: Evaluates an expression. For this to work the expression has to be already compiled. Additionally you can pass variables 
using the assignVariables() function before you call eval(), and it will evaluate the function with those values.
-assignValue: A function to assign a value to some variable. Note that each variable must be only 1 character long.
-resetVariables: sets every slot in the variables table to 0 (essentially resetting every variable)

Usable operations:
binary (two arguements): +; -; *; /; ^; %;
unary (single arguement): abs(); ln(); log(); sqrt(); sin(); cos(); tan(); asin(); acos(); atan(); fac(); floor;

Constants (provided by this library): 
-e (2.7182818)
-pi (3.141592)

HOW IT WORKS: 
It uses Pratt parsing to evaluate the function at first and it creates a tree. After that, I convert this tree to a postfix notation, 
because it is much more efficient to compute that way. Why don't I compile straight into the postfix notation? Because I made this 
library like 3 months ago, and now that I'm updating it to make it work better, I didn't want to just start from scratch. I could have 
used the shunting-yard algorithm, but this is how it turned out. It might be a little slower, but you only need to compile a string 
once, so I wasn't really concerned with optimising the compilation.

For the variables, it uses an array instead of a hashtable, but it still works like a hashtable. You have to index into this array 
using the variable name itself, eg. vars['a'] to get back the actual variables value. This is much faster than a hashtable, and 256 
slots can store all of the English Alphabets characters.

It also can fold constants, but only for simple cases, where the two constants appear right beside eachother on the stack. Constant 
variables are folded by looking up the current value of the variable in the hashtable, which should have been set before it was made into 
a constant variable, althought my parser doesn't really check if the variable really is constant. 

The cases where it can fold constants: x * (2 + 3) -> x * 5; 2 * 3 + 4 -> 10; a^2 + x (where const a = 5) -> 25 + x
The cases where it can't fold them: 2 * x * 4; x - 2 + 4; a - x + 2 (where const a = 5)

Why it cant fold them in these cases? Because the expressions are essentially parsed as 2 * (x * 4), and I would need to open up 
the parenthesis in order to fold the constants, which would require me to implement algebraic manipulation into my parser, which I 
didn't bother with.
*/


#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include "expression.hpp"
#include "time.hpp"

#define tableSize 256
// Keep a global hashtable for the variables, and also for constant variables (so that we can fold constants)
double vars[tableSize] = {0};
bool constants[tableSize] = {0};

static void resize(void** arr, int size, int elements_size){
    void* new_arr = realloc(*arr, size * elements_size);

    if (!new_arr){
        printf("malloc failed\n");
        return;
    }

    *arr = new_arr;
}

static void removeSpaces(std::string& str) {
    int i, j = 0;
    for (i = 0; str[i] != '\0'; i++) {
        if (str[i] != ' ') {
            str[j++] = str[i];
        }
    }
    str[j] = '\0';  // Null-terminate the modified string
}

// Check if the input string starting from index has the string str
static bool checkInput(const char* input, const std::string& str, int index){
    int len = strlen(input);

    // Invalid index
    if (index >= len){
        return false;
    }

    int i = 0;
    while (input[index]){
        if (!str[i]){
            break;
        }

        if (str[i] != input[index]){
            return false;
        }
        i++; index++;
    }

    // Check if we didn't reach the end of the str string
    return !str[i];
}

Token* tokens = NULL;
int token_count = 0; int token_size = 100;
int current = 0;

static void tokenize(const char* input){
    // Go until the end of the input
    int i = 0;
    while (input[i]){
        if (isdigit(input[i])){
            char* end;
            tokens[token_count].type = TOKEN_NUMBER;
            tokens[token_count].value = strtod(&input[i], &end);     // Read until there are no other digits
            i = end - input;                // Step I forward to the next non-digit
            token_count++;

            if (token_count >= token_size){
                token_size *= 2;
                resize((void**)&tokens, token_size, sizeof(Token));
            }
        }
        else {
            switch (input[i]){
                case '*': tokens[token_count++].type = TOKEN_STAR; break;
                case '/': tokens[token_count++].type = TOKEN_SLASH; break;
                case '+': tokens[token_count++].type = TOKEN_PLUS; break;
                case '-': tokens[token_count++].type = TOKEN_MINUS; break;
                case '^': tokens[token_count++].type = TOKEN_ARROW; break;
                case '%': tokens[token_count++].type = TOKEN_PERCENT; break;
                case '(': tokens[token_count++].type = TOKEN_LPAREN; break;
                case ')': tokens[token_count++].type = TOKEN_RPAREN; break;
                default:
                    // Check for other things
                    if (checkInput(input, "e", i)){
                        tokens[token_count].value = e;
                        tokens[token_count++].type = TOKEN_NUMBER;
                    }
                    else if (checkInput(input, "pi", i)){
                        tokens[token_count].value = pi;
                        tokens[token_count++].type = TOKEN_NUMBER;

                        i += 1;
                    }
                    else if (checkInput(input, "abs", i)){
                        tokens[token_count++].type = TOKEN_ABS;

                        i += 2;
                    }
                    else if (checkInput(input, "sqrt", i)){
                        tokens[token_count++].type = TOKEN_SQRT;

                        i += 3;
                    }
                    else if (checkInput(input, "ln", i)){
                        tokens[token_count++].type = TOKEN_LN;

                        i += 1;
                    }
                    else if (checkInput(input, "log", i)){
                        tokens[token_count++].type = TOKEN_LOG;

                        i += 2;
                    }
                    else if (checkInput(input, "sin", i)){
                        tokens[token_count++].type = TOKEN_SIN;

                        i += 2;
                    }
                    else if (checkInput(input, "cos", i)){
                        tokens[token_count++].type = TOKEN_COS;

                        i += 2;
                    }
                    else if (checkInput(input, "tan", i)){
                        tokens[token_count++].type = TOKEN_TAN;

                        i += 2;
                    }
                    else if (checkInput(input, "asin", i)){
                        tokens[token_count++].type = TOKEN_ASIN;

                        i += 3;
                    }
                    else if (checkInput(input, "acos", i)){
                        tokens[token_count++].type = TOKEN_ACOS;

                        i += 3;
                    }
                    else if (checkInput(input, "atan", i)){
                        tokens[token_count++].type = TOKEN_ATAN;

                        i += 3;
                    }
                    else if (checkInput(input, "floor", i)){
                        tokens[token_count++].type = TOKEN_FLOOR;

                        i += 4;
                    }
                    else {
                        // We found a variable, assume its one character
                        tokens[token_count].type = TOKEN_VAR;
                        tokens[token_count++].var = input[i];
                    }
            }

            if (token_count >= token_size){
                token_size *= 2;
                resize((void**)&tokens, token_size, sizeof(Token));
            }

            i++;
        }
    }

    tokens[token_count++].type = TOKEN_END;

    if (token_count >= token_size){
        token_size *= 2;
        resize((void**)&tokens, token_size, sizeof(Token));
    }
}

static Token peek(){
    return tokens[current];
}

static Token advance(){
    return tokens[current++];
}

// Silly forward declaration
static Expr* parse_expression();

static Expr* parse_binary(int min_pre);

static Expr* parseFunction(Operations operation){
    // Parse the equation inside the parenthesis

    if (advance().type != TOKEN_LPAREN){
        //printf("Expected a parenthesis after function\n");
        return NULL;
    }

    Expr* rhs = parse_binary(1);

    if (advance().type != TOKEN_RPAREN){
        //printf("Expected closing parenthesis!\n");
        return NULL;
    }
    if (!rhs) return NULL;

    Expr* node = (Expr*)malloc(sizeof(Expr));
    node->kind = Expr::EXPR_UNARY;
    node->unary.op = operation;
    node->unary.rhs = rhs;
    return node;
}

static Expr* parse_primary() {
    Token tok = advance();
    if (tok.type == TOKEN_NUMBER){
        Expr* expr = (Expr*)malloc(sizeof(Expr));
        expr->kind = Expr::EXPR_NUMBER;
        expr->number = tok.value;
        return expr;
    }
    else if (tok.type == TOKEN_MINUS) {
        // Parse operand with higher binding power than * or +
        Expr* rhs = parse_binary(3);
        if (!rhs) return NULL;

        Expr* node = (Expr*)malloc(sizeof(Expr));
        node->kind = Expr::EXPR_UNARY;
        node->unary.op = OP_NEG;
        node->unary.rhs = rhs;
        return node;
    }
    else if (tok.type == TOKEN_SQRT){
        return parseFunction(OP_SQRT);
    }
    else if (tok.type == TOKEN_ABS){
        return parseFunction(OP_ABS);
    }
    else if (tok.type == TOKEN_LN){
        return parseFunction(OP_LN);
    }
    else if (tok.type == TOKEN_LOG){
        return parseFunction(OP_LOG);
    }
    else if (tok.type == TOKEN_SIN){
        return parseFunction(OP_SIN);
    }
    else if (tok.type == TOKEN_COS){
        return parseFunction(OP_COS);
    }
    else if (tok.type == TOKEN_TAN){
        return parseFunction (OP_TAN);
    }
    else if (tok.type == TOKEN_ASIN){
        return parseFunction(OP_ASIN);
    }
    else if (tok.type == TOKEN_ACOS){
        return parseFunction(OP_ACOS);
    }
    else if (tok.type == TOKEN_ATAN){
        return parseFunction(OP_ATAN);
    }
    else if (tok.type == TOKEN_FLOOR){
        return parseFunction(OP_FLOOR);
    }
    else if (tok.type == TOKEN_LPAREN){
        Expr* expr = parse_expression();

        if (!expr){
            return NULL;
        }
        if (advance().type != TOKEN_RPAREN) {
            //printf("Expected closing parenthesis\n");
            return NULL;
        }
        return expr;
    }
    else if (tok.type == TOKEN_VAR){
        Expr* expr = (Expr*)malloc(sizeof(Expr));
        expr->kind = Expr::EXPR_VAR;
        expr->var = tok.var;
        return expr;
    }
    else {
        //printf("Unexpected token in primary\n");
        return NULL;
    }
}

static int get_precedence(TokenType type) {
    switch (type) {
        case TOKEN_PLUS:
        case TOKEN_MINUS: return 1;
        case TOKEN_PERCENT:
        case TOKEN_STAR:
        case TOKEN_SLASH: return 2;
        case TOKEN_ARROW: return 3;
        default: return 0;
    }
}

static Expr* parse_binary(int min_prec) {
    Expr* left = parse_primary();

    if (!left){
        return NULL;
    }

    while (1) {
        TokenType op_type = peek().type;
        int prec = get_precedence(op_type);
        if (prec < min_prec) break;

        advance();  // consume operator
        Expr* right = parse_binary(prec + 1);

        if (!right){
            return NULL;
        }

        Expr* new_expr = (Expr*)malloc(sizeof(Expr));
        new_expr->kind = Expr::EXPR_BINARY;
        new_expr->binary.op = (op_type == TOKEN_PLUS) ? OP_ADD:
                              (op_type == TOKEN_MINUS) ? OP_SUB:
                              (op_type == TOKEN_STAR) ? OP_MUL:
                              (op_type == TOKEN_SLASH) ? OP_DIV: 
                              (op_type == TOKEN_ARROW) ? OP_POW : OP_MOD;
                              
        new_expr->binary.left = left;
        new_expr->binary.right = right;
        left = new_expr;
    }

    return left;
}

static Expr* parse_expression() {
    return parse_binary(1);
}

void printOp(Operations op){
    switch (op) {
        case OP_NEG:  std::cout << "neg"; break;
        case OP_ABS:  std::cout << "abs"; break;
        case OP_SQRT: std::cout << "sqrt"; break;
        case OP_LN:   std::cout << "ln"; break;
        case OP_LOG:  std::cout << "log"; break;
        case OP_SIN:  std::cout << "sin"; break;
        case OP_COS:  std::cout << "cos"; break;
        case OP_TAN:  std::cout << "tan"; break;
        case OP_ASIN: std::cout << "asin"; break;
        case OP_ACOS: std::cout << "acos"; break;
        case OP_ATAN: std::cout << "atan"; break;
        case OP_FLOOR: std::cout << "floor"; break;
        case OP_ADD: std::cout << "+"; break;
        case OP_SUB: std::cout << "-"; break;
        case OP_MUL: std::cout << "*"; break;
        case OP_DIV: std::cout << "/"; break;
        case OP_POW: std::cout << "^"; break;
        case OP_MOD: std::cout << "%"; break;
    }
}

// Hashtable needs to contain the variable - value pairs
// double eval(Expr* expr){
//     if (expr->kind == Expr::EXPR_NUMBER) {
//         return expr->number;
//     } 
//     else if (expr->kind == Expr::EXPR_VAR){
//         return vars[(unsigned char)expr->var];
//     }
//     else if (expr->kind == Expr::EXPR_UNARY){
//         double value = eval(expr->unary.rhs);

//         switch (expr->unary.op) {
//             case OP_NEG:  return -value;
//             case OP_ABS:  return fabs(value);
//             case OP_SQRT: return sqrt(value);
//             case OP_LN:   return log(value);
//             case OP_LOG:  return log10(value);
//             case OP_SIN:  return sin(value);
//             case OP_COS:  return cos(value);
//             case OP_TAN:  return tan(value);
//             case OP_ASIN: return asin(value);
//             case OP_ACOS: return acos(value);
//             case OP_ATAN: return atan(value);
//             case OP_FAC:  return tgamma(value + 1);
//         }
//     }
//     else {
//         double left = eval(expr->binary.left);
//         double right = eval(expr->binary.right);
//         switch (expr->binary.op) {
//             case OP_ADD: return left + right;
//             case OP_SUB: return left - right;
//             case OP_MUL: return left * right;
//             case OP_DIV: return left / right;
//             case OP_POW: return pow(left, right);
//             default:
//                 //printf("Unknown operator\n");
//                 return 0;
//         }
//     }

//     return 0;
// }

double eval(const Expression& exprStack) {
    // Preallocated small fixed-size stack
    double stack[100];  
    int sp = 0;  // Stack pointer

    for (const auto& elem : exprStack) {
        switch (elem.kind) {
            case Stack::EXPR_NUMBER:
                stack[sp++] = elem.number;
                break;

            case Stack::EXPR_VAR:
                stack[sp++] = vars[(unsigned char)elem.var];
                break;

            case Stack::EXPR_UNARY: {
                double val = stack[sp - 1];
                switch (elem.op) {
                    case OP_NEG:  stack[sp - 1] = -val; break;
                    case OP_ABS:  stack[sp - 1] = fabs(val); break;
                    case OP_SQRT: stack[sp - 1] = sqrt(val); break;
                    case OP_LN:   stack[sp - 1] = log(val); break;
                    case OP_LOG:  stack[sp - 1] = log10(val); break;
                    case OP_SIN:  stack[sp - 1] = sin(val); break;
                    case OP_COS:  stack[sp - 1] = cos(val); break;
                    case OP_TAN:  stack[sp - 1] = tan(val); break;
                    case OP_ASIN: stack[sp - 1] = asin(val); break;
                    case OP_ACOS: stack[sp - 1] = acos(val); break;
                    case OP_ATAN: stack[sp - 1] = atan(val); break;
                    case OP_FLOOR: stack[sp - 1] = floor(val); break;
                }
                break;
            }

            case Stack::EXPR_BINARY: {
                double rhs = stack[sp - 1];
                double lhs = stack[sp - 2];

                // Replace pow for common integer exponents
                if (elem.op == OP_POW) {
                    if (rhs == 2.0) lhs = lhs * lhs;
                    else if (rhs == 3.0) lhs = lhs * lhs * lhs;
                    else lhs = std::pow(lhs, rhs);
                } else {
                    switch (elem.op) {
                        case OP_ADD: lhs += rhs; break;
                        case OP_SUB: lhs -= rhs; break;
                        case OP_MUL: lhs *= rhs; break;
                        case OP_DIV: lhs /= rhs; break;
                        case OP_MOD: lhs = fmod(lhs, rhs); break;
                    }
                }

                sp--;  // Pop rhs
                stack[sp - 1] = lhs; // Store result
                break;
            }
        }
    }

    return stack[0]; // Result
}

void treeToPostfix(Expr* expression, Expression& exprStack){
    if (!expression) return;  // Ignore nulls
    
    if (expression->kind == Expr::EXPR_NUMBER){
        Stack elem;
        elem.kind = Stack::EXPR_NUMBER;
        elem.number = expression->number;

        exprStack.push_back(elem);
    }
    else if (expression->kind == Expr::EXPR_VAR){
        Stack elem;
        elem.kind = Stack::EXPR_VAR;
        elem.var = expression->var;

        exprStack.push_back(elem);
    }
    else if (expression->kind == Expr::EXPR_UNARY){
        treeToPostfix(expression->unary.rhs, exprStack);

        Stack elem;
        elem.kind = Stack::EXPR_UNARY;
        elem.op = expression->unary.op;

        exprStack.push_back(elem);
    }
    else if (expression->kind == Expr::EXPR_BINARY){
        treeToPostfix(expression->binary.left, exprStack);
        treeToPostfix(expression->binary.right, exprStack);

        Stack elem;
        elem.kind = Stack::EXPR_BINARY;
        elem.op = expression->binary.op;

        exprStack.push_back(elem);
    }
}

void foldConstants(Expression& exprStack){
    Expression stack;
    stack.resize(100);
    int sp = 0;

    // Traverse the stack, and for each op we see, if the top 2 (or 1) elements of the stack are constants, fold them
    for (const auto& elem : exprStack){
        switch (elem.kind){
            case Stack::EXPR_NUMBER: {
                stack[sp++] = elem;
                break;
            }
            case Stack::EXPR_VAR: {
                Stack newElem;

                // If the currently variable is a constant one, then just replace it with a number
                if (constants[elem.var]){
                    newElem.kind = Stack::EXPR_NUMBER;
                    newElem.number = vars[elem.var];
                }
                else {
                    newElem = elem;
                }

                newElem.op = (Operations)0;

                stack[sp++] = newElem;
                break;
            }
            case Stack::EXPR_UNARY: {
                // If the top element is a constant, then execute this operation
                if (stack[sp - 1].kind == Stack::EXPR_NUMBER){
                    double val = stack[sp - 1].number;
                    switch (elem.op) {
                        case OP_NEG:  stack[sp - 1].number = -val; break;
                        case OP_ABS:  stack[sp - 1].number = fabs(val); break;
                        case OP_SQRT: stack[sp - 1].number = sqrt(val); break;
                        case OP_LN:   stack[sp - 1].number = log(val); break;
                        case OP_LOG:  stack[sp - 1].number = log10(val); break;
                        case OP_SIN:  stack[sp - 1].number = sin(val); break;
                        case OP_COS:  stack[sp - 1].number = cos(val); break;
                        case OP_TAN:  stack[sp - 1].number = tan(val); break;
                        case OP_ASIN: stack[sp - 1].number = asin(val); break;
                        case OP_ACOS: stack[sp - 1].number = acos(val); break;
                        case OP_ATAN: stack[sp - 1].number = atan(val); break;
                        case OP_FLOOR: stack[sp - 1].number = floor(val); break;
                    }
                }
                // Otherwise just add the operator to the end of the stack
                else {
                    stack[sp++] = elem;
                }
                break;
            }
            case Stack::EXPR_BINARY: {
                if (stack[sp-1].kind == Stack::EXPR_NUMBER && stack[sp-2].kind == Stack::EXPR_NUMBER){
                    double rhs = stack[sp - 1].number;
                    double lhs = stack[sp - 2].number;

                    // Replace pow for common integer exponents
                    if (elem.op == OP_POW) {
                        if (rhs == 2.0) lhs = lhs * lhs;
                        else if (rhs == 3.0) lhs = lhs * lhs * lhs;
                        else lhs = std::pow(lhs, rhs);
                    } else {
                        switch (elem.op) {
                            case OP_ADD: lhs += rhs; break;
                            case OP_SUB: lhs -= rhs; break;
                            case OP_MUL: lhs *= rhs; break;
                            case OP_DIV: lhs /= rhs; break;
                            case OP_MOD: lhs = fmod(lhs, rhs); break;
                        }
                    }

                    sp--;  // Pop rhs

                    Stack newElem;
                    newElem.kind = Stack::EXPR_NUMBER;
                    newElem.number = lhs;

                    stack[sp - 1] = newElem; // Store result
                }
                // Just add the operation
                else {
                    stack[sp++] = elem;
                }
                break;
            }
        }
    }

    stack.resize(sp);
    exprStack = stack;
}

Expression parseInput(std::string input){
    token_count = 0;
    token_size = 100;
    current = 0;
    tokens = (Token*)malloc(100 * sizeof(Token));

    // Get rid of the new line
    input[strcspn(input.c_str(), "\n")] = '\0';

    removeSpaces(input);

    // Puts the input into an array of Tokens and tokenizes them
    tokenize(input.c_str());

    Expr* expression = parse_expression();

    // Convert to postfix notation because its faster
    Expression exprStack;
    treeToPostfix(expression, exprStack);

    // Fold constants
    foldConstants(exprStack);

    // // Print the postfix notation for visuals
    // for (auto elem : exprStack){
    //     if (elem.kind == Stack::EXPR_NUMBER) std::cout << elem.number;
    //     if (elem.kind == Stack::EXPR_VAR) std::cout << elem.var;
    //     if (elem.kind == Stack::EXPR_UNARY) printOp(elem.op);
    //     if (elem.kind == Stack::EXPR_BINARY) printOp(elem.op);

    //     std::cout << " ";
    // }
    // std::cout << "\n";

    free(tokens);
    
    return exprStack;
}

// The variable must be a single character long
void assignValue(unsigned char variable, double value){
    vars[variable] = value;
}

void setToConstant(unsigned char variable){
    constants[variable] = true;
}

void setToVariable(unsigned char variable){
    constants[variable] = false;
}

void resetVariables(){
    for (int i = 0; i < tableSize; i++){
        vars[i] = 0;
    }
}