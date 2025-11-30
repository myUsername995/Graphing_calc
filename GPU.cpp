// --- includes ---
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
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

// Mirror these enums on the GLSL side!
enum InstrKind : uint32_t { EXPR_NUMBER = 0, EXPR_BINARY = 1, EXPR_VAR = 2, EXPR_UNARY = 3 };

// relation signs for comparisons (per function)
enum RelSign : uint32_t { REL_EQ=0, REL_LT=1, REL_LTE=2, REL_GT=3, REL_GTE=4 };

// boolean combinators for comparisons
enum BoolOp : uint32_t { BOOL_AND=0, BOOL_OR=1, BOOL_DIFF=2, BOOL_XOR=3 };

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
void runCompute(GLuint shaderEvaluate, GLuint shaderCombine, size_t funcCount, size_t comparisonCount, GLuint outTex, 
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

    glBindImageTexture(
        0,           // image unit = 0, matches "binding = 0"
        outTex,      // the texture name
        0,           // mip level
        GL_FALSE,    // not layered
        0,           // layer
        GL_WRITE_ONLY,
        GL_RGBA8     // format, must match shader
    );

    glUniform2i(glGetUniformLocation(shaderCombine, "u_res"), W, H);
    glUniform2i(glGetUniformLocation(shaderCombine, "u_cornerRes"), CORNER_W, CORNER_H);
    glUniform1ui(glGetUniformLocation(shaderCombine, "u_comparisonCount"), comparisonCount);

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

ScreenQuad createScreenQuadAndTexture() {
    ScreenQuad sq{};

    // -------- texture --------
    glGenTextures(1, &sq.texture);
    glBindTexture(GL_TEXTURE_2D, sq.texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    // -------- quad VBO/VAO --------
    float quadVerts[] = {
        // pos      // uv
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f,

        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f,
        -1.0f,  1.0f,  0.0f, 1.0f
    };

    glGenVertexArrays(1, &sq.vao);
    glGenBuffers(1, &sq.vbo);

    glBindVertexArray(sq.vao);

    glBindBuffer(GL_ARRAY_BUFFER, sq.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glBindVertexArray(0);

    glBindImageTexture(0, sq.texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

    return sq;
}

void drawTexture(GLuint shader, GLuint texture, GLuint vao) {
    glUseProgram(shader);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(glGetUniformLocation(shader, "u_tex"), 0);

    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

// --- Global GPU shape renderer state ---
GLuint gShapeVAO = 0;
GLuint gShapeVBO = 0;

void initShapeRenderer(){
    glGenVertexArrays(1, &gShapeVAO);
    glGenBuffers(1, &gShapeVBO);

    glBindVertexArray(gShapeVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gShapeVBO);

    // Allocate enough space for ALL shapes (lines + rectangles)
    glBufferData(GL_ARRAY_BUFFER, 1024 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), 0);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

GLuint textVAO, textVBO;

void initTextQuad() {
    float verts[24];
    glGenVertexArrays(1, &textVAO);
    glGenBuffers(1, &textVBO);

    glBindVertexArray(textVAO);
    glBindBuffer(GL_ARRAY_BUFFER, textVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), nullptr, GL_DYNAMIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
}

inline float sx(float x) { return 2.0f * x / W - 1.0f; }
inline float sy(float y) { return 1.0f - 2.0f * y / H; }

void GPURenderLine(GLuint shapeShader, SDL_FPoint p1, SDL_FPoint p2, SDL_Color color){
    float x1 = p1.x; float y1 = p1.y; float x2 = p2.x; float y2 = p2.y;

    float verts[] = {
        sx(x1), sy(y1),
        sx(x2), sy(y2)
    };

    glUseProgram(shapeShader);

    glUniform4f(glGetUniformLocation(shapeShader, "u_color"),
                color.r/255.0f, color.g/255.0f, color.b/255.0f, color.a/255.0f);

    glBindVertexArray(gShapeVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gShapeVBO);

    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

    glDrawArrays(GL_LINES, 0, 2);

    glBindVertexArray(0);
}

void GPURenderRect(GLuint shapeShader, SDL_FRect pos, SDL_Color color, bool filled){
    float x = pos.x; float y = pos.y; float w = pos.w; float h = pos.h;

    glUseProgram(shapeShader);

    glUniform4f(glGetUniformLocation(shapeShader, "u_color"),
                color.r/255.0f, color.g/255.0f, color.b/255.0f, color.a/255.0f);

    glBindVertexArray(gShapeVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gShapeVBO);

    if (filled){
        float verts[] = {
            sx(x),     sy(y),
            sx(x+w),   sy(y),
            sx(x+w),   sy(y+h),

            sx(x),     sy(y),
            sx(x+w),   sy(y+h),
            sx(x),     sy(y+h)
        };

        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    else
    {
        float verts[] = {
            sx(x),     sy(y),
            sx(x+w),   sy(y),
            sx(x+w),   sy(y+h),
            sx(x),     sy(y+h)
        };

        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
        glDrawArrays(GL_LINE_LOOP, 0, 4);
    }

    glBindVertexArray(0);
}

void GPURenderText(GLuint shader, GLuint tex, SDL_FRect pos, SDL_Color color){
    float x = pos.x; float y = pos.y; float w = pos.w; float h = pos.h;

    // Build vertices in NDC
    float verts[] = {
        2.0f * x / float(W) - 1.0f, 1.0f - 2.0f * y / float(H), 0.0f, 0.0f,
        2.0f * (x + w) / float(W) - 1.0f, 1.0f - 2.0f * y / float(H), 1.0f, 0.0f,
        2.0f * (x + w) / float(W) - 1.0f, 1.0f - 2.0f * (y + h) / float(H), 1.0f, 1.0f,
        2.0f * x / float(W) - 1.0f, 1.0f - 2.0f * y / float(H), 0.0f, 0.0f,
        2.0f * (x + w) / float(W) - 1.0f, 1.0f - 2.0f * (y + h) / float(H), 1.0f, 1.0f,
        2.0f * x / float(W) - 1.0f, 1.0f - 2.0f * (y + h) / float(H), 0.0f, 1.0f
    };

    glBindBuffer(GL_ARRAY_BUFFER, textVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

    glUseProgram(shader);
    glUniform4f(glGetUniformLocation(shader,"u_color"),
                color.r/255.0f, color.g/255.0f, color.b/255.0f, color.a/255.0f);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(glGetUniformLocation(shader,"u_tex"),0);

    glBindVertexArray(textVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}