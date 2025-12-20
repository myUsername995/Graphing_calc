/*
A library to graph functions using compute shaders on the GPU.

There are 3 main functions, with the other ones are helper functions:

-appendEvaluationFunction() -> Takes an array of expression as input, converts them to GLSL code, and puts them into the 
evaluation.comp.glsl file. You must recompile the file if you want to see any changes after calling this.

-createBuffersAndUpload() -> creates buffers for the compute shader and uploads them. 
0: An array of 2D arrays that stores every corner on the screen, each corner can be negative, zero or positive. 
1: Comparisons array used to compare between functions. Every element has three attributes: index1, index2, boolean operator 
(between the first 2 functions). 
2: Relation signs array, used to store all the relation signs for each function. 
3: Colors array, used to store the colors of each function

-runCompute() -> runs the evaluate and combine shader. The evaluate shader assigns values for every function's gridSigns array, 
so that you get what each function looks like individually. This is where the expressions are evaluated. 
The combine shader looks at all the comparisons, and then renders the resulting functions onto one texture.

*/
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <vector>
#include <cstdint>
#include <cmath>
#include <fstream>
#include <sstream>
#include "rendering.hpp"
#include "GPU.hpp"
#include "time.hpp"

static int W, H, CORNER_W, CORNER_H;
GLuint screenShader;

// relation signs for comparisons (per function)
enum RelSign : uint32_t { REL_EQ=0, REL_LT=1, REL_LTE=2, REL_GT=3, REL_GTE=4 };

// boolean combinators for comparisons
enum BoolOp : uint32_t { BOOL_AND=0, BOOL_OR=1, BOOL_DIFF=2, BOOL_XOR=3 };

// Globals for VAO/VBO/EBO
static GLuint quadVAO = 0, quadVBO = 0, quadEBO = 0;

void initQuad() {
    if (quadVAO != 0) return; // already initialized

    // Vertex data for a full-screen quad (NDC)
    static const float quadVertices[] = {
        // positions   // uvs
        -1.0f, -1.0f, 0.0f, 0.0f, // bottom-left
        1.0f, -1.0f, 1.0f, 0.0f, // bottom-right
        1.0f,  1.0f, 1.0f, 1.0f, // top-right
        -1.0f,  1.0f, 0.0f, 1.0f  // top-left
    };

    static const unsigned int quadIndices[] = {
        0, 1, 2,
        2, 3, 0
    };

    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glGenBuffers(1, &quadEBO);

    glBindVertexArray(quadVAO);

    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, quadEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quadIndices), quadIndices, GL_STATIC_DRAW);

    // position attribute
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // uv attribute
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
}

