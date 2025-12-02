#version 430
layout(local_size_x = 16, local_size_y = 16) in;

layout(std430, binding = 3) readonly buffer GridSSBO {
    uint grid[]; // as written in pass1
};

layout(std430, binding = 4) readonly buffer ComparisonsSSBO {
    // each comparison: index1, index2, booleanOp
    uvec4 comps[]; // x=index1, y=index2, z=booleanOp, w=unused
};

layout(std430, binding = 5) readonly buffer RelSignSSBO {
    uint relSigns[]; // relation sign per function
};

layout(std430, binding = 6) readonly buffer ColorsSSBO {
    uvec4 colors[]; // RGBA per function
};

uniform ivec2 u_res;        // W,H
uniform ivec2 u_cornerRes;  // W+1,H+1
uniform uint u_comparisonCount;

layout(binding=0, rgba8) uniform writeonly image2D outImage;

uint readGrid(uint funcIndex, int cx, int cy){
    // bounds assumed valid
    uint idx = funcIndex * uint(u_cornerRes.x * u_cornerRes.y) + uint(cy) * uint(u_cornerRes.x) + uint(cx);
    return uint(grid[idx]);
}

bool applyRelation(uint rel, bool allEqual, uint cornerValue) {
    bool cornerIsZero = (cornerValue == 0u);
    bool cornerIsOne  = (cornerValue == 1u);

    if (rel == 0u) return !allEqual;                 
    else if (rel == 1u) return allEqual && cornerIsZero;
    else if (rel == 2u) return !allEqual || cornerIsZero;
    else if (rel == 3u) return allEqual && cornerIsOne;
    else if (rel == 4u) return !allEqual || cornerIsOne;
    return false;
}

void main()
{
    ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
    if (gid.x >= u_res.x || gid.y >= u_res.y) return;

    // read all comparisons and decide pixel color; we will paint with a fixed color for matched comparisons
    // compute corner coords: use corners (x,y), (x+1,y), (x,y+1), (x+1,y+1)
    int x = gid.x;
    int y = gid.y;

    for (uint ci = 0u; ci < u_comparisonCount; ++ci) {
        uvec4 cmp = comps[ci];
        uint idx1 = cmp.x;
        uint idx2 = cmp.y;
        uint bOp  = cmp.z;

        // read four corners for each function
        uint a1 = readGrid(idx1, x,   y);
        uint b1 = readGrid(idx1, x+1, y);
        uint c1 = readGrid(idx1, x,   y+1);
        uint d1 = readGrid(idx1, x+1, y+1);

        uint a2 = readGrid(idx2, x,   y);
        uint b2 = readGrid(idx2, x+1, y);
        uint c2 = readGrid(idx2, x,   y+1);
        uint d2 = readGrid(idx2, x+1, y+1);

        bool allEqual1 = (a1 == b1) && (a1 == c1) && (a1 == d1);
        bool allEqual2 = (a2 == b2) && (a2 == c2) && (a2 == d2);

        uint corner1 = a1; // choose a corner value for equal-case tests
        uint corner2 = a2;

        uint rel1 = relSigns[idx1];
        uint rel2 = relSigns[idx2];

        bool color1 = applyRelation(rel1, allEqual1, corner1);
        bool color2 = applyRelation(rel2, allEqual2, corner2);

        bool result = false;
        if (bOp == 0u) result = color1 && color2;       // AND
        else if (bOp == 1u) result = color1 || color2;  // OR
        else if (bOp == 2u) result = color1 && !color2; // DIFF
        else if (bOp == 3u) result = color1 != color2;   // XOR

        if (result) {
            // pick a color for one of the functions involved in this comparison
            uvec4 uc = colors[ci];
            vec4 c = vec4(uc.r, uc.g, uc.b, 127.0) / 255.0; // normalize 0–255 to 0.0–1.0
            imageStore(outImage, gid, c);
            // break; // optional: only first matching comparison
        }
    }
}
