#version 430

// expr() function will be pasted into here everytime we compile new expressions

// PASS 1: evaluate expressions at every corner sample
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(std430, binding = 3) writeonly buffer GridSSBO {
    // one uint per corner sample (0 = NEGATIVE, 1 = POSITIVE)
    uint grid[]; // size = funcCount * CORNER_W * CORNER_H
};

// Screen params as uniforms
uniform ivec2 u_Res;         // (W, H)
uniform ivec2 u_CornerRes;   // (W+1, H+1)

void main(){
    
}