void drawTexture(GLuint screenShader, GLuint texture, float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f) {
    glUseProgram(screenShader);

    // set uniform color
    GLint colorLoc = glGetUniformLocation(screenShader, "u_color");
    glUniform4f(colorLoc, r, g, b, a);

    // bind texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    GLint texLoc = glGetUniformLocation(screenShader, "u_tex");
    glUniform1i(texLoc, 0); // texture unit 0

    // draw quad
    glBindVertexArray(quadVAO);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

// Helper function to convert an expr to a string
std::string exprToString(const Expression& expr){
    // Stack to hold values
    std::vector<std::string> stack;
    stack.resize(1);
    int sp = 0;

    // Go through all the elements
    for (int i = 0; i < expr.size(); i++){
        if (expr[i].kind == Stack::EXPR_NUMBER){
            stack[sp++] = std::to_string(expr[i].number);
            stack.resize(sp+1);
        }
        else if (expr[i].kind == Stack::EXPR_VAR){
            if (expr[i].var != "y" && expr[i].var != "x"){
                stack[sp++] = std::to_string(getValue(expr[i].var));
                stack.resize(sp+1);
            }
            else {
                stack[sp++] = expr[i].var;
                stack.resize(sp+1);
            }
        }
        // Unary expressions, take the first element of the stack, and create a string like this: func(x), 
        // where func is the unary op, and x is the element we took off the stack
        else if (expr[i].kind == Stack::EXPR_UNARY){
            if (sp <= 0) return "";

            std::string top = stack[--sp];

            std::string func;
            switch (expr[i].op) {
                case OP_NEG:  func = "-"; break;
                case OP_ABS:  func = "abs"; break;
                case OP_SQRT: func = "sqrt"; break;
                case OP_LN:   func = "log"; break;
                case OP_LOG:  func = "log10"; break;
                case OP_SIN:  func = "sin"; break;
                case OP_COS:  func = "cos"; break;
                case OP_TAN:  func = "tan"; break;
                case OP_ASIN: func = "asin"; break;
                case OP_ACOS: func = "acos"; break;
                case OP_ATAN: func = "atan"; break;
                case OP_FLOOR: func = "floor"; break;
                default: return "";
            }

            std::string fullStr = func + "(" + top + ")";
            stack[sp++] = fullStr;
            stack.resize(sp+1);
        }
        // Get the top 2 elements of the stack. Syntax should look like this: (operand1 binary op operand2), eg. ((1 + x) + y)
        else if (expr[i].kind == Stack::EXPR_BINARY){
            if (sp <= 1) return "";

            std::string val1 = stack[--sp];
            std::string val2 = stack[--sp];

            std::string func = "";

            switch (expr[i].op) {
                case OP_ADD: func = "+"; break;
                case OP_SUB: func = "-"; break;
                case OP_MUL: func = "*"; break;
                case OP_DIV: func = "/"; break;
                case OP_POW: func = "^"; break;
                case OP_MOD: func = "%"; break;
                default: return "";
            }

            std::string fullStr;
            if (func == "^"){
                fullStr = "(powInt(" + val2 + ", " + val1 + "))";
            }
            else if (func == "%"){
                fullStr = "(mod(" + val2 + ", " + val1 + "))";
            }
            else {
                fullStr = "(" + val2 + " " + func + " " + val1 + ")";
            }
            stack[sp++] = fullStr;
            stack.resize(sp+1);
        }
    }

    if (sp <= 0) return "";

    return stack[sp-1];
}

// Return the index where we found the string
bool findStr(const std::string& sourceStr, const std::string& subStr, int& index){
    index = sourceStr.find(subStr, 0);

    return index != std::string::npos;
}

// A function to convert expressions to strings that I can append to the existing .glsl file and compile, so that I get an efficient
// evaluation function

// Returns the string for the new compute shader
std::string appendEvaluationFunction(const std::vector<Expression>& exprs){
    // 4 spaces
    std::string tab = "    ";

    std::string evalStr = LoadFile("shaders\\evaluate.comp.glsl");
    std::string insertStr = tab + "switch (funcIndex){\n";

    for (int i = 0; i < exprs.size(); i++){
        const auto& expr = exprs[i];

        if (expr.empty()) continue;

        std::string funcStr = exprToString(expr);
        insertStr.append(tab + tab + "case " + std::to_string(i) + "u: result = " + funcStr + "; break;\n");
    }

    insertStr.append(tab + "}\n");

    std::string target = "// SWITCH STATEMENT\n";
    int index;

    if (findStr(evalStr, target, index)){
        int appendTo = index + target.size();

        evalStr.insert(appendTo, insertStr);
    }

    return evalStr;
}

// --- Creating SSBOs and texture ---
GLuint ssboInstructions = 0, ssboOffsets = 0, ssboLengths = 0;
GLuint ssboGridSigns = 0, ssboComparisons = 0, ssboRelationSigns = 0, ssboColors = 0;

void createBuffersAndUpload(const std::vector<Comparison>& comparisons,
                            const std::vector<uint32_t>& relationSigns,
                            const std::vector<SDL_Color>& funcColors,
                            size_t funcCount){

    // Build color components as uint32 per-channel (r,g,b,a) to match GLSL uvec4
    std::vector<uint32_t> colorComponents;
    colorComponents.reserve(funcColors.size() * 4);
    for (const auto& c : funcColors){
        colorComponents.push_back((uint32_t)c.r);
        colorComponents.push_back((uint32_t)c.g);
        colorComponents.push_back((uint32_t)c.b);
        colorComponents.push_back((uint32_t)c.a);
    }

    // Grid signs SSBO (binding = 0) — zero-initialized
    if (ssboGridSigns) { glDeleteBuffers(1, &ssboGridSigns); ssboGridSigns = 0; }
    size_t totalCorners = funcCount * CORNER_W * CORNER_H;
    std::vector<uint32_t> zeroGrid(totalCorners ? totalCorners : 1, 0u);
    glGenBuffers(1, &ssboGridSigns);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboGridSigns);
    glBufferData(GL_SHADER_STORAGE_BUFFER, zeroGrid.size() * sizeof(uint32_t), zeroGrid.data(), GL_DYNAMIC_COPY);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssboGridSigns);

    // Comparisons SSBO (binding = 1)
    if (ssboComparisons) { glDeleteBuffers(1, &ssboComparisons); ssboComparisons = 0; }
    glGenBuffers(1, &ssboComparisons);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboComparisons);
    if (!comparisons.empty())
        glBufferData(GL_SHADER_STORAGE_BUFFER, comparisons.size()*sizeof(Comparison), comparisons.data(), GL_STATIC_DRAW);
    else
        glBufferData(GL_SHADER_STORAGE_BUFFER, 1, nullptr, GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssboComparisons);

    // Relation signs SSBO (binding = 2)
    if (ssboRelationSigns) { glDeleteBuffers(1, &ssboRelationSigns); ssboRelationSigns = 0; }
    glGenBuffers(1, &ssboRelationSigns);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboRelationSigns);
    if (!relationSigns.empty())
        glBufferData(GL_SHADER_STORAGE_BUFFER, relationSigns.size()*sizeof(uint32_t), relationSigns.data(), GL_STATIC_DRAW);
    else
        glBufferData(GL_SHADER_STORAGE_BUFFER, 1, nullptr, GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, ssboRelationSigns);

    // Colors SSBO (binding = 3) — now 4 uints per color (r,g,b,a) so GLSL's uvec4 colors[] lines up
    if (ssboColors) { glDeleteBuffers(1, &ssboColors); ssboColors = 0; }
    glGenBuffers(1, &ssboColors);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboColors);

    if (!colorComponents.empty())
        glBufferData(GL_SHADER_STORAGE_BUFFER, colorComponents.size()*sizeof(uint32_t), colorComponents.data(), GL_STATIC_DRAW);
    else
        glBufferData(GL_SHADER_STORAGE_BUFFER, 1, nullptr, GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, ssboColors);
}

