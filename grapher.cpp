/*
INTRODUCTION: A graphing calculator that can take inputs in basically any form. It can also graph inequalities, 
and also compare between functions.

USER MANUAL:  
Inequalities: <, <=, >, >=
Comparisons: or (||), and (&&), xor (^), set difference (\ or \\)

IMPORTANT KEYS:
-Enter: Makes you go onto a new line. If you press enter at the start of the string, the new line will appear above. Anywhere else, it 
will appear below. 
-Left and right arrow keys: Move between letters inside a line
-Up and down arrow keys: Move between lines (up or down)
-Backspace: remove the letter youre currently at (shown by the white vertical line)
-Tab: delete a line
-Any key: it types a letter

There are 3 different expressions you can input: -comp, -var, -func

How your expressions should look like:
-comp: comp (name1) (comparator) (name2) -> comp a || b
-var: var (name) = (expression) -> var a = b^2 * 2 + 5
-func: func (name): (equation) -> func a: y = x

-You cannot compare more than 2 functions at once
-For the type of functions you can input, look at expression.cpp. Documentation is at the top.
-The expressions can be essentially in any form. Here are some examples:
"y = x", "y - x = 0", "x = 5", "y = 5", "0 = 1", "a + b + c + d = 0"
-> for the last one, you need to set the variables, like: var a = 5, var b = 2, etc...
-Also, when comparing two functions, you have to use the name you gave your function, so for example:
func a = y = x,  func b = y = -x, comp a || b

DOCUMENTATION:
The renderer renders functions in 3 steps: 
1st: parsing user input
2nd: evaluating functions
3rd: combining functions

I'm going to explain each step in detail here:
1st step
This step happens everytime the user updates their expressions. The parsing happens inside of updateExprs(), where the three different 
types of inputs are all handled. 
-The "var" kind is parsed by first finding the variable name, then parsing and evaluating the function 
expression. Note that variables must be declared BEFORE they're used. 
-The "comp" kind is parsed by collecting all the names and comparators into an array, and after every functions is parsed, only then 
does it compare them, so that every function can get evaluted before they're combined. You can put this expression anywhere and it will 
work.
-The "func" kind is parsed by also collecting every function name and expression into an array, and it only starts going through all 
the functions once the variables have all been parsed. Every elements gets passed to a function called getFunction(), which converts the 
relationship the user inputted (e.g: y = x) to an actual equation (e.g: y - x). These are then evaluted.

After all of these inputs have been parsed, theyre passed to the GPU using the createBuffersAndUpload() function.

2nd step
This steps happens on every frame (because the user can move the camera around at any time). The expressions are converted into GPU 
code, and then passed into the compute shader. After this the compute shader gets recompiled.

3rd step
This step also happens on every frame, inside the runCompute() function. It's just a compute shader that compares each function per pixel, 
and colors each pixel accordingly. Boundaries are handled specially, because they should only be drawn if theyre next to a shaded 
pixel (otherwise when comparing 2 functions, the result might look silly).
*/


#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <stdint.h>
#include <cmath>
#include "expression.hpp"               // Expression parsing for functions
#include "time.hpp"                     // Timings for benchmarking and FPS
#include "GPU.hpp"                      // General drawing functions compatible with openGL
#include "rendering.hpp"                // Specific rendering of functions

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 800

std::array<SDL_Color, 9> colors = {
    SDL_Color{255, 0, 0, 127},   // Red
    SDL_Color{180, 255, 180, 127},   // Green
    SDL_Color{0, 0, 255, 127},   // Blue
    SDL_Color{255, 255, 0, 127}, // Yellow
    SDL_Color{255, 165, 0, 127}, // Orange
    SDL_Color{128, 0, 128, 127}, // Purple
    SDL_Color{0, 255, 255, 127}, // Cyan
    SDL_Color{255, 192, 203, 127}, // Pink
    SDL_Color{255, 255, 255, 127}, // White
};

enum States {NEGATIVE, POSITIVE};
enum Booleans : uint32_t {AND, OR, DIFF, XOR};
enum Comparators : uint32_t {EQ, LT, LTE, GT, GTE};

struct Function {
    Expression expr;
    Comparators relationSign;
};

struct compare {
    // Indexes into the functions array
    int index1;
    int index2;

    // How to compare the functions
    Booleans boolean;

