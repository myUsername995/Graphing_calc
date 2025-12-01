// --- includes ---
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <vector>
#include <cstdint>
#include <cmath>
#include <fstream>
#include <sstream>
#include <iostream>
#include "GPU.hpp"

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 800

// --- useful consts ---
const int W = WINDOW_WIDTH;           // e.g. 1280
const int H = WINDOW_HEIGHT;          // e.g. 720

GLuint shapeShader;
GLuint textShader;

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

GLuint CreateComputeProgram(GLuint computeShader){
    GLuint program = glCreateProgram();
    glAttachShader(program, computeShader);
    glLinkProgram(program);

    GLint ok;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(program, 1024, NULL, log);
        printf("Compute shader link error:\n%s\n", log);
    }

    glDetachShader(program, computeShader);

    return program;
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

void GPURenderLine(SDL_FPoint p1, SDL_FPoint p2, SDL_Color color){
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

void GPURenderRect(SDL_FRect pos, SDL_Color color, bool filled){
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

void GPURenderTextTexture(GLuint tex, SDL_FRect pos, SDL_Color color){
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

    glUseProgram(textShader);
    glUniform4f(glGetUniformLocation(textShader,"u_color"),
                color.r/255.0f, color.g/255.0f, color.b/255.0f, color.a/255.0f);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(glGetUniformLocation(textShader,"u_tex"),0);

    glBindVertexArray(textVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

SDL_FRect GPURenderText(TTF_Font* font, const std::string& str, SDL_FPoint pos, SDL_Color color){ 
    SDL_Surface* surface = TTF_RenderText_Blended(font, str.c_str(), str.length(), color); 
    if (!surface) return {pos.x, pos.y, 0, 0}; 

    SDL_Surface* rgbaSurf = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA8888); 
    SDL_DestroySurface(surface); 

    int w = rgbaSurf->w; 
    int h = rgbaSurf->h; 

    GLuint tex; 
    glGenTextures(1, &tex); 
    glBindTexture(GL_TEXTURE_2D, tex); 

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgbaSurf->pixels); 

    // Set swizzle so the shader sees Alpha in RED
    GLint swizzleMask[] = { GL_ZERO, GL_ZERO, GL_ZERO, GL_RED };
    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); 
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); 
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); 
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    SDL_DestroySurface(rgbaSurf); 

    GPURenderTextTexture(tex, {pos.x, pos.y, float(w), float(h)}, color);    
    glDeleteTextures(1, &tex);

    return {pos.x, pos.y, float(w), float(h)}; 
}

void createShaders(){
    // shape.frag
    const char* shape_frag = 
    "#version 430 core\n"
    "in vec4 v_color;\n"
    "out vec4 FragColor;\n"
    "\n"
    "void main() {\n"
    "    FragColor = v_color;\n"
    "}\n";

    // shape.vert
    const char* shape_vert = 
    "#version 430 core\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "\n"
    "uniform vec4 u_color;\n"
    "\n"
    "out vec4 v_color;\n"
    "\n"
    "void main() {\n"
    "    v_color = u_color;\n"
    "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

    // text.frag
    const char* text_frag = 
    "#version 430 core\n"
    "\n"
    "in vec2 v_uv;\n"
    "out vec4 FragColor;\n"
    "\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec4 u_color;\n"
    "\n"
    "void main() {\n"
    "    float alpha = texture(u_tex, v_uv).a;\n"
    "    FragColor = vec4(u_color.rgb, u_color.a * alpha);\n"
    "}\n";

    // text.vert
    const char* text_vert = 
    "#version 430 core\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "layout(location = 1) in vec2 a_uv;\n"
    "\n"
    "out vec2 v_uv;\n"
    "\n"
    "void main() {\n"
    "    v_uv = a_uv;\n"
    "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

    std::string LoadFile(const std::string& path);
    GLuint CompileShader(const std::string& source, GLenum shaderType);
    GLuint CreateProgram(GLuint vert, GLuint frag);
    GLuint CreateComputeProgram(GLuint computeShader);

    shapeShader = CreateProgram(CompileShader(shape_frag, GL_FRAGMENT_SHADER), 
                                    CompileShader(shape_vert, GL_VERTEX_SHADER));

    textShader = CreateProgram(CompileShader(text_frag, GL_FRAGMENT_SHADER), 
                                    CompileShader(text_vert, GL_VERTEX_SHADER));
}

void initalizeGPU(SDL_Window* window){
    SDL_GLContext glctx = SDL_GL_CreateContext(window);

    SDL_GL_MakeCurrent(window, glctx);
    gladLoadGL();
    initTextQuad();
    initShapeRenderer();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    createShaders();
}