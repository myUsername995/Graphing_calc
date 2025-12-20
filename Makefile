# Compiler
CXX = g++
CXXFLAGS = -O0 -g

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
INCLUDES = -I"C:/Users/user/OneDrive/Desktop/gameszko/include" \
           -I"./libraries/header" \
           -I"."


# Library dirs and libs
LIBS = -L"C:/Users/user/OneDrive/Desktop/gameszko/lib" \
       -lSDL3 -lSDL3_ttf

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC) $(INCLUDES) $(LIBS)

clean:
	del /Q $(TARGET)