    // Color of the function on the graph
    SDL_Color clr = {0, 0, 0, 0};
};

SDL_FPoint world_to_screen(const SDL_FPoint& p, const double& zoom, const SDL_FPoint& top_left){
    return {float((p.x - top_left.x) / zoom), float((p.y - top_left.y) / zoom)};
}

SDL_FPoint screen_to_world(const SDL_FPoint& p, const double& zoom, const SDL_FPoint& top_left){
    return {float(p.x * zoom + top_left.x), float(p.y * zoom + top_left.y)};
}

std::string to_string_with_precision(double value, int precision) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

SDL_FPoint getWidthAndHeight(TTF_Font* font, const std::string& str){
    SDL_Surface* surface = TTF_RenderText_Solid(font, str.c_str(), str.length(), {0, 0, 0, 0});

    return {(float)surface->w, (float)surface->h};
}

SDL_FPoint getScreenPos(TTF_Font* font, const std::string& str, int index, SDL_FPoint startPos) {
    SDL_FPoint pos = startPos;
    if (index <= 0) return pos;           // first character
    if (index > str.size()) index = str.size(); // clamp to string length

    float x = startPos.x;

    for (int i = 0; i < index; i++) {
        int minX, maxX, minY, maxY, advance;

        // SDL3_ttf: get metrics for a single character
        if (TTF_GetGlyphMetrics(font, str[i], &minX, &maxX, &minY, &maxY, &advance)) {
            x += advance;
        }
    }

    pos.x = x;
    pos.y = startPos.y;
    return pos;
}

bool onlySpaces(const std::string& str) {
    for (const auto& letter : str){
        if (letter != ' ') return false;
    }

    return true;
}

bool checkInput(std::string str, std::string subStr, int startIndex) {
    return str.find(subStr, startIndex) == startIndex;
}

bool stringToDouble(const std::string& str, double& outValue) {
    try {
        outValue = std::stod(str);
        return true;  // conversion succeeded
    } catch (const std::invalid_argument&) {
        // str is not a valid number
        return false;
    } catch (const std::out_of_range&) {
        // number is too large
        return false;
    }
}

// Converts strings into "Function" structs and also handles invalid strings
// Assumes that only strings that already refer to functions are passed in
std::vector<Function> getFunctions(std::vector<std::string> strs){
    std::vector<Function> functions;

    // Transform the string into the string that will be calculated (eg. "y = x" -> "y - x")
    for (int idx = 0; idx < strs.size(); idx++){
        std::string str = strs[idx];
        Function func;
        bool foundRelationSign = false;

        int index = 0;
        int length = 0;

        // Find the separating relation sign
        for (int i = 0; i < str.size() - 1; i++){
            if (str[i] == '<' && str[i+1] == '='){
                index = i; length = 2; func.relationSign = LTE;
                foundRelationSign = true;
                break;
            }
            else if (str[i] == '>' && str[i+1] == '='){
                index = i; length = 2; func.relationSign = GTE;
                foundRelationSign = true;
                break;
            }
            else if (str[i] == '='){
                index = i; length = 1; func.relationSign = EQ;
                foundRelationSign = true;
                break;
            }
            else if (str[i] == '<'){
                index = i; length = 1; func.relationSign = LT;
                foundRelationSign = true;
                break;
            }
            else if (str[i] == '>'){
                index = i; length = 1; func.relationSign = GT;
                foundRelationSign = true;
                break;
            }
        }

        if (!foundRelationSign){
            break;
        }

        // Split the string according to this separator
        std::string half1 = str.substr(0, index);
        std::string half2 = str.substr(index + length);

        if (onlySpaces(half2)){
            break;
        }

        func.expr = parseInput("(" + half1 + ") - (" + half2 + ")");
        if (func.expr.empty()) continue;

        functions.push_back(func);
    }

    return functions;
}

