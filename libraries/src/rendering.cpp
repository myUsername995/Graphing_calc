// --- includes ---
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <vector>
#include <cstdint>
#include <cmath>
#include <fstream>
#include <sstream>
#include "rendering.hpp"

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 800

// --- useful consts ---
const int W = WINDOW_WIDTH;           // e.g. 1280
const int H = WINDOW_HEIGHT;          // e.g. 720
const int CORNER_W = W + 1;
const int CORNER_H = H + 1;

// Mirror these enums on the GLSL side!
enum InstrKind : uint32_t { EXPR_NUMBER = 0, EXPR_BINARY = 1, EXPR_VAR = 2, EXPR_UNARY = 3 };

// relation signs for comparisons (per function)
enum RelSign : uint32_t { REL_EQ=0, REL_LT=1, REL_LTE=2, REL_GT=3, REL_GTE=4 };

// boolean combinators for comparisons
enum BoolOp : uint32_t { BOOL_AND=0, BOOL_OR=1, BOOL_DIFF=2, BOOL_XOR=3 };

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

// Globals for VAO/VBO/EBO
static GLuint quadVAO = 0, quadVBO = 0, quadEBO = 0;

void initQuad() {
    if (quadVAO != 0) return; // already initialized

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

void drawTexture(GLuint screenShader, GLuint texture, float r = 1.0f, float g = 0.0f, float b = 0.0f, float a = 0.5f) {
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
GLuint ssboGridSigns = 0, ssboComparisons = 0, ssboRelationSigns = 0;
GLuint outputTex = 0;

void createBuffersAndUpload(const std::vector<GPUInstruction>& instrs,
                            const std::vector<uint32_t>& offsets,
                            const std::vector<uint32_t>& lengths,
                            const std::vector<Comparison>& comparisons,
                            const std::vector<uint32_t>& relationSigns,
                            size_t funcCount){
    // Instructions SSBO
    glGenBuffers(1, &ssboInstructions);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboInstructions);
    glBufferData(GL_SHADER_STORAGE_BUFFER, instrs.size()*sizeof(GPUInstruction), instrs.data(), GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssboInstructions); // binding 0

    // Offsets
    glGenBuffers(1, &ssboOffsets);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboOffsets);
    glBufferData(GL_SHADER_STORAGE_BUFFER, offsets.size()*sizeof(uint32_t), offsets.data(), GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssboOffsets); // binding 1

    // Lengths
    glGenBuffers(1, &ssboLengths);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboLengths);
    glBufferData(GL_SHADER_STORAGE_BUFFER, lengths.size()*sizeof(uint32_t), lengths.data(), GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, ssboLengths); // binding 2

    // Grid signs SSBO: uint per corner per function
    size_t totalCorners = funcCount * CORNER_W * CORNER_H;
    std::vector<uint32_t> zeroGrid(totalCorners, 0u);
    glGenBuffers(1, &ssboGridSigns);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboGridSigns);
    glBufferData(GL_SHADER_STORAGE_BUFFER, totalCorners * sizeof(uint32_t), zeroGrid.data(), GL_DYNAMIC_COPY);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, ssboGridSigns); // binding 3

    // Comparisons SSBO
    glGenBuffers(1, &ssboComparisons);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboComparisons);
    glBufferData(GL_SHADER_STORAGE_BUFFER, comparisons.size()*sizeof(Comparison), comparisons.data(), GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, ssboComparisons); // binding 4

    // RelationSigns per function
    glGenBuffers(1, &ssboRelationSigns);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboRelationSigns);
    glBufferData(GL_SHADER_STORAGE_BUFFER, relationSigns.size()*sizeof(uint32_t), relationSigns.data(), GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, ssboRelationSigns); // binding 5

    // Output image texture
    glGenTextures(1, &outputTex);
    glBindTexture(GL_TEXTURE_2D, outputTex);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, W, H);
    glBindImageTexture(0, outputTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8); // image unit 0   
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
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

// --- Dispatching compute shaders ---
// shaderEvaluate: GLuint of the compiled compute shader program for pass 1
// shaderCombine: GLuint for pass 2
void runCompute(GLuint shaderEvaluate, GLuint shaderCombine, GLuint screenShader, size_t funcCount, size_t comparisonCount, 
                float startX, float startY, float stepX, float stepY){
    // Bind program 1 (evaluate)
    glUseProgram(shaderEvaluate);

    // Setting the uniforms
    glUniform4f(glGetUniformLocation(shaderEvaluate, "u_screenParams"), startX, startY, stepX, stepY);
    glUniform2i(glGetUniformLocation(shaderEvaluate, "u_res"), W, H);
    glUniform2i(glGetUniformLocation(shaderEvaluate, "u_cornerRes"), CORNER_W, CORNER_H);

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