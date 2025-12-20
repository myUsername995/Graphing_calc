/*
HOW TO USE: 
-parseInput: pass the string into this function, and it will return an expression that is easy to evaluate by the computer
-eval: Evaluates an expression. For this to work the expression has to be already compiled. Additionally you can pass variables 
using the assignVariables() function before you call eval(), and it will evaluate the function with those values.
-assignValue: A function to assign a value to some variable. Note that each variable must be only 1 character long.
-resetVariables: sets every slot in the variables table to 0 (essentially resetting every variable)

Usable operations:
binary (two arguements): +; -; *; /; ^; %;
unary (single arguement): abs(); ln(); log(); sqrt(); sin(); cos(); tan(); asin(); acos(); atan(); floor;

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
#include <algorithm>
#include <unordered_map>
#include "expression.hpp"
#include "time.hpp"

#define tableSize 256
// Keep a global hashtable for the variables, and also for constant variables (so that we can fold constants)
std::unordered_map<std::string, double> vars;
std::unordered_map<std::string, bool> constants;
std::string errors;

bool expressionValid = true;

// The variable must be a single character long
void assignValue(const std::string& variable, double value){
    vars[variable] = value;
}

// Retrieve a variables value
double getValue(const std::string& variable){
    return vars[variable];
}

// Returns if a variable is constant or not
bool isConstant(const std::string& variable){
    return constants[variable];
}

void setToConstant(const std::string& variable){
    constants[variable] = true;
}

void setToVariable(const std::string& variable){
    constants[variable] = false;
}

void resetVariables(){
    vars.clear();
}

void printOp(Operations op){
    switch (op) {
        case OP_NEG:  errors += ("neg"); break;
        case OP_ABS:  errors += ("abs"); break;
        case OP_SQRT: errors += ("sqrt"); break;
        case OP_LN:  errors += ("ln"); break;
        case OP_LOG:  errors += ("log"); break;
        case OP_SIN:  errors += ("sin"); break;
        case OP_COS:  errors += ("cos"); break;
        case OP_TAN:  errors += ("tan"); break;
        case OP_ASIN: errors += ("asin"); break;
        case OP_ACOS: errors += ("acos"); break;
        case OP_ATAN: errors += ("atan"); break;
        case OP_FLOOR: errors += ("floor"); break;
        case OP_ADD: errors += ("+"); break;
        case OP_SUB: errors += ("-"); break;
        case OP_MUL: errors += ("*"); break;
        case OP_DIV: errors += ("/"); break;
        case OP_POW: errors += ("^"); break;
        case OP_MOD: errors += ("%"); break;
    }
}

void freeExpressions(Expr* node) {
    if (!node) return;

    freeExpressions(node->left);
    freeExpressions(node->right);

    delete node;
}

static void removeSpaces(std::string& str) {
    str.erase(std::remove(str.begin(), str.end(), ' '), str.end());
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

bool isLetterVar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

std::vector<Token> tokens;
int current = 0;

static void tokenize(const char* input){
    // Go until the end of the input
    int i = 0;
    while (input[i]){
        if (isdigit(input[i])){
            char* end; Token tkn;
            tkn.type = TOKEN_NUMBER; tkn.value = strtod(&input[i], &end);

            tokens.push_back(tkn);
            i = end - input;                // Step I forward to the next non-digit
        }
        else {
            Token tkn;
            switch (input[i]){
                case '*': tkn.type = TOKEN_STAR; i++; break;
                case '/': tkn.type = TOKEN_SLASH; i++; break;
                case '+': tkn.type = TOKEN_PLUS; i++; break;
                case '-': tkn.type = TOKEN_MINUS; i++; break;
                case '^': tkn.type = TOKEN_ARROW; i++; break;
                case '%': tkn.type = TOKEN_PERCENT; i++; break;
                case '(': tkn.type = TOKEN_LPAREN; i++; break;
                case ')': tkn.type = TOKEN_RPAREN; i++; break;
                case 'e': tkn.value = e; tkn.type = TOKEN_NUMBER; i++; break;
                default:
                    // Check for other things
                    if (checkInput(input, "pi", i)){
                        tkn.value = pi;
                        tkn.type = TOKEN_NUMBER;

                        i += 2;
                    }
                    else if (checkInput(input, "abs", i)){
                        tkn.type = TOKEN_ABS;

                        i += 3;
                    }
                    else if (checkInput(input, "sqrt", i)){
                        tkn.type = TOKEN_SQRT;

                        i += 4;
                    }
                    else if (checkInput(input, "ln", i)){
                        tkn.type = TOKEN_LN;

                        i += 2;
                    }
                    else if (checkInput(input, "log", i)){
                        tkn.type = TOKEN_LOG;

                        i += 3;
                    }
                    else if (checkInput(input, "sin", i)){
                        tkn.type = TOKEN_SIN;

                        i += 3;
                    }
                    else if (checkInput(input, "cos", i)){
                        tkn.type = TOKEN_COS;

                        i += 3;
                    }
                    else if (checkInput(input, "tan", i)){
                        tkn.type = TOKEN_TAN;

                        i += 3;
                    }
                    else if (checkInput(input, "asin", i)){
                        tkn.type = TOKEN_ASIN;

                        i += 4;
                    }
                    else if (checkInput(input, "acos", i)){
                        tkn.type = TOKEN_ACOS;

                        i += 4;
                    }
                    else if (checkInput(input, "atan", i)){
                        tkn.type = TOKEN_ATAN;

                        i += 4;
                    }
                    else if (checkInput(input, "floor", i)){
                        tkn.type = TOKEN_FLOOR;

                        i += 5;
                    }
                    else {
                        // We found a variable, go until we the end of the variable
                        std::string converted(input);

                        int startIndex = i;
                        while (i < converted.size() && isLetterVar(converted[i])) i++;

                        if (i > startIndex) {  // always positive length
                            std::string temp = converted.substr(startIndex, i - startIndex);
                            tkn.type = TOKEN_VAR;
                            tkn.var = temp;
                        } else {
                            // handle empty variable error
                            expressionValid = false;
                            errors += "Empty variable at index " + std::to_string(startIndex) + "\n";
                            return;
                        }
                    }
            }

            tokens.push_back(tkn);
        }
    }

    Token endTkn;
    endTkn.type = TOKEN_END;

    tokens.push_back(endTkn);
}

static Token peek(){
    if (current >= tokens.size()) {
        expressionValid = false;
        errors += "Index exceeded the size of the tokens array while peeking.\n";
        Token t; t.type = TOKEN_INVALID;
        return t;
    }
    return tokens[current];
}

static Token advance(){
    if (current >= tokens.size()) {
        expressionValid = false;
        errors += "Index exceeded the size of the tokens array while advancing.\n";
        Token t; t.type = TOKEN_INVALID;
        return t;
    }

    return tokens[current++];
}

// Silly forward declaration
static Expr* parse_expression();

static Expr* parse_binary(int min_pre);

static Expr* parseFunction(Operations operation){
    // Parse the equation inside the parenthesis
    if (advance().type != TOKEN_LPAREN){
        expressionValid = false;
        errors += ("Expected a parenthesis after function" + std::to_string(current) + '\n');
        return NULL;
    }

    Expr* rhs = parse_binary(1);

    if (advance().type != TOKEN_RPAREN){
        expressionValid = false;
        errors += ("Expected closing parenthesis! Index: " + std::to_string(current) + '\n');
        freeExpressions(rhs);
        return NULL;
    }

    if (!rhs){
        errors += "Invalid expression after the unary operator '";
        printOp(operation);
        errors += "'.\n";
        return NULL;
    }

    Expr* node = new Expr();
    node->kind = Expr::EXPR_UNARY;
    node->op = operation;
    node->right = rhs;
    return node;
}

static Expr* parse_primary() {
    Token tok = advance();
    if (tok.type == TOKEN_NUMBER){
        Expr* expr = new Expr();
        expr->kind = Expr::EXPR_NUMBER;
        expr->number = tok.value;
        return expr;
    }
    else if (tok.type == TOKEN_MINUS) {
        // Parse operand with higher binding power than * or +
        Expr* rhs = parse_binary(3);
        if (!rhs){
            errors += "Invalid expression after '-' sign.\n";
            return NULL;
        }

        Expr* node = new Expr();
        node->kind = Expr::EXPR_UNARY;
        node->op = OP_NEG;
        node->right = rhs;
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
            errors += "Invalid expression inside the parenthesis.\n";
            expressionValid = false;
            return NULL;
        }
        if (advance().type != TOKEN_RPAREN) {
            expressionValid = false;
            errors += ("Expected closing parenthesis! Index: " + std::to_string(current) + '\n');
            freeExpressions(expr);
            return NULL;
        }
        return expr;
    }
    else if (tok.type == TOKEN_VAR){
        Expr* expr = new Expr();
        expr->kind = Expr::EXPR_VAR;
        expr->var = tok.var;
        return expr;
    }
    else {
        expressionValid = false;
        errors += ("Unexpected token in primary! Index: " + std::to_string(current) + '\n');
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
        case TOKEN_END: return 0;           // Make sure the function exits when it reaches the end of the tokens
        default: return 0;
    }
}

static Expr* parse_binary(int min_prec) {
    Expr* left = parse_primary();

    if (!left){
        errors += "Invalid expression in primary.\n";
        expressionValid = false;
        return NULL;
    }

    while (1) {
        TokenType op_type = peek().type;
        int prec = get_precedence(op_type);
        if (prec < min_prec) break;

        advance();  // consume operator
        Expr* right = parse_binary(prec + 1);

        if (!right){
            expressionValid = false;
            errors += "Couldn't parse the right side of the equation.\n";
            freeExpressions(left);
            return NULL;
        }

        if (!expressionValid){
            freeExpressions(left);
            freeExpressions(right);
            return NULL;
        }

        Expr* new_expr = new Expr();
        new_expr->kind = Expr::EXPR_BINARY;
        new_expr->op = (op_type == TOKEN_PLUS) ? OP_ADD:
                       (op_type == TOKEN_MINUS) ? OP_SUB:
                       (op_type == TOKEN_STAR) ? OP_MUL:
                       (op_type == TOKEN_SLASH) ? OP_DIV: 
                       (op_type == TOKEN_ARROW) ? OP_POW : OP_MOD;
                              
        new_expr->left = left;
        new_expr->right = right;
        left = new_expr;
    }

    return left;
}

static Expr* parse_expression() {
    return parse_binary(1);
}

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
                stack[sp++] = vars[elem.var];
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
        treeToPostfix(expression->right, exprStack);

        Stack elem;
        elem.kind = Stack::EXPR_UNARY;
        elem.op = expression->op;

        exprStack.push_back(elem);
    }
    else if (expression->kind == Expr::EXPR_BINARY){
        treeToPostfix(expression->left, exprStack);
        treeToPostfix(expression->right, exprStack);

        Stack elem;
        elem.kind = Stack::EXPR_BINARY;
        elem.op = expression->op;

        exprStack.push_back(elem);
    }
}

void foldConstants(Expression& exprStack){
    Expression stack; int size = 100;
    stack.resize(size);
    int sp = 0;

    // Traverse the stack, and for each op we see, if the top 2 (or 1) elements of the stack are constants, fold them
    for (const auto& elem : exprStack){
        switch (elem.kind){
            case Stack::EXPR_NUMBER: {
                stack[sp++] = elem;

                if (sp >= size){
                    size *= 2; stack.resize(size);
                }
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
                if (sp >= size){
                    size *= 2; stack.resize(size);
                }
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
                    if (sp >= size){
                    size *= 2; stack.resize(size);
                }
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
                    if (sp >= size){
                        size *= 2; stack.resize(size);
                    }
                }
                break;
            }
        }
    }

    stack.resize(sp);

    exprStack = stack;
}

void printErrors(){
    if (errors.empty()) return;

    std::cout << errors << std::endl;
}

Expression parseInput(std::string input){
    expressionValid = true;
    errors.clear();
    
    current = 0;
    tokens.clear();

    // Get rid of the newline
    size_t pos = input.find('\n');
    if (pos != std::string::npos) {
        input.erase(pos);
    }

    removeSpaces(input);

    // Puts the input into an array of Tokens and tokenizes them
    tokenize(input.c_str());

    Expr* expression = parse_expression();

    if (!expressionValid){
        freeExpressions(expression);
        errors += ("Expression couldn't be parsed\n");
        return {};
    }

    // Convert to postfix notation because its faster
    Expression exprStack;
    treeToPostfix(expression, exprStack);

    // Free the expressions structure
    freeExpressions(expression);

    // Fold constants
    foldConstants(exprStack);

    // Print the postfix notation for visuals
    for (auto elem : exprStack){
        if (elem.kind == Stack::EXPR_NUMBER) errors += (std::to_string(elem.number));
        if (elem.kind == Stack::EXPR_VAR) errors += (elem.var);
        if (elem.kind == Stack::EXPR_UNARY) printOp(elem.op);
        if (elem.kind == Stack::EXPR_BINARY) printOp(elem.op);

        errors += (" ");
    }
    errors += ("\n");
    
    return exprStack;
}