void updateExprs(std::vector<Function>& functions, std::vector<compare>& comparisons, const std::vector<std::string> userInputs){
    struct comp {
        std::string name1;
        std::string name2;
        Booleans boolean;
    };

    std::vector<std::string> exprs;
    std::vector<std::string> funcNames;
    std::vector<comp> compareNames;
    std::vector<compare> newComparisons;

    // Reset the variables, eg. if you had written var b = 10 before, but you now deleted it, it shouldn't keep that old value
    resetVariables();

    for (const auto& input : userInputs){
        // Handle variables, form: "var a = 55"
        if (checkInput(input, "var", 0)){
            int i = 3;

            // Enforce whitespace
            if (!checkInput(input, " ", i)){
                continue;
            }

            // Skip white space
            while (i < input.size() && input[i] == ' ') i++;
            if (i >= input.size()) continue;

            int varStartIndex = i;
            // Find the end of the variable
            while (i < input.size() && input[i] != ' ' && input[i] != '=') i++;
            if (i >= input.size()) continue;
            
            std::string var = input.substr(varStartIndex, i - varStartIndex).c_str();

            // Find the equals sign
            while (i < input.size() && input[i] != '=') i++;
            if (i >= input.size()) continue;
            i++;

            // Skip white space
            while (i < input.size() && input[i] == ' ') i++;
            if (i >= input.size()) continue;

            // Treat the right side as an expression
            double value = eval(parseInput(input.substr(i)));
            
            // Put the variable into the symbol table, and also set it to a constant (it doesnt change during the pixel drawing loop)
            assignValue(var, value);
            setToConstant(var);
        }
        // Handle functions, form: "func f(x) = y <= x"
        else if (checkInput(input, "func", 0)){
            int i = 4;

            // Enforce whitespace
            if (!checkInput(input, " ", i)){
                continue;
            }

            // Skip whitespace
            while (i < input.size() && input[i] == ' ') i++;
            if (i >= input.size()) continue;

            // Parse the functions name
            int nameStartIndex = i;

            while (i < input.size() && input[i] != ' ' && input[i] != ':') i++;
            if (i >= input.size()) continue;
            int nameEndIndex = i;
            std::string funcName = input.substr(nameStartIndex, nameEndIndex - nameStartIndex);

            // Go until we find the ':' sign
            while (i < input.size() && input[i] != ':') i++;
            if (i >= input.size()) continue;
            i++;

            // Skip whitespace
            while (i < input.size() && input[i] == ' ') i++;
            if (i >= input.size()) continue;

            funcNames.push_back(funcName);

            std::string expr = input.substr(i);
            exprs.push_back(expr);
        }
        else if (checkInput(input, "comp", 0)){
            int i = 4;

            // Enforce whitespace
            if (!checkInput(input, " ", i)){
                continue;
            }

            // Skip whitespace
            while (i < input.size() && input[i] == ' ') i++;
            if (i >= input.size()) continue;

            // Parse the first name
            int nameStartIndex = i;

            // Go to the end
            while (i < input.size() && 
                   input[i] != ' ' && 
                   !checkInput(input, "^", i) && 
                   !checkInput(input, "||", i) && 
                   !checkInput(input, "&&", i) && 
                   !checkInput(input, "\\", i)
            ) i++;

            if (i >= input.size()) continue;
            int nameEndIndex = i;
            std::string name1 = input.substr(nameStartIndex, nameEndIndex - nameStartIndex);

            // Find the boolean
            // Skip whitespace
            while (i < input.size() && input[i] == ' ') i++;
            if (i >= input.size()) continue;
            Booleans boolean;

            if (checkInput(input, "^", i)) {boolean = XOR; i++;}
            else if (checkInput(input, "||", i)) {boolean = OR; i += 2;}
            else if (checkInput(input, "&&", i)) {boolean = AND; i += 2;}
            else if (checkInput(input, "\\", i)) {boolean = DIFF; i += 2;}
            else {continue;}            // No boolean sign detected, invalid string

            // Skip whitespace
            while (i < input.size() && input[i] == ' ') i++;
            if (i >= input.size()) continue;

            // Parse the second function name
            nameStartIndex = i;

            // Go to the end
            while (i < input.size() && input[i] != ' ') i++;

            nameEndIndex = i;
            std::string name2 = input.substr(nameStartIndex, nameEndIndex - nameStartIndex);

            compareNames.push_back({name1, name2, boolean});
        }
    }

    // Create the comparisons
    for (int i = 0; i < compareNames.size(); i++){
        int index1 = -1;
        int index2 = -1;

        for (int j = 0; j < funcNames.size(); j++){
            if (funcNames[j] == compareNames[i].name1) index1 = j;
            if (funcNames[j] == compareNames[i].name2) index2 = j;
        }

        // Invalid names
        if (index1 == -1 || index2 == -1) continue;

        newComparisons.push_back({index1, index2, compareNames[i].boolean, colors[i % colors.size()]});
    }

    functions = getFunctions(exprs);
    comparisons = newComparisons;
}

