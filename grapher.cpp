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
-var: var (name) = (value) -> var a = 5
-func: func (name) = (expression) -> func a = y = x

-You cannot compare more than 2 functions at once
-For the type of functions you can input, look at expression.cpp. Documentation is at the top.
-The expressions can be essentially in any form. Here are some examples:
"y = x", "y - x = 0", "x = 5", "y = 5", "0 = 1", "a + b + c + d = 0"
-> for the last one, you need to set the variables, like: var a = 5, var b = 2, etc...
-Also, when comparing two functions, you have to use the name you gave your function, so for example:
func a = y = x,  func b = y = -x, comp a || b

DOCUMENTATION:
How the renderer works: it takes your input, eg.: y <= x, and then it transform that into the string (y - x) and compiles that 
as an expression, using the expression.hpp library I made. It does this for every string inside functionStrs, and puts it into the 
array functions. The renderer goes through every function, and then for each pixel it applies that function, and checks if the result 
is either negative or positive (0 counts as negative). This is used to ensure the line is drawn visually well instead of just being a 
bunch of points. It then goes through all the elements in the comparisons array, and compares two functions at a time individually per 
pixel. It draws the resulting pixels to a texture, which is then rendered on the screen.

Miscellanous facts:
The rest of the things inside this project I won't explain, such as the way the axis are rendered or how I implemented the moving 
around and zooming, because I've just copied that from the old graphing project, lol. I still understand it tho, and hopefully 
when you're reading this you still understand it. If not, go watch a video on it or something.

Besides the way the numbers are plotted on the axis is a mystery to me, I just copied it from the old project.

The dashed lines are rendered like this: for the first 50 (or whatever number) of points that are on the boundary, the program draws 
a pixel, and then for the next 50 it doesn't draw a pixel, then it draws pixels again and so on. You might think that the dashed 
lines look weird and move around in ways that they shouldn't do, but if you look at desmos, it looks the same (besides the random gaps 
in some of the dashed lines).
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
#include "expression.hpp"
#include "time.hpp"

#define WINDOW_HEIGHT 800
#define WINDOW_WIDTH 800

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
enum Booleans {OR, AND, XOR, DIFF};
enum Comparators {EQ, LT, LTE, GT, GTE};

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

SDL_Texture* renderTexture(SDL_Renderer* renderer, TTF_Font* font, const std::string& str, SDL_FRect& pos, SDL_Color color){
    SDL_Surface* surface = TTF_RenderText_Solid(font, str.c_str(), str.length(), color);

    pos.w = surface->w;
    pos.h = surface->h;

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);

    SDL_DestroySurface(surface);

    return texture;
}

