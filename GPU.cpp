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

void GPURenderLine(GLuint shader, float x1, float y1, float x2, float y2, SDL_Color color){
    // convert screen coords (0..W/H) to NDC (-1..1)
    float ndcVerts[4] = {
        2.0f*x1/W - 1.0f, 1.0f - 2.0f*y1/H,
        2.0f*x2/W - 1.0f, 1.0f - 2.0f*y2/H
    };

    GLuint vao, vbo;
    glGenVertexArrays(1,&vao);
    glGenBuffers(1,&vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glBufferData(GL_ARRAY_BUFFER,sizeof(ndcVerts),ndcVerts,GL_DYNAMIC_DRAW);

    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),0);
    glEnableVertexAttribArray(0);

    glUseProgram(shader);

    glUniform4f(glGetUniformLocation(shader,"u_color"),
                color.r/255.0f, color.g/255.0f, color.b/255.0f, color.a/255.0f);

    glDrawArrays(GL_LINES,0,2);

    glDeleteBuffers(1,&vbo);
    glDeleteVertexArrays(1,&vao);
}

void GPURenderRect(GLuint shader, float x, float y, float w, float h, SDL_Color color, bool filled){
    float verts[12]; // 2 triangles for filled
    if (filled) {
        verts[0]  = 2.0f*x/W - 1.0f;      verts[1]  = 1.0f - 2.0f*y/H;
        verts[2]  = 2.0f*(x+w)/W - 1.0f;  verts[3]  = 1.0f - 2.0f*y/H;
        verts[4]  = 2.0f*(x+w)/W - 1.0f;  verts[5]  = 1.0f - 2.0f*(y+h)/H;

        verts[6]  = 2.0f*x/W - 1.0f;      verts[7]  = 1.0f - 2.0f*y/H;
        verts[8]  = 2.0f*(x+w)/W - 1.0f;  verts[9]  = 1.0f - 2.0f*(y+h)/H;
        verts[10] = 2.0f*x/W - 1.0f;      verts[11] = 1.0f - 2.0f*(y+h)/H;
    } else {
        float tmp[8] = {
            x,     y,
            x+w,   y,
            x+w,   y+h,
            x,     y+h
        };
        for(int i=0;i<4;i++){
            verts[i*2]   = 2.0f*tmp[i*2]/W - 1.0f;
            verts[i*2+1] = 1.0f - 2.0f*tmp[i*2+1]/H;
        }
    }

    GLuint vao, vbo;
    glGenVertexArrays(1,&vao);
    glGenBuffers(1,&vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glBufferData(GL_ARRAY_BUFFER, filled?sizeof(verts):8*sizeof(float), verts, GL_DYNAMIC_DRAW);

    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),0);
    glEnableVertexAttribArray(0);

    glUseProgram(shader);

    glUniform4f(glGetUniformLocation(shader,"u_color"),
                color.r/255.0f, color.g/255.0f, color.b/255.0f, color.a/255.0f);

    if (filled)
        glDrawArrays(GL_TRIANGLES,0,6);
    else
        glDrawArrays(GL_LINE_LOOP,0,4);

    glDeleteBuffers(1,&vbo);
    glDeleteVertexArrays(1,&vao);
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

GLuint GPUCreateTextTexture(TTF_Font* font, const std::string& text, int& w, int& h){
    SDL_Surface* surf = TTF_RenderText_Blended(font, text.c_str(), text.length(), {127,127,255,255});
    if (!surf) return 0;

    w = surf->w;
    h = surf->h;

    // Convert to RGBA8888
    SDL_Surface* rgbaSurf = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA8888);
    SDL_DestroySurface(surf);  // free the original

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    // USE rgbaSurf HERE
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rgbaSurf->w, rgbaSurf->h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgbaSurf->pixels);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    SDL_DestroySurface(rgbaSurf);
    return tex;
}

void GPURenderText(GLuint shader, GLuint tex, float x, float y, int w, int h, SDL_Color color){
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