int main(int argc, char* argv[]){

    SDL_Init(SDL_INIT_VIDEO);
    TTF_Init();

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    SDL_Window* window = SDL_CreateWindow("Grapher", WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_OPENGL);
    initalizeGPU(window, WINDOW_WIDTH, WINDOW_HEIGHT);
    int error = initializeRendering(WINDOW_WIDTH, WINDOW_HEIGHT);
    if (error == -1) return 0;

    // Used to evaluate the function
    GLuint shaderEvaluate = CreateComputeProgram(CompileShader(LoadFile("shaders\\evaluate.comp.glsl"), GL_COMPUTE_SHADER));
    GLuint shaderCombine = CreateComputeProgram(CompileShader(LoadFile("shaders\\combine.comp.glsl"), GL_COMPUTE_SHADER));
    if (shaderEvaluate == -1 || shaderCombine == -1) return 0;

    bool leftMouseDown = false;
    bool updateExpressions = true;
    double zoom = 1.0 / 20.0;
    double fps = 1000;
    bool run = true;
    SDL_Event event;

    // Center the view at the start
    SDL_FPoint top_left;
    top_left.x = 0 - zoom * (WINDOW_WIDTH / 2);
    top_left.y = 0 - zoom * (WINDOW_HEIGHT / 2);
    SDL_FPoint startPan = {0, 0};

    int letterTrack = 0;
    std::vector<std::string> userInput = {"var a = 5", "var b = a * 3", "func ellipse: y^2 / a^2 + x^2 / b^2 < 1", "func a: y < x^2", "comp a \\ ellipse"};
    int uiTrack = userInput.size() - 1; int uiSize = userInput.size();
    if (uiSize > 0) letterTrack = userInput[uiTrack].size();

    std::vector<compare> comparisons;
    std::vector<Function> functions;
    #define Grid std::vector<std::vector<States>>

    // An arbitrary upper limit for the number of functions you can input
    int numFunctions = 100;

    // Create a grid to record the sign of values
    std::vector<Grid> gridSigns;
    std::vector<std::vector<std::vector<bool>>> coloredPixels;
    std::vector<std::vector<std::vector<bool>>> coloredBoundary;

    // Initalize the grids
    for (int i = 0; i < numFunctions; i++){
        std::vector<std::vector<States>> grid(WINDOW_HEIGHT + 1, std::vector<States>(WINDOW_WIDTH + 1));
        std::vector<std::vector<bool>> pixels(WINDOW_HEIGHT + 1, std::vector<bool>(WINDOW_WIDTH + 1));
        std::vector<std::vector<bool>> boundary(WINDOW_HEIGHT + 1, std::vector<bool>(WINDOW_WIDTH + 1));

        gridSigns.push_back(grid);
        coloredPixels.push_back(pixels);
        coloredBoundary.push_back(boundary);
    }

    double curWorldWidth = WINDOW_WIDTH;
    double curWorldHeight = WINDOW_HEIGHT;
    double gridWidth = WINDOW_WIDTH / 20.0;
    double gridHeight = WINDOW_HEIGHT / 20.0;
    double FPS;

    TTF_Font* font = TTF_OpenFont("Roboto_Condensed-Black.ttf", 20);
    std::vector<Expression> exprs; std::vector<uint32_t> relationSigns; std::vector<Comparison> cmprs;
    std::vector<SDL_Color> funcColors;
    SDL_StartTextInput(window);

    while (run){
        Clock clk = begin();

        glClearColor(0,0,0,1);
        glClear(GL_COLOR_BUFFER_BIT);

        while (SDL_PollEvent(&event)){
            switch (event.type){
                case SDL_EVENT_QUIT: {
                    run = false;
                    break;
                }
                case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                    if (event.button.button == SDL_BUTTON_LEFT){
                        leftMouseDown = true;
                        startPan = {event.button.x, event.button.y};
                    }
                    break;
                }
                case SDL_EVENT_MOUSE_BUTTON_UP: {
                    if (event.button.button == SDL_BUTTON_LEFT){
                        leftMouseDown = false;
                    }
                    break;
                }
                case SDL_EVENT_TEXT_INPUT: {
                    // Add the inputted text from the user (leave the rendering for the later parts in the code)
                    userInput[uiTrack].insert(letterTrack, 1, event.text.text[0]);
                    letterTrack++;
                    updateExpressions = true;
                    break;
                }
                case SDL_EVENT_KEY_DOWN: {
                    switch (event.key.key){
                        // Enter key, makes you go onto the next line
                        case SDLK_RETURN: {
                            uiSize++; uiTrack++;

                            // If were at the start of a message, then insert the new function to the top
                            if (letterTrack == 0){
                                userInput.insert(userInput.begin() + uiTrack - 1, std::string{});
                            }
                            // Insert the new function to the bottom
                            else {
                                userInput.insert(userInput.begin() + uiTrack, std::string{});
                            }

                            letterTrack = 0;
                            break;
                        }
                        // Go back up a line
                        case SDLK_UP: {
                            uiTrack--;

                            if (uiTrack < 0) uiTrack = 0;

                            letterTrack = userInput[uiTrack].size();
                            break;
                        }
                        // Go back down a line
                        case SDLK_DOWN: {
                            uiTrack++;

                            if (uiTrack >= uiSize) uiTrack = uiSize - 1;
                            letterTrack = userInput[uiTrack].size();
                            break;
                        }
                        case SDLK_LEFT: {
                            letterTrack--;

                            if (letterTrack < 0) letterTrack = 0;
                            break;
                        }
                        case SDLK_RIGHT: {
                            letterTrack++;

                            if (letterTrack >= userInput[uiTrack].size()) letterTrack = userInput[uiTrack].size();
                            break;
                        }
                        // Delete the last letter from the text were currently on
                        case SDLK_BACKSPACE: {
                            // Cant remove at the very start
                            if (letterTrack == 0) break;

                            if (!userInput[uiTrack].empty()) userInput[uiTrack].erase(letterTrack - 1, 1);
                            letterTrack--;

                            if (letterTrack < 0) letterTrack = 0;

                            updateExpressions = true;
                            break;
                        }
                        // Delete the whole entry were currently on
                        case SDLK_TAB: {
                            userInput[uiTrack].clear();

                            // Don't delete the last element in the array, cuz u cant have a vector of 0 size or idk
                            if (uiSize != 1) userInput.erase(userInput.begin() + uiTrack);
                            uiSize--; uiTrack--;

                            if (uiSize < 1) uiSize = 1;
                            if (uiTrack < 0) uiTrack = 0;

                            letterTrack = userInput[uiTrack].size();
                            updateExpressions = true;
                            break;
                        }
                    }
                    break;
                }
                case SDL_EVENT_MOUSE_WHEEL: {
                    // Get mouse state
                    SDL_PumpEvents();

                    float x, y;
                    SDL_GetMouseState(&x, &y);

                    SDL_FPoint mouseBeforeZoom = {x, y};
                    mouseBeforeZoom = screen_to_world(mouseBeforeZoom, zoom, top_left);

                    zoom -= event.wheel.y * zoom / 10.0;

                    if (zoom < 1e-5){
                        zoom = 1e-5;
                    }

                    SDL_FPoint mouseAfterZoom = {x, y};
                    mouseAfterZoom = screen_to_world(mouseAfterZoom, zoom, top_left);

                    top_left.x += mouseBeforeZoom.x - mouseAfterZoom.x;
                    top_left.y += mouseBeforeZoom.y - mouseAfterZoom.y;
                    break;
                }
            }
        }

        if (updateExpressions){
            // Update the expressions
            updateExprs(functions, comparisons, userInput);

            // Prepare the GPU for the new expressions
            exprs.clear(); funcColors.clear(); relationSigns.clear(); cmprs.clear();

            // Push from functions into simpler arrays
            for (const auto& elem : functions){
                exprs.push_back(elem.expr);
                relationSigns.push_back(elem.relationSign);
            }

            std::string newShader = appendEvaluationFunction(exprs);
            shaderEvaluate = CreateComputeProgram(CompileShader(newShader, GL_COMPUTE_SHADER));

            if (shaderEvaluate == -1) std::cout << newShader << std::endl;

            // Push from comparisons into simpler arrays
            for (const auto& elem : comparisons){
                cmprs.push_back(Comparison{(unsigned int)elem.index1, (unsigned int)elem.index2, elem.boolean, 0});
                funcColors.push_back(elem.clr);
            }

            // Create the buffers in the GPU and upload them
            createBuffersAndUpload(cmprs, relationSigns, funcColors, functions.size());

            updateExpressions = false;
        }

        // Get mouse state
        SDL_PumpEvents();

        float x, y;
        SDL_GetMouseState(&x, &y);

        if (leftMouseDown){
            top_left.x -= (x - startPan.x) * zoom;
            top_left.y -= (y - startPan.y) * zoom;

            startPan = {x, y};
        }

        // Get world bounds of visible screen
        SDL_FPoint screen_top_left = {0, 0};
        SDL_FPoint screen_bottom_right = {WINDOW_WIDTH, WINDOW_HEIGHT};

        SDL_FPoint world_top_left = screen_to_world(screen_top_left, zoom, top_left);
        SDL_FPoint world_bottom_right = screen_to_world(screen_bottom_right, zoom, top_left);

        float worldWidth = world_bottom_right.x - world_top_left.x;
        float worldHeight = world_bottom_right.y - world_top_left.y;

        // Grow the grid
        if (worldWidth >= curWorldWidth / 4 || worldHeight >= curWorldHeight / 4){
            curWorldWidth *= 2;
            curWorldHeight *= 2;

            // Update the grid widths
            gridWidth *= 2;
            gridHeight *= 2;
        }

        // Shrink the grid
        if (worldWidth < curWorldWidth / 4 || worldHeight < curWorldHeight / 4){
            curWorldWidth /= 2;
            curWorldHeight /= 2;

            gridWidth /= 2;
            gridHeight /= 2;
        }

        int startGridX = floor((world_top_left.x) / (gridWidth));
        int endGridX = ceil((world_bottom_right.x) / (gridWidth));

        int startGridY = floor((world_top_left.y) / (gridHeight));
        int endGridY = ceil((world_bottom_right.y) / (gridHeight));

        // Render the grids
        for (int i = startGridX; i < endGridX; i++){
            SDL_FPoint p1 = world_to_screen({float(i * gridWidth), world_top_left.y}, zoom, top_left);
            SDL_FPoint p2 = world_to_screen({float(i * gridWidth), world_bottom_right.y}, zoom, top_left);

            GPURenderLine(p1, p2, {127, 127, 127, 127});
        }

        for (int i = startGridY; i < endGridY; i++){
            SDL_FPoint p1 = world_to_screen({world_top_left.x, float(i * gridHeight)}, zoom, top_left);
            SDL_FPoint p2 = world_to_screen({world_bottom_right.x, float(i * gridHeight)}, zoom, top_left);

            GPURenderLine(p1, p2, {127, 127, 127, 127});
        }

        // Render the axis
        // Vertical line
        {
        SDL_FPoint p1_world = {0, world_top_left.y};
        SDL_FPoint p2_world = {0, world_bottom_right.y};

        SDL_FPoint p1_screen = world_to_screen(p1_world, zoom, top_left);
        SDL_FPoint p2_screen = world_to_screen(p2_world, zoom, top_left);

        GPURenderLine(p1_screen, p2_screen, {255, 255, 255, 255});
        }

        // Horizontal line
        {
        SDL_FPoint p1_world = {world_top_left.x, 0};
        SDL_FPoint p2_world = {world_bottom_right.x, 0};

        SDL_FPoint p1_screen = world_to_screen(p1_world, zoom, top_left);
        SDL_FPoint p2_screen = world_to_screen(p2_world, zoom, top_left);

        GPURenderLine(p1_screen, p2_screen, {255, 255, 255, 255});
        }

        SDL_FPoint start = screen_to_world({(float)(0), (float)(0)}, zoom, top_left);
        SDL_FPoint dirX = screen_to_world({(float)(1), (float)(0)}, zoom, top_left);
        SDL_FPoint dirY = screen_to_world({(float)(0), (float)(1)}, zoom, top_left);

        double xStep = fabs(dirX.x - start.x);
        double yStep = fabs(dirY.y - start.y);

        runCompute(shaderEvaluate, shaderCombine, functions.size(), comparisons.size(), start.x, start.y, xStep, yStep);

        // Render the numbers on the axis
        {
        float startNum = gridWidth + gridWidth * (startGridX-1);
        std::string number;

        bool horizontalOnTop = world_top_left.y > 0 && world_bottom_right.y > 0;
        bool horizontalOnBottom = world_top_left.y < 0 && world_bottom_right.y < 0;
        bool horizontalVisible = !horizontalOnTop && !horizontalOnBottom;

        // Draw the numbers on the horizontal line
        for (int i = startGridX; i < endGridX; i++){
            SDL_FPoint p1 = world_to_screen({float(i * gridWidth), 0}, zoom, top_left);

            if (horizontalVisible){
                p1.y -= 10;
            }
            else if (horizontalOnTop){
                p1.y = 0;
            }
            else if (horizontalOnBottom){
                p1.y = WINDOW_HEIGHT - 25;
            }

            number = to_string_with_precision(startNum, 3);
            startNum += gridWidth;

            GPURenderText(font, number, {p1.x, p1.y}, {255, 255, 255, 255});
        }
        }

        {
        float startNum = gridHeight + gridHeight * (startGridY-1);
        std::string number;

        bool verticalOnRight = world_top_left.x < 0 && world_bottom_right.x < 0;
        bool verticalOnLeft = world_top_left.x > 0 && world_bottom_right.x > 0;
        bool verticalVisible = !verticalOnRight && !verticalOnLeft;
        // Draw the numbers on the vertical line
        for (int i = startGridY; i < endGridY; i++){
            // Dont render 0 (horizontal line already rendered it)
            if (startNum == 0 && verticalVisible){
                startNum += gridHeight;
                continue;
            }
            SDL_FPoint p1 = world_to_screen({0, float(i * gridHeight)}, zoom, top_left);

            if (verticalVisible){
                p1.y -= 10;
            }
            else if (verticalOnRight){
                p1.x = WINDOW_WIDTH - 65;
            }
            else if (verticalOnLeft){
                p1.x = 0;
            }

            number = to_string_with_precision(-startNum, 3);
            startNum += gridHeight;

            GPURenderText(font, number, {p1.x, p1.y}, {255, 255, 255, 255});
        }
        }

        SDL_FRect prevRect = {10, -20};
        int clrTrack = 0;
        // Render user inputs
        for (int i = 0; i < uiSize; i++){
            SDL_Color background;
            // If the current text is a comparison function (things that actually appear on the graph), then assign it a color
            if (checkInput(userInput[i], "comp", 0)){
                background = colors[clrTrack++ % colors.size()];
            }
            else {
                background = {255, 0, 0, 127};
            }

            // Render the background and text
            GPURenderLine({2, prevRect.y + 40}, {8, prevRect.y + 40}, {255, 255, 255, 127});

            prevRect = GPURenderText(font, userInput[i], {prevRect.x, prevRect.y + 30}, {255, 255, 255, 255});

            // Background rectangle
            GPURenderRect(prevRect, background, true);
            if (uiTrack == i){
                // Render the white surrounding rectangle around the box
                GPURenderRect(prevRect, {255, 255, 255, 127}, false);

                // Render the vertical line were at
                float posX = getScreenPos(font, userInput[i], letterTrack, {prevRect.x, prevRect.y}).x;
                GPURenderLine({posX, prevRect.y}, {posX, prevRect.y + prevRect.h}, {255, 255, 255, 127});
            }
        }

        // Were actually showing the previously calculated FPS, but its fine because its atleast accurate
        // Render texts
        std::string FPSText = "FPS: " + to_string_with_precision(FPS, 0);
        int width = getWidthAndHeight(font, FPSText).x;
        GPURenderText(font, FPSText, {(float)WINDOW_WIDTH - width- 10, 10}, {255, 255, 255, 255});

        SDL_GL_SwapWindow(window);

        double dt = end(clk);

        // We cap the FPS, so make the counter match the actual FPS
        if (dt < 1000.0 / fps) dt = 1000.0 / fps;

        FPS = calculateFPS(dt);

        // Cap the FPS
        if (dt < 1000.0 / fps){
            SDL_Delay(1000.0 / fps - dt);
        }
    }

    SDL_StopTextInput(window);

    return 0;
}