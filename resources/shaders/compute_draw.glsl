#version 430 core

#define WORLD_POS_OFF 0
#define MOMENTUM_OFF 1
#define TEX_POS_OFF 2

#define DEBUG_SIZE 4096

layout (local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

// Constants -------------------------------------------------------------------

const int k_maxGeoPixels = 16384;
const int k_maxGeoPixelsRows = 27;
const int k_maxGeoPixelsSum = k_maxGeoPixels * (k_maxGeoPixelsRows / 3);

const int k_estMaxOutlinePixels = 16384;
const int k_maxOutlinePixelsRows = 27 * 2;
const int k_maxOutlinePixelsSum = k_estMaxOutlinePixels * (k_maxOutlinePixelsRows / 2 / 3);

const int k_invocCount = 1024;

const vec3 k_sideGeoColor = vec3(1.0f, 1.0f, 0.0f);
const vec3 k_sideOutlineColor = vec3(0.0f, 1.0f, 1.0f);

// Uniforms --------------------------------------------------------------------

uniform int u_swap;

layout (   r32i, binding = 2) uniform iimage2D u_flagImg;									
layout (  rgba8, binding = 3) uniform  image2D u_fboImg;
layout (rgba32f, binding = 4) uniform  image2D u_geoImg;	
layout (  rgba8, binding = 6) uniform  image2D u_geoSideImg;	
layout (rgba32f, binding = 5) uniform  image2D u_outlineImg;

layout (std430, binding = 0) restrict buffer SSBO { 
    int geoCount;
    int test; // necessary for padding
    int outlineCount[2];
    vec4 screenSpec;
    ivec4 momentum;
    ivec4 force;
    ivec4 debugShit[DEBUG_SIZE];
} ssbo;

// Functions -------------------------------------------------------------------

ivec2 getGeoTexCoord(int index, int offset) { // with offset being 0, 1, or 2 (world, momentum, tex)
    return ivec2(
        index % k_maxGeoPixels,
        offset + 3 * (index / k_maxGeoPixels)
    );
}

ivec2 getOutlineTexCoord(int index, int offset, int swap) { // with offset being 0, 1, or 2  (world, momentum, tex)
    return ivec2(
        index % k_estMaxOutlinePixels,
        offset + 3 * (index / k_estMaxOutlinePixels) + swap * (k_maxOutlinePixelsRows / 2)
    );
}

vec2 worldToScreen(vec3 world) {
    vec2 screenPos = world.xy;
    screenPos *= ssbo.screenSpec.zw; // compensate for aspect ratio
    screenPos = screenPos * 0.5f + 0.5f; // center
    screenPos *= ssbo.screenSpec.xy; // scale to texture space
    return screenPos;
}

void main() {
    int invocI = int(gl_GlobalInvocationID.x);
    int invocWorkload = (ssbo.outlineCount[u_swap] + k_invocCount - 1) / k_invocCount;
    for (int ii = 0; ii < invocWorkload; ++ii) {

        int workI = invocI + (k_invocCount * ii);
        if (workI >= ssbo.outlineCount[u_swap] || workI >= k_maxOutlinePixelsSum) {
            break;
        }

        //vec3 norm = imageLoad(u_outlineImg, getOutlineTexCoord(workI, MOMENTUM_OFF, u_swap)).xyz;
        vec3 outlineWorldPos = imageLoad(u_outlineImg, getOutlineTexCoord(workI, WORLD_POS_OFF, u_swap)).xyz;
        vec3 geoWorldPos = imageLoad(u_geoImg, getGeoTexCoord(workI, WORLD_POS_OFF)).xyz;
        
        ivec2 outlineScreenPos = ivec2(worldToScreen(outlineWorldPos));
        ivec2 geoSideTexPos = ivec2(worldToScreen(vec3(-geoWorldPos.z, geoWorldPos.y, 0)));
        ivec2 outlineSideTexPos = ivec2(worldToScreen(vec3(-outlineWorldPos.z, outlineWorldPos.y, 0)));
        
        vec4 originalColor = imageLoad(u_fboImg, outlineScreenPos);
        originalColor.b = 1.0f;

        imageStore(u_fboImg, outlineScreenPos, originalColor);
        if (abs(outlineWorldPos.x) <= 0.9f) { // ignore crazy stragglers on the edges
            imageStore(u_geoSideImg, geoSideTexPos, vec4(k_sideGeoColor, 1.0f));
            imageStore(u_geoSideImg, outlineSideTexPos, vec4(k_sideOutlineColor, 1.0f));
        }
        imageAtomicExchange(u_flagImg, outlineScreenPos, workI + 1);
    }        
}