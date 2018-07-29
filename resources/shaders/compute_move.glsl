﻿#version 450
#extension GL_ARB_shader_storage_buffer_object : require

#define WORLDPOSOFF 0
#define MOMENTUMOFF 1


layout (local_size_x = 1, local_size_y = 1) in;

#define ESTIMATEMAXOUTLINEPIXELS 16384
#define ESTIMATEMAXOUTLINEPIXELSROWS 54
#define ESTIMATEMAXOUTLINEPIXELSSUM ESTIMATEMAXOUTLINEPIXELS *(ESTIMATEMAXOUTLINEPIXELSROWS/(2*3))

ivec2 loadstore_outline(uint index, int init_offset, uint swapval)//with init_offset being 0,1 or 2  (world, momentum, tex)
    {
    uint off = index%ESTIMATEMAXOUTLINEPIXELS;
    uint mul = index/ESTIMATEMAXOUTLINEPIXELS;
    int halfval = ESTIMATEMAXOUTLINEPIXELSROWS/2;
    return ivec2(off ,init_offset + 3*mul + swapval*halfval);
    }
#define ESTIMATEMAXGEOPIXELS 16384
#define ESTIMATEMAXGEOPIXELSROWS 27
#define ESTIMATEMAXGEOPIXELSSUM ESTIMATEMAXGEOPIXELS *(ESTIMATEMAXGEOPIXELSROWS/3)

ivec2 load_geo(uint index,int init_offset)//with init_offset being 0,1 or 2 (world, momentum, tex)
    {
    uint off = index%ESTIMATEMAXGEOPIXELS;
    uint mul = index/ESTIMATEMAXGEOPIXELS;
    return ivec2(off ,init_offset + 3*mul);
    }

uniform uint swap;
// local group of shaders
// compute buffers
layout (r32ui, binding = 2) uniform uimage2D img_flag;									
layout (rgba8, binding = 3) uniform image2D img_FBO;	
layout (rgba32f, binding = 4) uniform image2D img_geo;	
layout (rgba32f, binding = 5) uniform image2D img_outline;	
layout (std430, binding = 0) restrict buffer ssbo_geopixels {
    uint geo_count;
    uint test;
    uint out_count[2];
    vec4 screenratio;
    ivec4 momentum;
    ivec4 force;
    ivec4 debugshit[4096];
} geopix;
//layout (binding = 1, offset = 0) uniform atomic_uint ac;

void main() {
    int index = int(gl_GlobalInvocationID.x);
    int shadernum = 1024;
    uint iac = geopix.out_count[swap];
    float f_cs_workload_per_shader = ceil(float(iac) / float(shadernum));
    int cs_workload_per_shader = int(f_cs_workload_per_shader);
    int counterswap =  abs(int(swap) - 1);
    if (swap == 0) counterswap = 1;
    else counterswap = 0;
    for (int ii = 0; ii < cs_workload_per_shader; ii++) {
        int work_on = index+shadernum * ii;
        if (work_on >= ESTIMATEMAXOUTLINEPIXELSSUM) break;
        if (work_on >= geopix.out_count[swap]) break;

        vec3 worldpos = imageLoad(img_outline,loadstore_outline(work_on, WORLDPOSOFF,swap)).xyz;

    

        vec3 normal = imageLoad(img_outline,loadstore_outline(work_on, MOMENTUMOFF,swap)).xyz;
        

        normal.z = 0.0f;
        vec3 winddirection = vec3(0.0f, 0.0f, 1.0f);
        vec3 direction_result = normalize(normal + winddirection);


      
        vec3 world_direction = direction_result*5.5;

        world_direction.x /= geopix.screenratio.x;
        world_direction.y /= geopix.screenratio.y;

        vec3 newworldpos = worldpos;
        newworldpos.xy = newworldpos.xy - world_direction.xy;
        vec2 w2t = newworldpos.xy;
        w2t.x *= geopix.screenratio.x*(2./3.);
        w2t.y *= geopix.screenratio.y;
        w2t.x+=geopix.screenratio.z/2.;
        w2t.y+=geopix.screenratio.w/2.;

        vec4 col = imageLoad(img_FBO, ivec2(w2t));

       // col = imageLoad(img_FBO, ivec2(newtexpos));

       if(col.r>0.5) //its geo!
           {
            vec2 nextpos = w2t;	
            nextpos = floor(nextpos) + vec2(0.5,0.5) + normalize(vec2(world_direction.x,-world_direction.y));
            vec4 nextcol = imageLoad(img_FBO, ivec2(nextpos) );			
            if(nextcol.r>0.5)
                {
                continue;
                }

           uint geo_index = imageAtomicAdd(img_flag, ivec2(w2t)+ ivec2(0,geopix.screenratio.w), uint(0));
           vec3 geo_worldpos = imageLoad(img_geo,load_geo(geo_index, WORLDPOSOFF)).xyz;
           vec2 geo_normal = imageLoad(img_geo,load_geo(geo_index, MOMENTUMOFF)).xy;

           vec3 dir_geo_out = newworldpos - geo_worldpos;
           if(dot(dir_geo_out.xy,geo_normal)<0)
                continue;

           }
           
       
       
       if(col.b>0.5) 
            continue;//merge!!!

                //   if (col.b < 0.1f && col.g < 0.1f && col.r < 0.1f) 
                   {
        uint current_array_pos = atomicAdd(geopix.out_count[counterswap], 1);
        if(current_array_pos >= ESTIMATEMAXOUTLINEPIXELSSUM)
            break;

        //imageStore(img_outline, loadstore_outline(current_array_pos, TEXPOSOFF,counterswap), vec4(newtexpos, 0.0f, 0.0f));   
        imageStore(img_outline, loadstore_outline(current_array_pos, MOMENTUMOFF,counterswap), vec4(direction_result, 0.0f));
        imageStore(img_outline, loadstore_outline(current_array_pos, WORLDPOSOFF,counterswap), vec4(newworldpos, 0.0f));
            }

    

    }
        
}