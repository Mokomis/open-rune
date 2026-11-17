#pragma once
#include <string>
inline std::string CrestSamplingGLSL() { return R"(
        layout(set=1,binding=4) uniform sampler2DArray crestWaves;
        vec3 crestDisplacement(vec2 world,float depth,vec2 camera) {
            vec3 total=vec3(0);vec2 meters=world*(1.0/40.0);
            { const float size=2.0; const int cascade=2;
                float lod=0.0;
                #ifdef CREST_FRAGMENT
                vec2 dx=dFdx(meters/size)*128.0,dy=dFdy(meters/size)*128.0;
                lod=clamp(.5*log2(max(dot(dx,dx),dot(dy,dy))),0.0,7.0);
                #endif
                vec3 d=textureLod(crestWaves,vec3(meters/size,float(cascade)),lod).xyz;
                total+=d.xzy;
            }
            { const float size=4.0; const int cascade=3;
                float lod=0.0;
                #ifdef CREST_FRAGMENT
                vec2 dx=dFdx(meters/size)*128.0,dy=dFdy(meters/size)*128.0;
                lod=clamp(.5*log2(max(dot(dx,dx),dot(dy,dy))),0.0,7.0);
                #endif
                vec3 d=textureLod(crestWaves,vec3(meters/size,float(cascade)),lod).xyz;
                total+=d.xzy;
            }
            { const float size=8.0; const int cascade=4;
                float lod=0.0;
                #ifdef CREST_FRAGMENT
                vec2 dx=dFdx(meters/size)*128.0,dy=dFdy(meters/size)*128.0;
                lod=clamp(.5*log2(max(dot(dx,dx),dot(dy,dy))),0.0,7.0);
                #endif
                vec3 d=textureLod(crestWaves,vec3(meters/size,float(cascade)),lod).xyz;
                total+=d.xzy;
            }
            { const float size=16.0; const int cascade=5;
                float lod=0.0;
                #ifdef CREST_FRAGMENT
                vec2 dx=dFdx(meters/size)*128.0,dy=dFdy(meters/size)*128.0;
                lod=clamp(.5*log2(max(dot(dx,dx),dot(dy,dy))),0.0,7.0);
                #endif
                vec3 d=textureLod(crestWaves,vec3(meters/size,float(cascade)),lod).xyz;
                total+=d.xzy;
            }
            { const float size=32.0; const int cascade=6;
                float lod=0.0;
                #ifdef CREST_FRAGMENT
                vec2 dx=dFdx(meters/size)*128.0,dy=dFdy(meters/size)*128.0;
                lod=clamp(.5*log2(max(dot(dx,dx),dot(dy,dy))),0.0,7.0);
                #endif
                vec3 d=textureLod(crestWaves,vec3(meters/size,float(cascade)),lod).xyz;
                total+=d.xzy;
            }
            { const float size=64.0; const int cascade=7;
                float lod=0.0;
                #ifdef CREST_FRAGMENT
                vec2 dx=dFdx(meters/size)*128.0,dy=dFdy(meters/size)*128.0;
                lod=clamp(.5*log2(max(dot(dx,dx),dot(dy,dy))),0.0,7.0);
                #endif
                vec3 d=textureLod(crestWaves,vec3(meters/size,float(cascade)),lod).xyz;
                total+=d.xzy;
            }
            float shallows=smoothstep(1.0,100.0,depth);
            vec2 patchDistance=abs(world-camera);
            float patchFade=1.0-smoothstep(1024.0,1536.0,max(patchDistance.x,patchDistance.y));
            #ifdef CREST_FRAGMENT
            // Only geometry uses the bounded near-view patch. Surface waves
            // and their normals continue into the distance, with mip filtering.
            return total*(40.0*.12)*shallows;
            #else
            return total*(40.0*.12)*shallows*patchFade;
            #endif
        }
    )"; }
