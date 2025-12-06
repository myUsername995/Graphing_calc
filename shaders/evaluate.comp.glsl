#version 430
// PASS 1: evaluate expressions at every corner sample
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

// We'll also expose an instructions-as-bytes view; instead we will read as uvec4 per-instruction:
layout(std430, binding = 0) readonly buffer InstrView {
    uvec4 rawInstr[]; // one uvec4 per GPUInstruction (16 bytes)
};

layout(std430, binding = 1) readonly buffer OffsetsSSBO {
    uint offsets[]; // offset (index into rawInstr)
};

layout(std430, binding = 2) readonly buffer LengthsSSBO {
    uint lengths[]; // length in instructions
};

layout(std430, binding = 3) writeonly buffer GridSSBO {
    // one uint per corner sample (0 = NEGATIVE, 1 = POSITIVE)
    uint grid[]; // size = funcCount * CORNER_W * CORNER_H
};

layout(std430, binding = 5) readonly buffer RelSignSSBO {
    uint relSigns[]; // for information, if needed
};

// Screen params as uniforms
uniform vec4 u_screenParams; // (startX, startY, stepX, stepY)
uniform ivec2 u_res;         // (W, H)
uniform ivec2 u_cornerRes;   // (W+1, H+1)

// Instr layout helpers
uint instr_kind(uvec4 v) { return v.x; }
uint instr_op(uvec4 v) { return v.y; }
float instr_number(uvec4 v) { return uintBitsToFloat(v.z); }
int instr_var(uvec4 v) { return int(v.w); }

// small stack interpreter using floats
float applyUnary(uint op, float a){
    switch (op){
        case 0u:  // OP_NEG
            return -a;
        case 1u:  // OP_ABS
            return abs(a);
        case 2u:  // OP_SQRT
            return sqrt(a);
        case 3u:  // OP_LN
            return log(a);       // natural log
        case 4u:  // OP_LOG (log10)
            return log10(a);
        case 5u:  // OP_SIN
            return sin(a);
        case 6u:  // OP_COS
            return cos(a);
        case 7u:  // OP_TAN
            return tan(a);
        case 8u:  // OP_ASIN
            return asin(a);
        case 9u:  // OP_ACOS
            return acos(a);
        case 10u: // OP_ATAN
            return atan(a);
        case 11u: // OP_FLOOR
            return floor(a);
    }
    return a;
}

float pow_cpp(float base, float exp) {
    // Check if exponent is a real integer (e.g., 2.0, 3.0, -1.0)
    float expInt = floor(exp + 0.5);

    int intExponentLimit = 25;

    // If exponent is (approximately) integer:
    if (abs(exp - expInt) < 1e-6 && exp < intExponentLimit){
        float r = 1.0;
        for(int i=0;i<expInt;i++) r*=base;
        return r;
    }

    return pow(base, exp);
}

float applyBinary(uint op, float a, float b){
    switch (op){
        case 12u: // OP_ADD
            return a + b;
        case 13u: // OP_SUB
            return a - b;
        case 14u: // OP_MUL
            return a * b;
        case 15u: // OP_DIV
            return a / b;
        case 16u: // OP_POW
            return pow_cpp(a, b);
        case 17u: // OP_MOD
            return mod(a, b);    // GLSL mod(a,b)
    }
    return 0.0;
}

void main(){
    ivec2 gid = ivec2(gl_GlobalInvocationID.xy); // corner x,y
    uint funcIndex = uint(gl_WorkGroupID.z);     // we dispatched gz = funcCount
    if (gid.x >= u_cornerRes.x || gid.y >= u_cornerRes.y) return;
    // compute world coords for this corner
    float worldX = u_screenParams.x + float(gid.x) * u_screenParams.z; // startX + x * stepX
    float worldY = u_screenParams.y + float(gid.y) * u_screenParams.w; // startY + y * stepY

    // find instr offset and length for funcIndex
    uint offset = offsets[funcIndex]; // index into rawInstr
    uint len = lengths[funcIndex];

    // small eval stack
    const int STACK_MAX = 64;
    float stack[STACK_MAX];
    int sp = 0;

    for (uint i = 0u; i < len; i++) {
        uvec4 inst = rawInstr[offset + i];
        uint kind = instr_kind(inst);
        if (kind == 0u) { // EXPR_NUMBER
            stack[sp++] = instr_number(inst);
        } 
        else if (kind == 2u) { // EXPR_VAR
            int v = instr_var(inst);
            if (v == 1) stack[sp++] = worldX;
            else if (v == 2) stack[sp++] = -worldY;
            else stack[sp++] = 0.0; // default/fallback
        } 
        else if (kind == 3u) { // EXPR_UNARY
            uint op = instr_op(inst);
            float a = stack[--sp];
            stack[sp++] = applyUnary(op, a);
        } 
        else if (kind == 1u) { // EXPR_BINARY
            uint op = instr_op(inst);
            float b = stack[--sp];
            float a = stack[--sp];
            stack[sp++] = applyBinary(op, a, b);
        }
    }

    float result = (sp > 0) ? stack[sp - 1] : 0.0;

    // Sign rule from your CPU: sign <= 0 => NEGATIVE (we will store NEGATIVE as 0, POSITIVE as 1)
    uint sign = (result <= 0.0) ? 0u : 1u;
    // index into grid: idx = funcIndex * cornerCount + gid.y * CORNER_W + gid.x
    // keep in mind that openGL starts (0, 0) at the bottom left corner, instead of top left

    // When indexing into the 3D array grid, you use grid[z][y][x]
    // With a flat array, its different ->
    // bigArrayIndex -> essentially specifies the z index
    // smallArrayIndex -> specifies the y and x indexes

    uint bigArrayIndex = funcIndex * uint(u_cornerRes.x * u_cornerRes.y);
    // flip Y so 0 is bottom row (if that's intended)
    uint row = uint(u_cornerRes.y - 1 - gid.y);   // use u_cornerRes.y, not u_res.y, and -1
    uint smallArrayIndex = row * uint(u_cornerRes.x) + uint(gid.x);

    // We should only modify values within our z index, if smallArrayIndex is bigger (or equal) than the size of one z index, then 
    // we would be going outside of our z index into the z+1 index
    if (smallArrayIndex >= uint(u_cornerRes.x * u_cornerRes.y)) return;

    uint idx = bigArrayIndex + smallArrayIndex;
    grid[idx] = sign;
}
