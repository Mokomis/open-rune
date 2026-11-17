#version 450
// Native adaptation of Crest 4.23.0 UpdateFoam.compute, MIT (see LICENSE).
// Stable world-tiled FFT cascades replace Unity's camera-following LOD mapping.
layout(local_size_x=8,local_size_y=8)in;
layout(set=0,binding=0)uniform sampler2DArray waves;
layout(set=0,binding=1)uniform sampler2DArray previousFoam;
layout(r32f,set=0,binding=2)uniform writeonly image2DArray foamTarget;
layout(push_constant)uniform Parameters {uint size;float wind,turbulence,gravity,period,time,chop,dt;vec2 windDirection;};
void main(){
 ivec3 id=ivec3(gl_GlobalInvocationID.xyz);float worldSize=.5*exp2(float(id.z));float texelWidth=worldSize/128.0;
 vec2 uv=(vec2(id.xy)+.5)/128.0;
 // Same persistence, advection, Jacobian compression and time-scaled generation
 // as UpdateFoam. Shoreline depth is body-specific and applied at composition.
 float foam=textureLod(previousFoam,vec3(uv-dt*windDirection*.15/worldSize,float(id.z)),0.0).r;
 foam*=max(0.0,1.0-.8*dt);
 vec3 s=texelFetch(waves,id,0).xyz*.12;
 vec3 sx=texelFetch(waves,ivec3((id.x+1)%128,id.y,id.z),0).xyz*.12;
 vec3 sz=texelFetch(waves,ivec3(id.x,(id.y+1)%128,id.z),0).xyz*.12;
 vec2 dx=(vec2(texelWidth,0)+sx.xz-s.xz)/texelWidth;
 vec2 dz=(vec2(0,texelWidth)+sz.xz-s.xz)/texelWidth;
 float jac=dx.x*dz.y-dx.y*dz.x;
 foam+=5.0*dt*.7*clamp(.88-jac,0.0,1.0);
 imageStore(foamTarget,id,vec4(clamp(foam,0.0,1.0)));
}
