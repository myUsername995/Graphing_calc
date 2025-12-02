#pragma once
#include <glad/glad.h>
#include "expression.hpp"

// --- GPU-side instruction layout (matches GLSL std430 alignment) ---
struct GPUInstruction {
    uint32_t kind;    // 0 = NUMBER, 1 = BINARY, 2 = VAR, 3 = UNARY
    uint32_t op;      // operation enum id
    float    number;  // if kind == NUMBER
    int32_t  var;     // 0 = none, 1 = x, 2 = y
}; // sizeof = 16 bytes (good for std430)

struct Comparison {
    uint32_t index1;
    uint32_t index2;
    uint32_t booleanOp; // BoolOp
    uint32_t padding;
};

void packExpressionsToGPU(const std::vector<Expression>& exprs,
                          std::vector<GPUInstruction>& outInstrs,
                          std::vector<uint32_t>& outOffsets,
                          std::vector<uint32_t>& outLengths);

void createBuffersAndUpload(const std::vector<GPUInstruction>& instrs,
                            const std::vector<uint32_t>& offsets,
                            const std::vector<uint32_t>& lengths,
                            const std::vector<Comparison>& comparisons,
                            const std::vector<uint32_t>& relationSigns,
                            const std::vector<SDL_Color>& funcColors,
                            size_t funcCount);

void runCompute(GLuint shaderEvaluate, GLuint shaderCombine, GLuint screenShader, size_t funcCount, size_t comparisonCount, 
                float startX, float startY, float stepX, float stepY);