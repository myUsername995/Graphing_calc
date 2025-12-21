# Compiler
CXX = g++
CXXFLAGS = -O3 -g

# Output name
TARGET = grapher.exe

# Source files
SRC = grapher.cpp \
      libraries/src/expression.cpp \
      libraries/src/time.cpp \
      libraries/src/rendering.cpp \
      libraries/src/GPU.cpp \
      glad/glad.c

# Include directories
INCLUDES = -I"C:/Files/.vscode/include" \
           -I"./libraries/header" \
           -I"."


# Library directories and libraries
LIBS = -L"C:/Files/.vscode/lib" \
       -lSDL3 -lSDL3_ttf

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC) $(INCLUDES) $(LIBS)

clean:
	del /Q $(TARGET)
