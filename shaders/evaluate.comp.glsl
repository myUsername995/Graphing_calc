#version 430

// expr() function will be pasted into here ^^^^^^^^^^ everytime we compile new expressions

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(std430, binding = 0) writeonly buffer GridSSBO {
    // one uint per corner sample (0 = NEGATIVE, 1 = POSITIVE)
    uint grid[]; // size = funcCount * CORNER_W * CORNER_H
};

// Screen params as uniforms
uniform ivec2 u_Res;         // (W, H)
uniform ivec2 u_CornerRes;   // (W+1, H+1)

void main(){
    ivec2 gid = ivec2(gl_GlobalInvocationID.xy); // corner x,y
    if (gid.x >= u_CornerRes.x || gid.y >= u_CornerRes.y) return;
    float x = gid.x;
    float y = gid.y;

    uint funcIndex = uint(gl_WorkGroupID.z);     // we dispatched gz = funcCount

    float result = 0;
    // Use this comment so that we can insert the right string to recompile the file right
    // SWITCH STATEMENT

    uint bigArrayIndex = funcIndex * uint(u_CornerRes.x * u_CornerRes.y);
    uint row = uint(u_CornerRes.y - 1 - gid.y);
    uint smallArrayIndex = row * uint(u_CornerRes.x) + uint(gid.x);

    if (smallArrayIndex >= uint(u_CornerRes.x * u_CornerRes.y)) return;

    uint idx = bigArrayIndex + smallArrayIndex;
    grid[idx] = (result <= 0) ? 0u : 1u;
}