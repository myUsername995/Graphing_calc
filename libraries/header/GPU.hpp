#pragma once
#include <glad/glad.h>
#include <string>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

// Creating programs
std::string LoadFile(const std::string& path);
GLuint CompileShader(const std::string& source, GLenum shaderType);
GLuint CreateProgram(GLuint vert, GLuint frag);
GLuint CreateComputeProgram(GLuint computeShader);

// Graphics
void GPURenderLine(SDL_FPoint p1, SDL_FPoint p2, SDL_Color color);
void GPURenderRect(SDL_FRect pos, SDL_Color color, bool filled);
SDL_FRect GPURenderText(TTF_Font* font, const std::string& str, SDL_FPoint pos, SDL_Color color); // Render text

// Initialisation
int initalizeGPU(SDL_Window* window, int window_width, int window_height);
void GPUResizeWindow(int window_width, int window_height);