GLuint outputTex = 0;
int outputW = 0, outputH = 0;

// --- Dispatching compute shaders ---
// shaderEvaluate: GLuint of the compiled compute shader program for pass 1
// shaderCombine: GLuint for pass 2
void runCompute(GLuint shaderEvaluate, GLuint shaderCombine, size_t funcCount, size_t comparisonCount, 
                float startX, float startY, float stepX, float stepY){

    if (outputTex == 0 || outputW != W || outputH != H) {
        glGenTextures(1, &outputTex);
        glBindTexture(GL_TEXTURE_2D, outputTex);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, W, H);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        outputW = W; outputH = H;
    }
    glBindImageTexture(0, outputTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
                        
    // No functions to render
    if (comparisonCount == 0) return;
                    
    // Bind program 1 (evaluate)
    glUseProgram(shaderEvaluate);

    // Setting the uniforms
    glUniform4f(glGetUniformLocation(shaderEvaluate, "u_screenParams"), startX, startY, stepX, stepY);
    glUniform2i(glGetUniformLocation(shaderEvaluate, "u_Res"), W, H);
    glUniform2i(glGetUniformLocation(shaderEvaluate, "u_CornerRes"), CORNER_W, CORNER_H);

    // Dispatch evaluate for all corner points per-function
    // Workgroup layout chosen in shader e.g. local_size_x=16, local_size_y=16
    int gx = (CORNER_W + 15) / 16;
    int gy = (CORNER_H + 15) / 16;
    int gz = (int)funcCount; // we dispatch z by funcCount (one slice per function)

    glDispatchCompute(gx, gy, gz);

    // Wait for SSBO writes to be visible to next stage
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);

    // Now run combine shader
    glUseProgram(shaderCombine);

    // bind any extra uniforms required (e.g. W,H)
    glUniform2i(glGetUniformLocation(shaderCombine, "u_res"), W, H);
    glUniform2i(glGetUniformLocation(shaderCombine, "u_cornerRes"), CORNER_W, CORNER_H);
    glUniform1ui(glGetUniformLocation(shaderCombine, "u_comparisonCount"), (GLuint)comparisonCount);

    int cx = (W + 15)/16;
    int cy = (H + 15)/16;
    
    glDispatchCompute(cx, cy, 1);

    // Make sure the image writes are finished before rendering
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT);

    drawTexture(screenShader, outputTex);
}

int initializeRendering(int window_width, int window_height){
    initQuad();

    W = window_width; H = window_height;
    CORNER_W = window_width + 1; CORNER_H = window_height + 1;
    std::string basePath = "shaders\\";

    // Used to render the graph
    screenShader = CreateProgram(CompileShader(LoadFile(basePath + "screenShader.vert"), GL_VERTEX_SHADER), 
                                        CompileShader(LoadFile(basePath + "screenShader.frag"), GL_FRAGMENT_SHADER));

    if (screenShader == -1) return -1;
    
    return 1;
}