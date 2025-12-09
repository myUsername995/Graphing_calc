/*
A library to graph functions using compute shaders on the GPU.

There are 3 main functions, with the other ones are helper functions:
-packExpressionsToGPU() -> takes a std::vector<> of Expr structs, which store expressions in a stack (array), and converts it into 
an array of GPUInstructions that gets uploaded to the GPU later

-createBuffersAndUpload() -> creates buffers for the compute shader and uploads them. 
0: array of whole expressions, essentially all the expressions the user inputted. 
1: The offsets between the different expressions in the 1st array. 
2: The lengths of each expression in the 1st array. 
3: A 3D array of 2D arrays that stores every corner on the screen, each corner can be negative, zero or positive. 
4: Comparisons array used to compare between functions. Every element has three attributes: index1, index2, boolean operator 
(between the first 2 functions). 
5: Relation signs array, used to store all the relation signs for each function. 
6: Colors array, used to store the colors of each function

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
GLuint shaderEvaluate, shaderCombine, screenShader;

// Mirror these enums on the GLSL side!
enum InstrKind : uint32_t { EXPR_NUMBER = 0, EXPR_BINARY = 1, EXPR_VAR = 2, EXPR_UNARY = 3 };

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
    initQuad(); // make sure VAO/VBO/EBO are set up

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

// --- Creating SSBOs and texture ---
GLuint ssboInstructions = 0, ssboOffsets = 0, ssboLengths = 0;
GLuint ssboGridSigns = 0, ssboComparisons = 0, ssboRelationSigns = 0, ssboColors = 0;

void createBuffersAndUpload(const std::vector<GPUInstruction>& instrs,
                            const std::vector<uint32_t>& offsets,
                            const std::vector<uint32_t>& lengths,
                            const std::vector<Comparison>& comparisons,
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

    // Instructions SSBO (binding = 0)
    if (ssboInstructions) { glDeleteBuffers(1, &ssboInstructions); ssboInstructions = 0; }
    glGenBuffers(1, &ssboInstructions);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboInstructions);
    if (!instrs.empty())
        glBufferData(GL_SHADER_STORAGE_BUFFER, instrs.size()*sizeof(GPUInstruction), instrs.data(), GL_STATIC_DRAW);
    else
        glBufferData(GL_SHADER_STORAGE_BUFFER, 1, nullptr, GL_STATIC_DRAW); // avoid zero-size
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssboInstructions);

    // Offsets SSBO (binding = 1)
    if (ssboOffsets) { glDeleteBuffers(1, &ssboOffsets); ssboOffsets = 0; }
    glGenBuffers(1, &ssboOffsets);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboOffsets);
    if (!offsets.empty())
        glBufferData(GL_SHADER_STORAGE_BUFFER, offsets.size()*sizeof(uint32_t), offsets.data(), GL_STATIC_DRAW);
    else
        glBufferData(GL_SHADER_STORAGE_BUFFER, 1, nullptr, GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssboOffsets);

    // Lengths SSBO (binding = 2)
    if (ssboLengths) { glDeleteBuffers(1, &ssboLengths); ssboLengths = 0; }
    glGenBuffers(1, &ssboLengths);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboLengths);
    if (!lengths.empty())
        glBufferData(GL_SHADER_STORAGE_BUFFER, lengths.size()*sizeof(uint32_t), lengths.data(), GL_STATIC_DRAW);
    else
        glBufferData(GL_SHADER_STORAGE_BUFFER, 1, nullptr, GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, ssboLengths);

    // Grid signs SSBO (binding = 3) — zero-initialized
    if (ssboGridSigns) { glDeleteBuffers(1, &ssboGridSigns); ssboGridSigns = 0; }
    size_t totalCorners = funcCount * CORNER_W * CORNER_H;
    std::vector<uint32_t> zeroGrid(totalCorners ? totalCorners : 1, 0u);
    glGenBuffers(1, &ssboGridSigns);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboGridSigns);
    glBufferData(GL_SHADER_STORAGE_BUFFER, zeroGrid.size() * sizeof(uint32_t), zeroGrid.data(), GL_DYNAMIC_COPY);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, ssboGridSigns);

    // Comparisons SSBO (binding = 4)
    if (ssboComparisons) { glDeleteBuffers(1, &ssboComparisons); ssboComparisons = 0; }
    glGenBuffers(1, &ssboComparisons);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboComparisons);
    if (!comparisons.empty())
        glBufferData(GL_SHADER_STORAGE_BUFFER, comparisons.size()*sizeof(Comparison), comparisons.data(), GL_STATIC_DRAW);
    else
        glBufferData(GL_SHADER_STORAGE_BUFFER, 1, nullptr, GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, ssboComparisons);

    // Relation signs SSBO (binding = 5)
    if (ssboRelationSigns) { glDeleteBuffers(1, &ssboRelationSigns); ssboRelationSigns = 0; }
    glGenBuffers(1, &ssboRelationSigns);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboRelationSigns);
    if (!relationSigns.empty())
        glBufferData(GL_SHADER_STORAGE_BUFFER, relationSigns.size()*sizeof(uint32_t), relationSigns.data(), GL_STATIC_DRAW);
    else
        glBufferData(GL_SHADER_STORAGE_BUFFER, 1, nullptr, GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, ssboRelationSigns);

    // Colors SSBO (binding = 6) — now 4 uints per color (r,g,b,a) so GLSL's uvec4 colors[] lines up
    if (ssboColors) { glDeleteBuffers(1, &ssboColors); ssboColors = 0; }
    glGenBuffers(1, &ssboColors);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboColors);

    if (!colorComponents.empty())
        glBufferData(GL_SHADER_STORAGE_BUFFER, colorComponents.size()*sizeof(uint32_t), colorComponents.data(), GL_STATIC_DRAW);
    else
        glBufferData(GL_SHADER_STORAGE_BUFFER, 1, nullptr, GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, ssboColors);
}

// --- CPU helper to flatten Expression -> GPUInstruction vector ---
// assuming Expression is std::vector<Stack> from your parser
// also build offsets & lengths arrays per-expression
void packExpressionsToGPU(const std::vector<Expression>& exprs,
                          std::vector<GPUInstruction>& outInstrs,
                          std::vector<uint32_t>& outOffsets,
                          std::vector<uint32_t>& outLengths){
    outInstrs.clear();
    outOffsets.resize(exprs.size());
    outLengths.resize(exprs.size());

    for(int i = 0; i < exprs.size(); i++){
        const Expression &ex = exprs[i];
        outOffsets[i] = (uint32_t)outInstrs.size();
        outLengths[i] = (uint32_t)ex.size();
        for(const auto &s : ex){
            GPUInstruction g;
            g.kind = (uint32_t)s.kind;
            g.op = (uint32_t)s.op;
            g.number = (float)s.number;
            if (s.kind == Stack::EXPR_VAR) {
                g.var = (s.var == 'x') ? 1 : 2;
            } else g.var = 0;

            outInstrs.push_back(g);
        }
    }
}

GLuint outputTex = 0;
int outputW = 0, outputH = 0;

// --- Dispatching compute shaders ---
// shaderEvaluate: GLuint of the compiled compute shader program for pass 1
// shaderCombine: GLuint for pass 2
void runCompute(size_t funcCount, size_t comparisonCount, float startX, float startY, float stepX, float stepY){
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
    W = window_width; H = window_height;
    CORNER_W = window_width + 1; CORNER_H = window_height + 1;
    std::string basePath = "C:\\Files\\Cpp_files\\silly\\Grapher\\shaders\\";

    // Used to evaluate the function
    shaderEvaluate = CreateComputeProgram(CompileShader(LoadFile(basePath + "evaluate.comp.glsl"), GL_COMPUTE_SHADER));
    shaderCombine = CreateComputeProgram(CompileShader(LoadFile(basePath + "combine.comp.glsl"), GL_COMPUTE_SHADER));

    // Used to render the graph
    screenShader = CreateProgram(CompileShader(LoadFile(basePath + "screenShader.vert"), GL_VERTEX_SHADER), 
                                        CompileShader(LoadFile(basePath + "screenShader.frag"), GL_FRAGMENT_SHADER));

    if (shaderEvaluate == -1 || shaderCombine == -1 || screenShader == -1) return -1;

    return 1;
}