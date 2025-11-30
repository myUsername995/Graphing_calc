// --- includes ---
#include <SDL3/SDL.h>
#include <vector>
#include <cstdint>
#include <cmath>
#include <fstream>
#include <sstream>
#include "GPU.hpp"

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 800

// --- useful consts ---
const int W = WINDOW_WIDTH;           // e.g. 1280
const int H = WINDOW_HEIGHT;          // e.g. 720
const int CORNER_W = W + 1;
const int CORNER_H = H + 1;

// --- GPU-side instruction layout (matches GLSL std430 alignment) ---
struct GPUInstruction {
    uint32_t kind;    // 0 = NUMBER, 1 = BINARY, 2 = VAR, 3 = UNARY
    uint32_t op;      // operation enum id
    float    number;  // if kind == NUMBER
    int32_t  var;     // 0 = none, 1 = x, 2 = y
}; // sizeof = 16 bytes (good for std430)

// Mirror these enums on the GLSL side!
enum InstrKind : uint32_t { EXPR_NUMBER = 0, EXPR_BINARY = 1, EXPR_VAR = 2, EXPR_UNARY = 3 };

// relation signs for comparisons (per function)
enum RelSign : uint32_t { REL_EQ=0, REL_LT=1, REL_LTE=2, REL_GT=3, REL_GTE=4 };

// boolean combinators for comparisons
enum BoolOp : uint32_t { BOOL_AND=0, BOOL_OR=1, BOOL_DIFF=2, BOOL_XOR=3 };

struct Comparison {
    uint32_t index1;
    uint32_t index2;
    uint32_t booleanOp; // BoolOp
    uint32_t padding;
};

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
}

// --- Dispatching compute shaders ---
// shaderEvaluate: GLuint of the compiled compute shader program for pass 1
// shaderCombine: GLuint for pass 2
void runCompute(GLuint shaderEvaluate, GLuint shaderCombine, size_t funcCount){
    // Bind program 1 (evaluate)
    glUseProgram(shaderEvaluate);

    // push uniforms: world transform params (startX, startY, stepX, stepY, W, H)
    // You need to set these uniforms / an SSBO for screen params. Example:
    GLint loc = glGetUniformLocation(shaderEvaluate, "u_screenParams");
    GLint loc = glGetUniformLocation(shaderEvaluate, "u_res");
    GLint loc = glGetUniformLocation(shaderEvaluate, "u_cornerRes");
    GLint loc = glGetUniformLocation(shaderEvaluate, "u_instrCount");
    GLint loc = glGetUniformLocation(shaderEvaluate, "u_funcCount");

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

    // Bind output image is already bound to image unit 0
    // bind any extra uniforms required (e.g. W,H)
    int cx = (W + 15)/16;
    int cy = (H + 15)/16;
    glDispatchCompute(cx, cy, 1);

    // Make sure the image writes are finished before rendering
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT);
}

std::string LoadFile(const std::string& path) {
    std::ifstream file(path);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

GLuint CompileShader(const std::string& source, GLenum shaderType) {
    GLuint shader = glCreateShader(shaderType);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    // Error checking
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        std::cerr << "Shader compile error:\n" << infoLog << std::endl;
    }
    return shader;
}

GLuint CreateProgram(GLuint vert, GLuint frag) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);

    // Error checking
    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(program, 512, nullptr, infoLog);
        std::cerr << "Program link error:\n" << infoLog << std::endl;
    }
    return program;
}

// Later: render outputTex as usual (simple textured quad)
