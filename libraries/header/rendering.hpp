#pragma once
#include <glad/glad.h>
#include "expression.hpp"

struct Comparison {
    uint32_t index1;
    uint32_t index2;
    uint32_t booleanOp;
    uint32_t padding;
};

std::string appendEvaluationFunction(const std::vector<Expression>& exprs);

void createBuffersAndUpload(const std::vector<Comparison>& comparisons,
                            const std::vector<uint32_t>& relationSigns,
                            const std::vector<SDL_Color>& funcColors,
                            size_t funcCount);

void runCompute(GLuint shaderEvaluate, GLuint shaderCombine, size_t funcCount, size_t comparisonCount, 
                float startX, float startY, float stepX, float stepY);

// Call this before calling any other functions
int initializeRendering(int window_width, int window_height);