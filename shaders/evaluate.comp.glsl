#version 430

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(std430, binding = 0) writeonly buffer GridSSBO {
    // one uint per corner sample (0 = NEGATIVE, 1 = POSITIVE)
    uint grid[]; // size = funcCount * CORNER_W * CORNER_H
};

float powInt(float base, float exp){
    float r = round(exp);

    if (abs(exp - r) < 1e-4 && abs(r) <= 32.0) {
        // 0^0 → define as 1
        if (exp == 0)
            return 1.0;

        // Avoid division by zero for negative exponents
        if (base == 0.0)
            return 0.0;

        float result = 1.0;

        // Fixed upper bound so GLSL can unroll the loop
        for (int i = 0; i < 32; ++i) {
            if (i >= r) break;
            result *= base;
        }

        return exp < 0 ? 1.0 / result : result;
    }
    else {
        return pow(base, exp);
    }
}

// Screen params as uniforms
uniform vec4 u_screenParams;
uniform ivec2 u_Res;         // (W, H)
uniform ivec2 u_CornerRes;   // (W+1, H+1)

void main(){
    ivec2 gid = ivec2(gl_GlobalInvocationID.xy); // corner x,y
    if (gid.x >= u_CornerRes.x || gid.y >= u_CornerRes.y) return;

    float x = u_screenParams.x + float(gid.x) * u_screenParams.z; // startX + x * stepX
    float y = u_screenParams.y + float(gid.y) * u_screenParams.w; // startY + y * stepY
    y = -y;

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