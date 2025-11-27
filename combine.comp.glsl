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

uniform ivec2 u_res;        // W,H
uniform ivec2 u_cornerRes;  // W+1,H+1
uniform uint u_funcCount;
uniform uint u_comparisonCount;

layout(binding=0, rgba8) uniform writeonly image2D outImage;

uint readGrid(uint funcIndex, int cx, int cy){
    // bounds assumed valid
    uint idx = funcIndex * uint(u_cornerRes.x * u_cornerRes.y) + uint(cy) * uint(u_cornerRes.x) + uint(cx);
    return grid[idx];
}

bool applyRelation(uint rel, uint allEqual, uint cornerValue) {
    // rel: REL_EQ=0, REL_LT=1, REL_LTE=2, REL_GT=3, REL_GTE=4
    bool allCornersEqual = allEqual != 0u;
    bool allCornersNegative = allCornersEqual && (cornerValue == 0u);
    bool allCornersPositive = allCornersEqual && (cornerValue == 1u);

    if (rel == 0u) return !allCornersEqual;                 // "=" => any sign change inside cell -> color
    else if (rel == 1u) return allCornersEqual && (cornerValue == 0u); // "<" 
    else if (rel == 2u) return (!allCornersEqual) || (cornerValue == 0u); // "<="
    else if (rel == 3u) return allCornersEqual && (cornerValue == 1u); // ">"
    else if (rel == 4u) return (!allCornersEqual) || (cornerValue == 1u); // ">="
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

        bool color1 = applyRelation(rel1, uint(allEqual1 ? 1u : 0u), corner1);
        bool color2 = applyRelation(rel2, uint(allEqual2 ? 1u : 0u), corner2);

        bool result = false;
        if (bOp == 0u) result = color1 && color2;       // AND
        else if (bOp == 1u) result = color1 || color2;  // OR
        else if (bOp == 2u) result = color1 && !color2; // DIFF
        else if (bOp == 3u) result = color1 ^ color2;   // XOR

        if (result) {
            // write a color (R,G,B,A). Example: teal-ish with transparency 127/255
            vec4 c = vec4(0.0, 0.5, 1.0, 0.5); // in normalized floats
            // convert to uvec4 for RGBA8
            uvec4 outc = uvec4(uvec3(c.rgb * 255.0), uint(c.a * 255.0));
            imageStore(outImage, ivec2(gid), vec4(c.rgb, c.a)); // imageStore accepts float vec4 for rgba8 too
            // break if we want first matching comparison only:
            // break;
        }
    }

    // If no comparison matched we leave pixel black (image initially cleared or not written).
    // Alternatively you can explicitly write (0,0,0,1).
}