// Return the bounding rectangle so we can position other texts accordingly
SDL_FRect renderTexts(SDL_Renderer* renderer, TTF_Font* font, const std::string& str, SDL_FPoint pos, SDL_Color color){
    // Rectangle that has its top left corner at pos.x, pos.y
    SDL_FRect posRect = {pos.x, pos.y, 0, 0};

    if (str.empty()) return posRect;

    SDL_Texture* tex = renderTexture(renderer, font, str, posRect, color);
    SDL_RenderTexture(renderer, tex, NULL, &posRect);
    SDL_DestroyTexture(tex);

    return posRect;
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

void setPixel(void *pixels, int pitch, int x, int y,
              Uint8 sr, Uint8 sg, Uint8 sb, Uint8 sa){
    if (!pixels || x < 0 || y < 0) return;

    Uint32 *p = (Uint32*)((Uint8*)pixels + y * pitch);

    // Read existing destination pixel
    Uint32 dst = p[x];

    Uint8 dr = (dst >> 24) & 0xFF;
    Uint8 dg = (dst >> 16) & 0xFF;
    Uint8 db = (dst >>  8) & 0xFF;
    Uint8 da =  dst        & 0xFF;

    // Convert alpha to 0–1 range
    float a  = sa / 255.0f;
    float ia = 1.0f - a;

    // Source-over alpha compositing (same as SDL_BLENDMODE_BLEND)
    Uint8 rr = (Uint8)(sr * a + dr * ia);
    Uint8 rg = (Uint8)(sg * a + dg * ia);
    Uint8 rb = (Uint8)(sb * a + db * ia);
    Uint8 ra = (Uint8)(sa * a + da * ia); // matches SDL's alpha behavior

    // Store back to RGBA8888
    p[x] = (rr << 24) | (rg << 16) | (rb << 8) | ra;
}

bool onlySpaces(const std::string& str) {
    for (const auto& letter : str){
        if (letter != ' ') return false;
    }

    return true;
}

bool checkInput(std::string str, std::string subStr, int startIndex){
    return str.find(subStr, startIndex) != std::string::npos;
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

std::vector<Function> getFunctions(std::vector<std::string> strs){
    std::vector<Function> functions(strs.size());

    bool foundRelationSign = false;

    // Transform the string into the string that will be calculated (eg. "y = x" -> "y - x")
    for (int idx = 0; idx < strs.size(); idx++){
        std::string str = strs[idx];

        int index = 0;
        int length = 0;

        // Find the separating relation sign
        for (int i = 0; i < str.size() - 1; i++){
            if (str[i] == '<' && str[i+1] == '='){
                index = i; length = 2; functions[idx].relationSign = LTE;
                foundRelationSign = true;
                break;
            }
            else if (str[i] == '>' && str[i+1] == '='){
                index = i; length = 2; functions[idx].relationSign = GTE;
                foundRelationSign = true;
                break;
            }
            else if (str[i] == '='){
                index = i; length = 1; functions[idx].relationSign = EQ;
                foundRelationSign = true;
                break;
            }
            else if (str[i] == '<'){
                index = i; length = 1; functions[idx].relationSign = LT;
                foundRelationSign = true;
                break;
            }
            else if (str[i] == '>'){
                index = i; length = 1; functions[idx].relationSign = GT;
                foundRelationSign = true;
                break;
            }
        }

        if (!foundRelationSign){
            functions.erase(functions.begin() + idx);
            break;
        }

        // Split the string according to this separator
        std::string half1 = str.substr(0, index);
        std::string half2 = str.substr(index + length);

        if (onlySpaces(half2)){
            functions.erase(functions.begin() + idx);
            break;
        }

        functions[idx].expr = parseInput("(" + half1 + ") - (" + half2 + ")");
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

            // Skip white space
            while (i < input.size() && input[i] == ' ') i++;
            if (i >= input.size()) continue;

            unsigned char var = input[i];

            // Find the equals sign
            while (i < input.size() && input[i] != '=') i++;
            if (i >= input.size()) continue;
            i++;

            // Skip white space
            while (i < input.size() && input[i] == ' ') i++;
            if (i >= input.size()) continue;
            int valStartIndex = i;

            // Find the end of the value
            while (i < input.size() && input[i] != ' ') i++;

            std::string val = input.substr(valStartIndex, i - valStartIndex);
            double value;

            if (val == "pi"){
                value = pi;
            }
            else if (val == "e"){
                value = e;
            }
            else {
                bool isValid = stringToDouble(val, value);
                if (!isValid) continue;
            }

            // Put the variable into the symbol table, and also set it to a constant (it doesnt change during the pixel drawing loop)
            assignValue(var, value);
            setToConstant(var);
        }
        // Handle functions, form: "func f(x) = y <= x"
        else if (checkInput(input, "func", 0)){
            int i = 4;

            // Skip whitespace
            while (i < input.size() && input[i] == ' ') i++;
            if (i >= input.size()) continue;

            // Parse the functions name
            int nameStartIndex = i;

            while (i < input.size() && input[i] != ' ') i++;
            if (i >= input.size()) continue;
            int nameEndIndex = i;
            std::string funcName = input.substr(nameStartIndex, nameEndIndex - nameStartIndex);

            // Go until we find the equals sign
            while (i < input.size() && input[i] != '=') i++;
            if (i >= input.size()) continue;
            i++;

            // Skip whitespace
            while (i < input.size() && input[i] == ' ') i++;
            if (i >= input.size()) continue;

            funcNames.push_back(funcName);

            // Parse the actual function
            std::string expr = input.substr(i);
            exprs.push_back(expr);
        }
        else if (checkInput(input, "comp", 0)){
            int i = 4;

            // Skip whitespace
            while (i < input.size() && input[i] == ' ') i++;
            if (i >= input.size()) continue;

            // Parse the first name
            int nameStartIndex = i;

            // Go to the end
            while (i < input.size() && input[i] != ' ') i++;
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

    SDL_Renderer* renderer;
    SDL_Window* window;

    SDL_Init(SDL_INIT_VIDEO);
    SDL_CreateWindowAndRenderer("Grapher", WINDOW_WIDTH, WINDOW_HEIGHT, 0, &window, &renderer);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    TTF_Init();

    bool leftMouseDown = false;
    bool updateExpressions = false;

    double zoom = 1.0 / 20.0;

    bool run = true;
    SDL_Event event;

    // Center the view at the start
    SDL_FPoint top_left;
    top_left.x = 0 - zoom * (WINDOW_WIDTH / 2);
    top_left.y = 0 - zoom * (WINDOW_HEIGHT / 2);

    SDL_FPoint startPan = {0, 0};

    // Create a streaming texture for direct pixel manipulation
    SDL_Texture *pixelTex = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING,
        WINDOW_WIDTH, WINDOW_HEIGHT
    );

    int letterTrack = 0;

    std::vector<std::string> userInput = {"func a = y < -x^3-2*x^2+2*x+1", "comp a || a"};
    int uiTrack = userInput.size() - 1; int uiSize = userInput.size();

    std::vector<compare> comparisons;
    std::vector<Function> functions;

    updateExprs(functions, comparisons, userInput);

    #define Grid std::vector<std::vector<States>>
    // Create a grid to record the sign of values
    std::vector<Grid> gridSigns;

    // Initalize the grids
    for (int i = 0; i < 100; i++){
        std::vector<std::vector<States>> grid(WINDOW_HEIGHT + 1, std::vector<States>(WINDOW_WIDTH + 1));
        gridSigns.push_back(grid);
    }

    double curWorldWidth = WINDOW_WIDTH;
    double curWorldHeight = WINDOW_HEIGHT;

    double gridWidth = WINDOW_WIDTH / 20.0;
    double gridHeight = WINDOW_HEIGHT / 20.0;

    TTF_Font* font = TTF_OpenFont("Roboto_Condensed-Black.ttf", 20);

    SDL_StartTextInput(window);

    while (run){
        Clock clk = begin();

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

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
            updateExprs(functions, comparisons, userInput);
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

        SDL_SetRenderDrawColor(renderer, 127, 127, 127, 127);

        // Render the grids
        for (int i = startGridX; i < endGridX; i++){
            SDL_FPoint p1 = world_to_screen({float(i * gridWidth), world_top_left.y}, zoom, top_left);
            SDL_FPoint p2 = world_to_screen({float(i * gridWidth), world_bottom_right.y}, zoom, top_left);

            SDL_RenderLine(renderer, p1.x, p1.y, p2.x, p2.y);
        }

        for (int i = startGridY; i < endGridY; i++){
            SDL_FPoint p1 = world_to_screen({world_top_left.x, float(i * gridHeight)}, zoom, top_left);
            SDL_FPoint p2 = world_to_screen({world_bottom_right.x, float(i * gridHeight)}, zoom, top_left);

            SDL_RenderLine(renderer, p1.x, p1.y, p2.x, p2.y);
        }

        // Render the axis
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

        // Vertical line
        {
        SDL_FPoint p1_world = {0, world_top_left.y};
        SDL_FPoint p2_world = {0, world_bottom_right.y};

        SDL_FPoint p1_screen = world_to_screen(p1_world, zoom, top_left);
        SDL_FPoint p2_screen = world_to_screen(p2_world, zoom, top_left);

        SDL_RenderLine(renderer, p1_screen.x, p1_screen.y, p2_screen.x, p2_screen.y);
        }

        // Horizontal line
        {
        SDL_FPoint p1_world = {world_top_left.x, 0};
        SDL_FPoint p2_world = {world_bottom_right.x, 0};

        SDL_FPoint p1_screen = world_to_screen(p1_world, zoom, top_left);
        SDL_FPoint p2_screen = world_to_screen(p2_world, zoom, top_left);

        SDL_RenderLine(renderer, p1_screen.x, p1_screen.y, p2_screen.x, p2_screen.y);
        }

        void *pixels;
        int pitch;
        SDL_LockTexture(pixelTex, NULL, &pixels, &pitch);

        SDL_FPoint start = screen_to_world({(float)(0), (float)(0)}, zoom, top_left);
        SDL_FPoint dirX = screen_to_world({(float)(1), (float)(0)}, zoom, top_left);
        SDL_FPoint dirY = screen_to_world({(float)(0), (float)(1)}, zoom, top_left);

        double xStep = fabs(dirX.x - start.x);
        double yStep = fabs(dirY.y - start.y);

        // X and Y are the cordinates in screen cordinates
        for (int i = 0; i < functions.size(); i++){
            for (int x = 0; x <= WINDOW_WIDTH; x++){
                for (int y = 0; y <= WINDOW_HEIGHT; y++){

                    assignValue('x', start.x + x * xStep);
                    assignValue('y', -(start.y + y * yStep));

                    gridSigns[i][y][x] = eval(functions[i].expr) <= 0 ? NEGATIVE : POSITIVE;
                }
            }
        }

        int dashLength = 20;
        // Go through every pixel on the screen and colour them based on the 4 corners
        for (int i = 0; i < comparisons.size(); i++){
            int dashLength1 = dashLength; int dashLength2 = dashLength; bool drawDash1 = false; bool drawDash2 = false;
            int index1 = comparisons[i].index1; int index2 = comparisons[i].index2;

            // Check if a function is strict or not
            bool isStrict1 = functions[index1].relationSign == LT || functions[index1].relationSign == GT;
            bool isStrict2 = functions[index2].relationSign == LT || functions[index2].relationSign == GT;

            const Grid& grid1 = gridSigns[index1];
            const Grid& grid2 = gridSigns[index2];
            for (int y = 0; y < WINDOW_WIDTH; y++){
                for (int x = 0; x < WINDOW_HEIGHT; x++){
                    int a1 = grid1[y][x];
                    int b1 = grid1[y][x + 1];
                    int c1 = grid1[y + 1][x];
                    int d1 = grid1[y + 1][x + 1];

                    int a2 = grid2[y][x];
                    int b2 = grid2[y][x + 1];
                    int c2 = grid2[y + 1][x];
                    int d2 = grid2[y + 1][x + 1];

                    bool allCornersEqual1 = a1 == b1 && a1 == c1 && a1 == d1;
                    bool allCornersEqual2 = a2 == b2 && a2 == c2 && a2 == d2;

                    // Decide if we should color the current pixel for both functions
                    bool colorPixel1, colorPixel2;
                    switch (functions[index1].relationSign){
                        case EQ: colorPixel1 = !allCornersEqual1; break;
                        case LT: colorPixel1 = allCornersEqual1 && a1 == NEGATIVE; break;
                        case LTE: colorPixel1 = !allCornersEqual1 || a1 == NEGATIVE; break;
                        case GT: colorPixel1 = allCornersEqual1 && a1 == POSITIVE; break;
                        case GTE: colorPixel1 = !allCornersEqual1 || a1 == POSITIVE; break;
                    }

                    switch (functions[index1].relationSign){
                        case EQ: colorPixel2 = !allCornersEqual2; break;
                        case LT: colorPixel2 = allCornersEqual2 && a2 == NEGATIVE; break;
                        case LTE: colorPixel2 = !allCornersEqual2 || a2 == NEGATIVE; break;
                        case GT: colorPixel2 = allCornersEqual2 && a2 == POSITIVE; break;
                        case GTE: colorPixel2 = !allCornersEqual2 || a2 == POSITIVE; break;
                    }

                    // Represent strict inequality with a dashed line (<) and the other inequality with a normal line (<=)
                    // ColorSolids -> same expression as it would be for just a normal straight line (=)
                    bool colorSolid1 = !allCornersEqual1;
                    bool colorSolid2 = !allCornersEqual2;

                    // If the current point is on the boundary line, subtract from dashLength once 
                    if (colorSolid1 && isStrict1){ dashLength1--; }
                    if (colorSolid2 && isStrict2){ dashLength2--; }
                    // Switch from drawing points on the boundary to not drawing points on the boundary (or the reverse) to make
                    // the line look dashed
                    if (dashLength1 < 0){
                        drawDash1 = !drawDash1; dashLength1 = dashLength;
                    }
                    if (dashLength2 < 0){
                        drawDash2 = !drawDash2; dashLength2 = dashLength;
                    }

                    // Draw on the boundary if: were on the boundary, and either we need to draw a dash on a strict line, or we need 
                    // to draw a full line on a non-strict line
                    bool colorBoundary1 = (colorSolid1 && !isStrict1) || (colorSolid1 && drawDash1 && isStrict1);
                    bool colorBoundary2 = (colorSolid2 && !isStrict2) || (colorSolid2 && drawDash2 && isStrict2);
                    bool colorBoundary = colorBoundary1 || colorBoundary2;

                    int a = colorBoundary ? 255 : 127;

                    SDL_Color c = comparisons[i].clr;
                    switch (comparisons[i].boolean){
                        case AND: if ((colorPixel1 && colorPixel2) || colorBoundary) setPixel(pixels, pitch, x, y, c.r, c.g, c.b, a); break;
                        case OR: if ((colorPixel1 || colorPixel2) || colorBoundary) setPixel(pixels, pitch, x, y, c.r, c.g, c.b, a); break;
                        case DIFF: if ((colorPixel1 && !colorPixel2) || colorBoundary) setPixel(pixels, pitch, x, y, c.r, c.g, c.b, a); break;
                        case XOR: if ((colorPixel1 ^ colorPixel2) || colorBoundary) setPixel(pixels, pitch, x, y, c.r, c.g, c.b, a); break;
                    }
                }
            }
        }

        SDL_UnlockTexture(pixelTex);

        // Draw pixel texture
        SDL_RenderTexture(renderer, pixelTex, NULL, NULL);

        // Clear the texture to black (RGBA = 0,0,0,255)
        memset(pixels, 0, pitch * WINDOW_HEIGHT);

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

            renderTexts(renderer, font, number, {p1.x, p1.y}, {255, 255, 255, 255});
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

            renderTexts(renderer, font, number, {p1.x, p1.y}, {255, 255, 255, 255});
        }
        }

        // Render texts
        // std::string zoomText = "Zoom: " + std::to_string(zoom);
        // SDL_FRect prevText = renderTexts(renderer, font, zoomText, {10, 10}, {255, 255, 255, 255});

        // std::string FPSText = "FPS: " + to_string_with_precision(FPS, 0);
        // prevText = renderTexts(renderer, font, FPSText, {prevText.x, prevText.y + 30}, {255, 255, 255, 255});

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
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 127);
            SDL_RenderLine(renderer, 2, prevRect.y + 40, 8, prevRect.y + 40);

            prevRect = renderTexts(renderer, font, userInput[i], {prevRect.x, prevRect.y + 30}, {255, 255, 255, 255});

            SDL_SetRenderDrawColor(renderer, background.r, background.g, background.b, background.a);
            SDL_RenderFillRect(renderer, &prevRect);

            if (uiTrack == i){
                // Render the white surrounding rectangle around the box
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 127);
                SDL_RenderRect(renderer, &prevRect);

                // Render the vertical line were at
                float posX = getScreenPos(font, userInput[i], letterTrack, {prevRect.x, prevRect.y}).x;
                SDL_RenderLine(renderer, posX, prevRect.y, posX, prevRect.y + prevRect.h);
            }
        }

        double FPS = calculateFPS(end(clk));

        SDL_RenderPresent(renderer);
    }

    SDL_StopTextInput(window);

    return 0;
}