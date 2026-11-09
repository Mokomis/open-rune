
#include "Precomp.h"
#include "FileResource.h"
#include "Render/ShadowSettings.h"
#include "sgsr/SGSRShader.h"
#include "cas/CASShader.h"

// I probably should find a less brain dead way of doing this. :)

std::string FileResource::readAllText(const std::string& filename)
{
	if (filename == "shaders/Scene.vert")
	{
		return R"(
			layout(constant_id=1002) const bool OpenRuneFixedPbr=false;
            layout(push_constant) uniform ScenePushConstants
			{
				mat4 objectToProjection;
				mat4 objectToView;
				vec4 nearClip;
				uint uHitIndex;
				uint padding1, padding2, padding3;
			};

			layout(location = 0) in uint aFlags;
			layout(location = 1) in vec3 aPosition;
			layout(location = 2) in vec2 aTexCoord;
			layout(location = 3) in vec2 aTexCoord2;
			layout(location = 4) in vec2 aTexCoord3;
			layout(location = 5) in vec2 aTexCoord4;
			layout(location = 6) in vec4 aColor;
			layout(location = 7) in ivec4 aTextureBinds;
            layout(location = 8) in vec3 aNormal;

			layout(location = 0) flat out uint flags;
			layout(location = 1) out vec2 texCoord;
			layout(location = 2) out vec2 texCoord2;
			layout(location = 3) out vec2 texCoord3;
			layout(location = 4) out vec2 texCoord4;
			layout(location = 5) out vec4 color;
			#ifndef NO_HIT_OUTPUT
			layout(location = 6) flat out uint hitIndex;
			#endif
			layout(location = 7) flat out ivec4 textureBinds;
            layout(location = 8) out vec3 reliefViewPosition;
            layout(location = 9) out vec3 meshPosition;
            layout(location = 10) out vec3 meshNormal;
            layout(location = 11) out vec3 environmentDiffuse;
            layout(location = 12) out vec3 environmentSpecular;
            struct MeshLight { vec4 positionRadius; vec4 color; };
            struct MeshLighting { vec4 ambientScale; ivec4 meta; MeshLight lights[8]; vec4 surfaceOrigin; vec4 surfaceU; vec4 surfaceV; vec4 material; ivec4 materialMaps; vec4 viewCamera; vec4 surfaceTangent; vec4 surfaceBitangent; vec4 environment[6]; };
            layout(std430, set = 1, binding = 0) readonly buffer MeshLightBuffer { MeshLighting meshLights[]; };
            void vertexEnvironment(int index,vec3 position,vec3 N)
            {
                if(meshLights[index].environment[0].w<0.5)return;
                vec3 V=normalize(meshLights[index].viewCamera.xyz-position);
                if(dot(N,V)<0.0)N=-N;
                vec3 R=reflect(-V,N);
                vec3 xp=meshLights[index].environment[0].rgb,xn=meshLights[index].environment[1].rgb;
                vec3 yp=meshLights[index].environment[2].rgb,yn=meshLights[index].environment[3].rgb;
                vec3 zp=meshLights[index].environment[4].rgb,zn=meshLights[index].environment[5].rgb;
                vec3 mean=(xp+xn+yp+yn+zp+zn)*(1.0/6.0),n2=N*N,r2=R*R;
                vec3 diffuse=(N.x>=0.0?xp:xn)*n2.x+(N.y>=0.0?yp:yn)*n2.y+(N.z>=0.0?zp:zn)*n2.z;
                vec3 reflected=(R.x>=0.0?xp:xn)*r2.x+(R.y>=0.0?yp:yn)*r2.y+(R.z>=0.0?zp:zn)*r2.z;
                vec4 material=meshLights[index].material;
                float rough=(OpenRuneFixedPbr || material.w>0.5)?material.y:sqrt(sqrt(2.0/(material.y+2.0)));
                // These probes contain only broad room color, so evaluate their
                // smooth direction weights per vertex. Direct lights and normal
                // maps still use per-fragment shading and actual roughness.
                environmentDiffuse=mix(mean,diffuse,0.5)*0.32;
                environmentSpecular=mix(mean,reflected,(1.0-rough)*0.9)*(1.0-rough*0.5)*0.85;
            }


			void main()
			{
				gl_Position = objectToProjection * vec4(aPosition, 1.0);
				gl_ClipDistance[0] = dot(nearClip, objectToView * vec4(aPosition, 1.0));
				flags = aFlags;
                meshPosition = (aFlags & (65536u|524288u))!=0u ? aPosition : vec3(0.0);
                meshNormal = (aFlags & 65536u)!=0u ? aNormal : vec3(0.0);
                environmentDiffuse=environmentSpecular=vec3(0);
                if((aFlags&65536u)!=0u)vertexEnvironment(aTextureBinds.y,aPosition,normalize(aNormal));
                else if((aFlags&262144u)!=0u) {
                    int index=int(aFlags>>20);
                    vec4 origin=meshLights[index].surfaceOrigin,u=meshLights[index].surfaceU,v=meshLights[index].surfaceV;
                    vertexEnvironment(index,origin.xyz+u.xyz*aTexCoord2.x+v.xyz*aTexCoord2.y,vec3(origin.w,u.w,v.w));
                }

                reliefViewPosition = (objectToView * vec4(aPosition,1.0)).xyz;
				texCoord = aTexCoord;
				texCoord2 = aTexCoord2;
				texCoord3 = aTexCoord3;
				texCoord4 = aTexCoord4;
				color = aColor;
				#ifndef NO_HIT_OUTPUT
			hitIndex = uHitIndex;
			#endif
				textureBinds = aTextureBinds;
			}
		)";
	}

    else if (filename == "shaders/Shadow.vert")
    {
        return R"(
            layout(push_constant) uniform Push { mat4 worldToShadow; };
            layout(location=0) in uint aFlags;
            layout(location=1) in vec3 aPosition;
            layout(location=2) in vec2 aUV;
            layout(location=7) in ivec4 aTextures;
            layout(location=0) out vec2 uv;
            layout(location=1) flat out int tex;
            layout(location=2) flat out uint masked;
            void main() { gl_Position=worldToShadow*vec4(aPosition,1); uv=aUV; tex=aTextures.x; masked=aFlags; }
        )";
    }
    else if (filename == "shaders/Shadow.frag")
    {
        return R"(
            layout(binding=0) uniform sampler2D textures[];
            layout(location=0) in vec2 uv;
            layout(location=1) flat in int tex;
            layout(location=2) flat in uint masked;
            void main() { if(masked!=0u && texture(textures[nonuniformEXT(tex)],uv).a<0.5) discard; }
        )";
    }
	else if (filename == "shaders/Scene.frag")
	{
		return std::string("#define SHADOW_CAPACITY ")+std::to_string(RuneShadows::Capacity)+"\n#define SHADOW_COLUMNS "+std::to_string(RuneShadows::Columns)+"\n#define SHADOW_ROWS "+std::to_string(RuneShadows::Rows)+"\n#define SHADOW_SLOT_BITS "+std::to_string(RuneShadows::Slots::Bits)+"\n#define SHADOW_SLOT_MASK "+std::to_string(RuneShadows::Slots::Mask)+"u\n"+R"(
			layout(constant_id=1001) const bool OpenRuneFixedRemastered=false;
            layout(constant_id=1002) const bool OpenRuneFixedPbr=false;
            layout(binding = 0) uniform sampler2D textures[];

			layout(location = 0) flat in uint flags;
			layout(location = 1) centroid in vec2 texCoord;
			layout(location = 2) in vec2 texCoord2;
			layout(location = 3) in vec2 texCoord3;
			layout(location = 4) in vec2 texCoord4;
			layout(location = 5) in vec4 color;
			#ifndef NO_HIT_OUTPUT
			layout(location = 6) flat in uint hitIndex;
			#endif
			layout(location = 7) flat in ivec4 textureBinds;
            layout(location = 8) in vec3 reliefViewPosition;
            layout(location = 9) centroid in vec3 meshPosition;
            layout(location = 10) centroid in vec3 meshNormal;
            layout(location = 11) in vec3 environmentDiffuse;
            layout(location = 12) in vec3 environmentSpecular;
            struct MeshLight { vec4 positionRadius; vec4 color; };
            struct MeshLighting { vec4 ambientScale; ivec4 meta; MeshLight lights[8]; vec4 surfaceOrigin; vec4 surfaceU; vec4 surfaceV; vec4 material; ivec4 materialMaps; vec4 viewCamera; vec4 surfaceTangent; vec4 surfaceBitangent; vec4 environment[6]; };
            layout(std430, set = 1, binding = 0) readonly buffer MeshLightBuffer { MeshLighting meshLights[]; };

            struct ShadowLight { mat4 worldToShadow; vec4 position; };
            #ifdef UNIFORM_SHADOW_DATA
            layout(std140, set=1, binding=1) uniform ShadowLightingBuffer {
            #else
            layout(std430, set=1, binding=1) readonly buffer ShadowLightingBuffer {
            #endif
                ShadowLight shadowLights[SHADOW_CAPACITY]; vec4 lightingCamera; mat4 waterWorldToClip; vec4 waterParameters;
                vec4 contactCenters[4]; vec4 contactPlanes[4];
            };
            layout(set=1, binding=2) uniform sampler2D dynamicShadow;
            layout(set=1, binding=3) uniform sampler2D waterReflection;
            bool remastered() { return OpenRuneFixedRemastered || lightingCamera.w > 0.5; }

            float contactShadow(vec3 position, vec3 normal, int mask)
            {
                if(!remastered()) return 1.0;
                #ifdef CULL_LIGHTING
                if(mask==0) return 1.0;
                #endif
                float occlusion=0.0;
                for(int i=0;i<4;++i) {
                    #ifdef CULL_LIGHTING
                    if((mask & (1<<i))==0) continue;
                    #endif
                    vec4 plane=contactPlanes[i];
                    if(plane.w<=0.0) continue;
                    vec3 delta=position-contactCenters[i].xyz;
                    float height=dot(delta,plane.xyz);
                    // Restrict the footprint to the traced ground plane: no
                    // shadows painted onto walls or a floor underneath it.
                    if(abs(height)>=2.0 || abs(dot(normal,plane.xyz))<0.85) continue;
                    float r2=max(dot(delta,delta)-height*height,0.0)*contactCenters[i].w;
                    float footprint=1.0-smoothstep(0.0,1.0,r2);
                    float planeFade=1.0-smoothstep(0.5,2.0,abs(height));
                    occlusion=max(occlusion,plane.w*footprint*planeFade);
                }
                return 1.0-occlusion;
            }

            LIGHTP float localLightBoost(LIGHTP float normalizedDistance)
            {
                // Keep the authored room exposure. Extra diffuse energy belongs
                // near the source, not across its entire influence radius.
                return 1.0+0.20*(1.0-smoothstep(0.15,0.55,normalizedDistance));
            }

            float movingShadow(int slot, vec3 position, vec3 N, vec3 lightDirection)
            {
                if(slot<0 || slot>=SHADOW_CAPACITY || shadowLights[slot].position.w<=0.0) return 1.0;
                vec3 lightPosition=shadowLights[slot].position.xyz;
                #ifdef CULL_LIGHTING
                vec3 L=lightDirection;
                #else
                vec3 L=normalize(lightPosition-position);
                #endif
                N=dot(N,L)>=0.0?N:-N; // BSP and masked meshes can be two-sided.
                // A small world-space normal offset avoids acne on grazing surfaces.
                vec4 projected=shadowLights[slot].worldToShadow*vec4(position+N*(0.12+0.25*(1.0-abs(dot(N,L)))),1);
                if(projected.w<=0.0) return 1.0;
                vec3 q=projected.xyz/projected.w;
                if(q.z<=0.0 || q.z>=1.0 || max(abs(q.x),abs(q.y))>=1.0) return 1.0;
                vec2 uv=q.xy*0.5+0.5;
                uv=(uv+vec2(float(slot%SHADOW_COLUMNS),float(slot/SHADOW_COLUMNS)))/vec2(SHADOW_COLUMNS,SHADOW_ROWS);
                // Gather the adjacent depth texels in one texture instruction,
                // then bilinearly filter the comparisons (portable D32 PCF).
                vec2 f=fract(uv*vec2(textureSize(dynamicShadow,0))-0.5);
                vec4 compared=step(vec4(q.z-0.000015),textureGather(dynamicShadow,uv));
                float visibility=mix(mix(compared.w,compared.z,f.x),mix(compared.x,compared.y,f.x),f.y);
                float edge=1.0-smoothstep(0.80,0.98,max(abs(q.x),abs(q.y)));
                return mix(1.0,visibility,edge*shadowLights[slot].position.w);
            }

            LIGHTP vec3 materialHighlight(LIGHTP vec3 V,LIGHTP vec3 N,LIGHTP vec3 L,LIGHTP vec3 light,LIGHTP vec4 material,LIGHTP vec3 albedo)
            {
                if((OpenRuneFixedPbr || material.w>0.5)) {
                    if(dot(N,L)<=0.0 || dot(N,V)<=0.0)return vec3(0);
                    // GGX distribution, Schlick Fresnel and separable Smith
                    // visibility. High precision prevents half-float overflow
                    // at grazing angles. Roughness floor limits specular aliasing.
                    float rough=max(material.y,0.28),a=rough*rough,a2=a*a;
                    // Very rough dielectrics have a broad, weak specular lobe.
                    // Its bounded diffuse-limit approximation saves the full
                    // microfacet evaluation on stone/cloth, without losing it
                    // on metal, wet materials or smoother skin/armor regions.
                    if(rough>=0.65 && material.z<0.05)return light*vec3(0.0125)*mix(0.5,1.0,clamp(material.x,0.0,1.0));
                    vec3 H=L+V;H*=inversesqrt(max(dot(H,H),1e-12));
                    float nh=max(dot(N,H),0.0),nv=max(dot(N,V),0.001),nl=max(dot(N,L),0.001);
                    float denominator=nh*nh*(a2-1.0)+1.0;
                    float distribution=a2/(3.14159265*denominator*denominator);
                    float k=(rough+1.0)*(rough+1.0)*0.125;
                    float visibility=0.25/((nv*(1.0-k)+k)*(nl*(1.0-k)+k));
                    vec3 f0=mix(vec3(0.04),clamp(albedo,0.0,0.85),clamp(material.z*2.0,0.0,1.0));
                    float vh=clamp(dot(V,H),0.0,1.0),f=1.0-vh,f2=f*f;
                    vec3 fresnel=f0+(1.0-f0)*f2*f2*f;
                    return light*min(distribution*visibility*fresnel,vec3(2.0))*mix(0.5,1.0,clamp(material.x,0.0,1.0));
                }
                vec3 H=L+V;H*=inversesqrt(max(dot(H,H),1e-12));
                LIGHTP float spec=pow(max(dot(N,H),0.0),material.y)*material.x;
                return light*spec*mix(vec3(1),albedo,material.z);
            }

            LIGHTP vec3 mappedNormal(int index,vec3 position,vec2 uv,LIGHTP vec3 N,inout LIGHTP vec4 material)
            {
                if(!remastered() || meshLights[index].materialMaps.y==0) return N;
                LIGHTP vec4 packed=texture(textures[nonuniformEXT(meshLights[index].materialMaps.x)],uv);
                #ifdef CULL_LIGHTING
                if(meshLights[index].surfaceBitangent.w>0.5) {
                    vec3 T=meshLights[index].surfaceTangent.xyz;
                    vec3 B=meshLights[index].surfaceBitangent.xyz;
                    LIGHTP vec3 n=packed.rgb*2.0-1.0;
                    N=normalize(T*n.x+B*n.y+N*n.z);
                } else
                #endif
                {
                vec3 dpdx=dFdx(position),dpdy=dFdy(position); vec2 du=dFdx(uv),dv=dFdy(uv);
                float det=du.x*dv.y-du.y*dv.x;
                if(abs(det)>1e-10) {
                    vec3 T=(dpdx*dv.y-dpdy*du.y)/det;
                    T-=N*dot(N,T); T*=inversesqrt(max(dot(T,T),1e-12));
                    vec3 B=normalize(cross(N,T))*sign(det);
                    // Match UV handedness even on mirrored actor triangles.
                    vec3 rawB=(dpdy*du.x-dpdx*dv.x)/det;
                    if(dot(B,rawB)<0.0)B=-B;
                    LIGHTP vec3 n=packed.rgb*2.0-1.0;
                    N=normalize(T*n.x+B*n.y+N*n.z);
                }
                }
                LIGHTP float rough=clamp(packed.a,0.18,0.98);
                material.y=(OpenRuneFixedPbr || material.w>0.5)?rough:clamp(2.0/(rough*rough*rough*rough)-2.0,4.0,128.0);
                material.x*=mix(1.7,0.55,rough);
                if(meshLights[index].materialMaps.w!=0) {
                    LIGHTP vec2 response=texture(textures[nonuniformEXT(meshLights[index].materialMaps.z)],uv).rg;
                    material.x=response.r*mix(1.7,0.55,rough);
                    material.z=response.g;
                }
                return N;
            }

            LIGHTP vec3 environmentLighting(int index,vec3 N,vec3 V,vec4 material,vec3 albedo,inout LIGHTP vec3 highlights)
            {
                if(meshLights[index].environment[0].w<0.5)return vec3(0);
                LIGHTP float metal=clamp(material.z*2.0,0.0,1.0);
                LIGHTP vec3 f0=mix(vec3(0.04),clamp(albedo,0.0,0.85),metal);
                LIGHTP float f=1.0-clamp(abs(dot(N,V)),0.0,1.0),f2=f*f;
                LIGHTP vec3 fresnel=f0+(1.0-f0)*f2*f2*f;
                highlights+=environmentSpecular*fresnel;
                return environmentDiffuse*(1.0-metal*0.85);
            }

            LIGHTP vec3 pixelLighting(LIGHTP vec3 albedo, out LIGHTP vec3 highlights)
            {
                int index = textureBinds.y;
                LIGHTP vec4 ambientScale = meshLights[index].ambientScale;
                int count = meshLights[index].meta.x;
                LIGHTP vec3 N = meshNormal * inversesqrt(max(dot(meshNormal,meshNormal),1e-12));
                LIGHTP vec3 total = vec3(0.0);
                highlights=vec3(0);
                LIGHTP vec3 viewDirection=normalize(meshLights[index].viewCamera.xyz-meshPosition);
                LIGHTP vec4 material=meshLights[index].material;
                N=mappedNormal(index,meshPosition,texCoord,N,material);
                int shadowIndex=meshLights[index].meta.w;

                for (int i=0; i<8; i++)
                {
                    if (i >= count) break;
                    vec4 positionRadius = meshLights[index].lights[i].positionRadius;
                    vec3 L = positionRadius.xyz - meshPosition;
                    float lensqr = dot(L,L);
                    float distsqr = lensqr * positionRadius.w;
                    if (distsqr < 1.0)
                    {
                        float v = sqrt(distsqr + 0.0001);
                        LIGHTP float falloff = min((1.0 + 2.0*v*v*v - 3.0*v*v)/v, 1.0);
                        LIGHTP float d = dot(L * inversesqrt(max(lensqr,1e-12)), N);
                        LIGHTP vec3 light=meshLights[index].lights[i].color.rgb;
                        #ifdef CULL_LIGHTING
                        // Back-facing one-sided light has zero diffuse and specular
                        // weight. Keep the authored two-sided mesh path intact.
                        if(((flags & 131072u)==0u && d<=0.0) || all(equal(light,vec3(0)))) continue;
                        #endif
                        if(remastered())
                        {
                            LIGHTP vec3 lightDir=L*inversesqrt(max(lensqr,1e-12));
                            int slot=int((uint(shadowIndex)>>(i*SHADOW_SLOT_BITS))&SHADOW_SLOT_MASK)-1;
                            LIGHTP float visibility=movingShadow(slot,meshPosition,N,lightDir);
                            LIGHTP vec3 energy=light*falloff*visibility;
                            total+=energy*((flags & 131072u)!=0u ? abs(d) : max(d,0.0))*localLightBoost(v);
                            highlights+=materialHighlight(viewDirection,N,lightDir,energy*3.0,material,albedo)*max(d,0.0);
                        }
                        else total += light * falloff * ((flags & 131072u)!=0u ? abs(d) : max(d,0.0));
                    }
                }
                // Bound diffuse illumination like the original; specular
                // highlights retain their separate headroom and shadowing.
                if(remastered()) {
                    vec3 bounce=environmentLighting(index,N,viewDirection,material,albedo,highlights);
                    return clamp((ambientScale.rgb*0.72+total*ambientScale.a)*3.0+bounce,0.0,1.0);
                }
                return min((ambientScale.rgb + total*ambientScale.a)*3.0, vec3(1.0));
            }

            #ifdef NO_DISCARD
            // Opaque draws cannot discard coverage. Let depth rejection happen
            // before the expensive lighting shader; fading/masked draws use
            // separate shaders so their holes still preserve background depth.
            layout(early_fragment_tests) in;
            #endif
			layout(location = 0) out vec4 outColor;
			#ifndef NO_HIT_OUTPUT
			layout(location = 1) out uint outHitIndex;
			#endif

			vec4 darkClamp(vec4 c)
			{
				// Make all textures a little darker as some of the textures (i.e coronas) never become completely black as they should have
				float cutoff = 3.1/255.0;
				return vec4(clamp((c.rgb - cutoff) / (1.0 - cutoff), 0.0, 1.0), c.a);
			}

			vec4 textureTex(vec2 uv) { return texture(textures[nonuniformEXT(textureBinds.x)], uv); }
			vec4 textureMacro(vec2 uv) { return texture(textures[nonuniformEXT(textureBinds.y)], uv); }
			vec4 textureDetail(vec2 uv) { return texture(textures[nonuniformEXT(textureBinds.z)], uv); }
			vec4 textureLightmap(vec2 uv) { return texture(textures[nonuniformEXT(textureBinds.w)], uv); }

            LIGHTP vec3 surfacePixelLighting(LIGHTP vec3 baseLight,vec2 materialUV,LIGHTP vec3 albedo,out LIGHTP vec3 highlights)
            {
                int index = int(flags >> 20);
                ivec4 meta = meshLights[index].meta;
                vec4 origin = meshLights[index].surfaceOrigin;
                vec4 u = meshLights[index].surfaceU, v = meshLights[index].surfaceV;
                // Like meshPosition, receiver coordinates must stay inside the
                // covered primitive at MSAA edges for conservative CPU bounds.
                #ifdef SHADOW_RECEIVER_BOUNDS
                vec2 receiverUV=interpolateAtCentroid(texCoord2);
                #else
                vec2 receiverUV=texCoord2;
                #endif
                vec3 position = origin.xyz + u.xyz * receiverUV.x + v.xyz * receiverUV.y;
                LIGHTP vec3 N = vec3(origin.w, u.w, v.w);
                float contact=contactShadow(position,N,int(meshLights[index].surfaceTangent.w));
                highlights=vec3(0);
                // Ambient-only floors also receive contact shadows, retaining
                // their existing baked brightness everywhere outside the feet.
                if(meta.x==0 && meshLights[index].environment[0].w<0.5) return baseLight*contact;
                LIGHTP vec4 shadow0 = texture(textures[nonuniformEXT(meta.y)], texCoord2);
                LIGHTP vec4 shadow1 = meta.x > 4 ? texture(textures[nonuniformEXT(meta.z)], texCoord2) : vec4(0);
                #ifdef CULL_LIGHTING
                // Fully occluded baked light masks cannot contribute diffuse or
                // specular lighting. Avoid normal/response maps and the light loop.
                if(meshLights[index].environment[0].w<0.5 && all(lessThanEqual(shadow0,vec4(0))) && all(lessThanEqual(shadow1,vec4(0))))
                    return baseLight*(remastered()?0.72:1.0)*contact;
                #endif
                if(remastered() && (flags & 1024u)!=0u && meshLights[index].materialMaps.y==0)
                {
                    vec3 packed=texture(textures[nonuniformEXT(textureBinds.y)],materialUV).rgb*2.0-1.0;
                    vec3 T=normalize(u.xyz), B=normalize(v.xyz);
                    float fade=1.0-smoothstep(300.0,850.0,length(reliefViewPosition));
                    float strength=float((flags>>12)&3u)*0.30*fade;
                    N=normalize(N+strength*(T*packed.x+B*packed.y));
                }
                float time = meshLights[index].ambientScale.w;
                LIGHTP vec3 total = baseLight*(remastered() && meta.x>0?0.72:1.0);
                LIGHTP vec3 viewDirection=normalize(meshLights[index].viewCamera.xyz-position);
                LIGHTP vec4 material=meshLights[index].material;
                N=mappedNormal(index,position,materialUV,N,material);
                for (int i=0; i<8; i++)
                {
                    if (i >= meta.x) break;
                    LIGHTP float shadow = i < 4 ? shadow0[i] : shadow1[i-4];
                    #ifdef CULL_LIGHTING
                    // Exactly-zero baked visibility cannot contribute diffuse
                    // or specular energy; avoid all subsequent dynamic work.
                    if(shadow<=0.0) continue;
                    #endif
                    vec4 positionRadius = meshLights[index].lights[i].positionRadius;
                    vec3 L = positionRadius.xyz - position;
                    float lensqr = dot(L,L), distsqr = lensqr * positionRadius.w;
                    if (distsqr < 1.0)
                    {
                        LIGHTP vec4 lightColor = meshLights[index].lights[i].color;
                        // Preserve the authored two-sided BSP response and shadow
                        // masks. Only evaluate distance/angle continuously here.
                        float distance = sqrt(max(lensqr,1e-12));
                        float unitDistance = max(sqrt(distsqr),1e-6);
                        LIGHTP float falloff = min((1.0+2.0*unitDistance*unitDistance*unitDistance-3.0*distsqr)/unitDistance,1.0);
                        LIGHTP float angle = abs(dot(L / distance,N));
                        LIGHTP float wave = lightColor.w == 0.0 ? 1.0 : 0.6+0.4*sin(distance*0.04+time*lightColor.w);
                        if(remastered())
                        {
                            int slot=int((uint(meta.w)>>(i*SHADOW_SLOT_BITS))&SHADOW_SLOT_MASK)-1;
                            LIGHTP float visibility=movingShadow(slot,position,N,L/distance);
                            LIGHTP vec3 energy=lightColor.rgb*falloff*wave*shadow*visibility;
                            total+=energy*angle*localLightBoost(unitDistance);
                            LIGHTP vec3 facingN=dot(N,(OpenRuneFixedPbr || material.w>0.5)?viewDirection:L)>=0.0?N:-N;
                            highlights+=materialHighlight(viewDirection,facingN,L/distance,energy*2.0,material,albedo)*angle;
                        }
                        else total += min(lightColor.rgb * falloff * angle * wave * shadow,vec3(1.0));
                    }
                }
                if(remastered())total+=environmentLighting(index,N,viewDirection,material,albedo,highlights);
                highlights*=contact;
                return clamp(total,0.0,1.0)*contact;
            }

			void main()
			{
				float actorXBlending = (flags & 32) != 0 ? 1.5 : 1.0;
				float oneXBlending = (flags & 64) != 0 ? 1.0 : 2.0;
                bool remasteredSurface=(flags & 262144u)!=0u && (meshLights[int(flags>>20)].meta.x>0 || meshLights[int(flags>>20)].environment[0].w>0.5);
                bool remasteredShading=remastered() && ((flags & 65536u)!=0u || remasteredSurface);

				#ifdef WATER_SURFACE
                const bool water=true;
                #else
                const bool water=false;
                #endif
                vec2 wave=vec2(0);
                #ifdef WATER_SURFACE
                if(waterParameters.w>1.5) {
                    float t=waterParameters.x;
                    vec2 p=meshPosition.xy;
                    // Four directional bands. Derivative-based fade removes
                    // subpixel waves/glints in distant/grazing water.
                    vec4 phase=vec4(dot(p,vec2(.035,.013))-t*.65,
                                    dot(p,vec2(-.079,.156))+t*1.23,
                                    dot(p,vec2(.322,.179))-t*1.87,
                                    dot(p,vec2(-.571,.346))+t*2.43);
                    // Cross-modulate the short ripples to avoid long, perfectly
                    // repeating sine ridges while keeping the cost bounded.
                    phase.zw+=.55*sin(vec2(phase.y,phase.x)*1.37);
                    vec4 bandFade=1.0-smoothstep(vec4(.45),vec4(2.2),fwidth(phase));
                    vec4 slopes=cos(phase)*bandFade*vec4(.10,.16,.11,.065);
                    vec2 gradient=slopes.x*vec2(.938,.349)+slopes.y*vec2(-.452,.892)+slopes.z*vec2(.874,.486)+slopes.w*vec2(-.855,.518);
                    vec3 N=normalize(vec3(-gradient,1));
                    vec3 V=normalize(lightingCamera.xyz-meshPosition);
                    if(V.z<0.0)N=-N;
                    float nv=clamp(dot(N,V),0.0,1.0);
                    float fresnel=.02+.98*pow(1.0-nv,5.0);
                    vec3 sourceColor=darkClamp(textureTex(texCoord+gradient*.012)).rgb;
                    vec3 authored=sourceColor;
                    float sourceLuma=dot(authored,vec3(.2126,.7152,.0722));
                    vec3 hue=clamp(authored/max(sourceLuma,.06),vec3(.35),vec3(1.8));
                    // Original fluid textures often contain large cloudy white
                    // patches. Retain their hue, not those baked pseudo-waves.
                    authored=vec3(.21,.32,.36)*mix(vec3(1),hue,.35)*(.75+.25*sourceLuma);
                    vec3 lighting=(flags&1u)!=0u?clamp(textureLightmap(texCoord2).rgb*2.0,vec3(0),vec3(1)):color.rgb;
                    vec3 base=clamp(authored*lighting,vec3(0),vec3(1));
                    float brightness=dot(base,vec3(.2126,.7152,.0722));
                    float depth=clamp(color.a,1.0,2048.0);
                    float deep=1.0-exp(-depth*.006);
                    // Keep the map's colour and exposure, with a restrained
                    // cool absorption gradient rather than opaque blue paint.
                    vec3 body=mix(base*vec3(.78,1.02,1.08),base*vec3(.38,.68,.78),deep);
                    body*=.91+.09*sin(phase.x+sin(phase.y)*.65);
                    vec3 R=reflect(-V,N);
                    vec3 environment=waterWorldToClip[0].rgb;
                    float outdoors=waterWorldToClip[0].w;
                    // Indoor caustic textures are often the only authored light
                    // cues on translucent water. Preserve their energy rather
                    // than replacing a bright basin with dark absorption.
                    if(outdoors<.5)body=mix(body,sourceColor*lighting*.65,.65);
                    vec3 room=body*.85+environment*.15;
                    vec3 sky=mix(room,max(environment,vec3(.04,.06,.08)),outdoors);
                    float horizon=pow(1.0-clamp(abs(R.z),0.0,1.0),3.0);
                    if(outdoors>.5 && waterWorldToClip[3].x>.5) {
                        int skyIndex=int(waterWorldToClip[3].x)-1;
                        vec2 skyUV=R.xy/(abs(R.z)+.35)*.65+vec2(waterParameters.x*.001,0);
                        vec3 skyDetail=texture(textures[nonuniformEXT(skyIndex)],skyUV).rgb;
                        sky=mix(sky,skyDetail,.65);
                    }
                    sky*=.75+.55*horizon;
                    float direct=waterWorldToClip[1].w;
                    vec3 lightVector=direct>.01?waterWorldToClip[1].xyz-meshPosition:waterWorldToClip[1].xyz;
                    vec3 L=lightVector*inversesqrt(max(dot(lightVector,lightVector),1e-8));
                    if(direct<.01) {L=normalize(vec3(.35,-.45,1));direct=outdoors*.65;}
                    vec3 H=V+L;H*=inversesqrt(max(dot(H,H),1e-8));
                    float roughness=.12+clamp(length(fwidth(N))*1.3,0.0,.25);
                    float power=clamp(2.0/(roughness*roughness)-2.0,12.0,150.0);
                    float sparkle=pow(max(dot(N,H),0.0),power)*direct;
                    vec3 lamp=waterWorldToClip[2].rgb;
                    lamp=mix(vec3(.95,.98,1),clamp(lamp,vec3(0),vec3(1)),step(.01,dot(lamp,lamp)));
                    vec3 waterColor=mix(body,sky,.18+.78*fresnel)+lamp*sparkle*(.35+brightness*.7);
                    float crest=dot(sin(phase)*bandFade,vec4(.45,.30,.17,.08));
                    float foam=smoothstep(.82,.99,crest)*.09*(.3+.7*brightness);
                    waterColor=mix(waterColor,vec3(brightness*.85),foam);
                    if((flags&8u)!=0u) {vec4 fog=textureDetail(texCoord4);waterColor=mix(waterColor,clamp(fog.rgb/max(fog.a,.001),vec3(0),vec3(1)),clamp(fog.a,0.0,.25));}
                    float opacity=(flags&131072u)!=0u?1.0:clamp(.18+.25*deep+.48*fresnel,.18,.95);
                    outColor=vec4(clamp(waterColor,vec3(0),vec3(1.2))*opacity,opacity);
                    #ifndef NO_HIT_OUTPUT
                    outHitIndex=hitIndex;
                    #endif
                    return;
                }
                #endif
                if(water) {
                    float t=waterParameters.x;
                    vec2 p=meshPosition.xy;
                    wave=vec2(sin(dot(p,vec2(.035,.013))+t*1.1)+.45*sin(dot(p,vec2(-.019,.06))-t*1.7),
                              cos(dot(p,vec2(.021,-.033))-t*.9)+.4*cos(dot(p,vec2(.057,.017))+t*1.5));
                }
                vec2 materialUV=texCoord+wave*0.009;
                float reliefShade=1.0;
                if ((flags & 512) != 0)
                {
                    vec2 du=dFdx(texCoord),dv=dFdy(texCoord);
                    float fade=1.0-smoothstep(220.0,650.0,length(reliefViewPosition));
                    if((flags & 2048)!=0 && fade>0.0)
                    {
                        vec3 px=dFdx(reliefViewPosition),py=dFdy(reliefViewPosition);
                        float determinant=du.x*dv.y-du.y*dv.x;
                        if(abs(determinant)>1e-10)
                        {
                            vec3 T=(px*dv.y-py*du.y)/determinant;
                            vec3 B=(py*du.x-px*dv.x)/determinant;
                            float tl=length(T),bl=length(B);
                            T/=max(tl,0.0001); B/=max(bl,0.0001);
                            vec3 N=normalize(cross(T,B));
                            vec3 V=normalize(-reliefViewPosition);
                            if(dot(N,V)<0.0) N=-N;
                            vec3 tangentView=vec3(dot(V,T),dot(V,B),dot(V,N));
                            // Quality scales actual ray steps and depth, never geometry.
                            int quality=int((flags>>14)&3u);
                            int steps=quality==1 ? 4 : quality==3 ? 16 : 8;
                            float height=quality==1 ? 0.35 : quality==3 ? 1.05 : 0.7;
                            // Explicit gradients keep mip choice stable through the loop.
                            vec2 stepUV=tangentView.xy/max(tangentView.z,0.3)*vec2(height/max(tl,1.0),height/max(bl,1.0))*fade/float(steps);
                            vec2 cursor=texCoord;
                            float layer=0.0;
                            for(int i=0;i<steps;i++)
                            {
                                float depth=1.0-textureGrad(textures[nonuniformEXT(textureBinds.y)],cursor,du,dv).a;
                                if(layer>=depth) break;
                                cursor-=stepUV;layer+=1.0/float(steps);
                            }
                            materialUV=cursor;
                        }
                    }
                    // Tangent-space lighting is constant per surface: compute it
                    // on the CPU, avoiding a derivative basis at every normal-only pixel.
                    if((flags & 1024)!=0 && fade>0.0 && dot(color.rgb,color.rgb)>0.1)
                    {
                        vec3 packed=normalize(textureGrad(textures[nonuniformEXT(textureBinds.y)],materialUV,du,dv).rgb*2.0-1.0);
                        vec3 L=normalize(color.rgb);
                        int quality=int((flags>>12)&3u);
                        float strength=quality==1 ? 0.25 : quality==3 ? 0.70 : 0.45;
                        reliefShade=1.0+strength*fade*(max(dot(packed,L),0.0)-max(L.z,0.0));
                    }
                }
                vec4 albedo=darkClamp(textureTex(materialUV));
                vec3 highlights=vec3(0);
                vec4 lightingColor=color;
                if((flags & 65536u)!=0u) lightingColor=vec4(pixelLighting(albedo.rgb,highlights),color.a);
                outColor = albedo * ((flags & 512)!=0 ? vec4(1.0) : lightingColor);
                if(remastered() && remasteredSurface) reliefShade=1.0;
                outColor.rgb *= reliefShade;
				outColor.rgb *= actorXBlending;

				if ((flags & 2) != 0) // Macro texture
				{
					outColor *= darkClamp(textureMacro(texCoord3));
				}

				if ((flags & 1) != 0) // Lightmap
				{
					vec3 surfaceLight = textureLightmap(texCoord2).rgb;
                    if ((flags & 262144u)!=0u) surfaceLight = surfacePixelLighting(surfaceLight,materialUV,albedo.rgb,highlights);
                    outColor.rgb *= (remastered() && (flags & 262144u)!=0u ? surfaceLight : clamp(surfaceLight, 0.0, 1.0)) * oneXBlending;
				}

                if(remasteredShading)
                {
                    outColor.rgb+=highlights;
                    vec3 excess=max(outColor.rgb-vec3(0.65),vec3(0));
                    outColor.rgb=min(outColor.rgb,vec3(0.65))+0.55*(1.0-exp(-excess/0.55));
                }
                float waterOpacity=1.0;
                if(water) {
                    vec3 N=normalize(vec3(wave*.16,1));
                    vec3 V=normalize(lightingCamera.xyz-meshPosition);
                    float fresnel=.045+.955*pow(1.0-clamp(dot(N,V),0.0,1.0),5.0);
                    if(waterParameters.w>.5) {
                        // Analytic environment approximation: the authored lit
                        // water supplies local color/brightness, avoiding a blue
                        // sky pasted into caves. No reflected objects or image.
                        vec3 R=reflect(-V,N);
                        float horizon=pow(1.0-clamp(abs(R.z),0.0,1.0),3.0);
                        float illumination=clamp(dot(outColor.rgb,vec3(.2126,.7152,.0722)),0.0,1.0);
                        vec3 tint=mix(outColor.rgb,vec3(illumination),.45);
                        vec3 environment=tint*(.65+.70*horizon)+vec3(.12,.15,.17)*illumination;
                        float glint=pow(max(dot(R,normalize(vec3(.35,-.45,1))),0.0),32.0);
                        outColor.rgb=mix(outColor.rgb,environment,.18+.70*fresnel)
                            +vec3(.55,.62,.65)*illumination*glint*.35;
                    } else {
                        vec4 projected=waterWorldToClip*vec4(meshPosition,1);
                        vec2 uv=projected.xy/max(projected.w,.001)*.5+.5;
                        float edge=1.0-smoothstep(.87,1.0,max(abs(uv.x*2-1),abs(uv.y*2-1)));
                        vec3 reflected=texture(waterReflection,clamp(uv+wave*.006,vec2(.002),vec2(.998))).rgb;
                        float reflectionWeight=clamp((.16+.80*fresnel)*edge,0.0,.96);
                        outColor.rgb=mix(outColor.rgb,reflected,reflectionWeight);
                    }
                    waterOpacity=(flags&131072u)!=0u?1.0:.40+.54*fresnel;
                    outColor.a=1.0;
                }
                if ((flags & 4) != 0) // Detail texture
				{
					float fadedistance = 380.0f;
					float a = clamp(2.0f - (1.0f / gl_FragCoord.w) / fadedistance, 0.0f, 1.0f);
					vec4 detailColor = (textureDetail(texCoord4) - 0.5) * 0.8 + 1.0;
					outColor.rgb = mix(outColor.rgb, outColor.rgb * detailColor.rgb, a);
				}
				else if ((flags & 8) != 0) // Fog map
				{
					vec4 fogcolor = textureDetail(texCoord4);
					outColor.rgb = fogcolor.rgb + outColor.rgb * (1.0 - fogcolor.a);
				}
				else if ((flags & 16) != 0) // Fog color
				{
					vec4 fogcolor = vec4(texCoord2, texCoord3);
					outColor.rgb = fogcolor.rgb + outColor.rgb * (1.0 - fogcolor.a);
				}

				#if defined(ALPHATEST)
				if (outColor.a < 0.5) discard;
				#endif

				float ceiling = remasteredShading ? 1.2 : 1.0;
                outColor = clamp(outColor, vec4(0), vec4(vec3(ceiling), 1.0));
				if(water)outColor*=waterOpacity;
                // Premultiplied actor opacity, after fog and texture alpha testing.
				// Masked texels stay holes even when the whole actor is fading.
				if ((flags & 128) != 0) outColor *= clamp(texCoord4.x, 0.0, 1.0);
                #ifndef NO_DISCARD
				if ((flags & 256) != 0)
				{
					// All player/equipment surfaces share the same screen coverage.
					// Surviving samples write depth, so eyes/front faces cannot bleed
					// through the back while the world remains visible in the gaps.
					const int bayer[64] = int[64](
						0,32,8,40,2,34,10,42, 48,16,56,24,50,18,58,26,
						12,44,4,36,14,46,6,38, 60,28,52,20,62,30,54,22,
						3,35,11,43,1,33,9,41, 51,19,59,27,49,17,57,25,
						15,47,7,39,13,45,5,37, 63,31,55,23,61,29,53,21);
					ivec2 pixel = ivec2(gl_FragCoord.xy) & ivec2(7);
					float threshold = (float(bayer[pixel.y * 8 + pixel.x]) + 0.5) / 64.0;
					if (clamp(texCoord4.x, 0.0, 1.0) < threshold) discard;
				}

                #endif
				#ifndef NO_HIT_OUTPUT
			outHitIndex = hitIndex;
			#endif
			}
		)";
	}
	else if (filename == "shaders/PPStep.vert")
	{
		return R"(
			layout(location = 0) out vec2 texCoord;

			vec2 positions[6] = vec2[](
				vec2(-1.0, -1.0),
				vec2( 1.0, -1.0),
				vec2(-1.0,  1.0),
				vec2(-1.0,  1.0),
				vec2( 1.0, -1.0),
				vec2( 1.0,  1.0)
			);

			void main()
			{
				vec4 pos = vec4(positions[gl_VertexIndex], 0.0, 1.0);
				gl_Position = pos;
				texCoord = pos.xy * 0.5 + 0.5;
			}
		)";
	}
	else if (filename == "shaders/Present.frag")
	{
		return R"(
			layout(push_constant) uniform PresentPushConstants
			{
				float Contrast;
				float Saturation;
				float Brightness;
				float HdrScale;
				vec4 GammaCorrection;
				vec4 TextureRow0;
				vec4 TextureRow1;
                vec4 SourceSize;
                int Upscale;
				float AdaptiveSharpness;
				float ShadowLift;
				float AmbientOcclusion;
				int AmbientSamples;
                vec4 Underwater;
			};

			layout(binding = 0) uniform sampler2D texSampler;
			layout(binding = 1) uniform sampler2D texDither;
			#ifdef AMBIENT_OCCLUSION
			#ifdef DEPTH_MULTISAMPLED
			layout(binding = 2) uniform sampler2DMS texDepth;
			#else
			layout(binding = 2) uniform sampler2D texDepth;
			#endif
			#endif
			layout(location = 0) in vec2 texCoord;
			layout(location = 0) out vec4 outColor;

        )" + std::string(SGSRShader) + std::string(CASShader) + R"(
			#ifdef AMBIENT_OCCLUSION
			float sceneDepth(ivec2 pixel)
			{
				#ifdef DEPTH_MULTISAMPLED
				pixel = clamp(pixel, ivec2(0), textureSize(texDepth) - ivec2(1));
				#else
				pixel = clamp(pixel, ivec2(0), textureSize(texDepth, 0) - ivec2(1));
				#endif
				return texelFetch(texDepth, pixel, 0).r;
			}
			float viewDistance(float depth)
			{
				return 1.0 / max(1.0000305 - (depth - 0.1) / 0.9, 0.000031);
			}
			float ambientVisibility(vec2 uv)
			{
				ivec2 pixel = ivec2(uv * SourceSize.zw);
				float center = sceneDepth(pixel);
				if (center < 0.15 || center > 0.9999) return 1.0;
				float centerDistance = viewDistance(center);
				const ivec2 offsets[4] = ivec2[4](
					ivec2(12,0), ivec2(0,12), ivec2(28,28), ivec2(28,-28));
				float range = max(8.0, centerDistance * 0.20);
				float occlusion = 0.0;
				for (int i = 0; i < AmbientSamples / 2; i++)
				{
					float positiveDistance = viewDistance(sceneDepth(pixel + offsets[i]));
					float negativeDistance = viewDistance(sceneDepth(pixel - offsets[i]));
					float delta = centerDistance - 0.5 * (positiveDistance + negativeDistance);
					occlusion += smoothstep(0.5, 4.0, delta) *
						(1.0 - smoothstep(range * 0.25, range, delta));
				}
				return 1.0 - AmbientOcclusion * occlusion / float(AmbientSamples / 2);
			}
			#endif
			vec3 liftShadows(vec3 color)
			{
				if (ShadowLift <= 0.0) return color;
				vec3 c = clamp(color, 0.0, 1.0);
				float luminance = dot(c, vec3(0.2126, 0.7152, 0.0722));
				float gain = 1.0 + ShadowLift * pow(1.0 - luminance, 3.0);
				// Keep black black, preserve channel ratios and avoid clipping highlights.
				gain = min(gain, 1.0 / max(max(c.r, max(c.g, c.b)), 0.0001));
				return c * gain;
			}
			vec3 dither(vec3 c)
			{
				vec2 texSize = vec2(textureSize(texDither, 0));
				float threshold = texture(texDither, gl_FragCoord.xy / texSize).r;
				return floor(c.rgb * 255.0 + threshold) / 255.0;
			}

			vec3 linearHdr(vec3 c)
			{
				return pow(c, vec3(2.2)) * HdrScale;
			}

			#if defined(GAMMA_MODE_D3D9)

			vec3 gammaCorrect(vec3 c)
			{
				return pow(c, GammaCorrection.xyz);
			}

			#elif defined(GAMMA_MODE_XOPENGL)

			// Returns maximum of first 3 components
			float max3(vec3 v)
			{
				return max(max(v.x, v.y), v.z);
			}
			float max3(vec4 v)
			{
				return max(max(v.x, v.y), v.z);
			}

			// Returns square of argument
			float square_f( float f)
			{
				return f*f;
			}

			vec3 gammaCorrect(vec3 c)
			{
				c = clamp(c, 0.0, 1.0); // XOpenGLDrv doesn't use a half-float scene buffer

				if (GammaCorrection.w > 1.0)
				{
					// Obtains a multiplier required to offset value according to the following
					// formula: ((1 - (2 * value - 1)^2) * 0.25)
					// It has the shape of a parabola with roots in 0,1 and maximum at f(x=0.5)=0.25
					float CCValue = max(max3(c), 0.001);
					float CC = (1.0 - square_f(2.0 * CCValue - 1.0)) * 0.25  * (GammaCorrection.w - 1.0);
					c = clamp( c * ((CCValue+CC) / CCValue), 0.0, 1.0);
				}
				else if (GammaCorrection.w < 1.0)
				{
					// Downscale brightness
					c *= GammaCorrection.w;
				}

				return pow(c, GammaCorrection.xyz);
			}

			#endif

			#if defined(COLOR_CORRECT_MODE0)
			vec3 colorCorrect(vec3 c)
			{
				float v = c.r + c.g + c.b;
				vec3 valgray = vec3(v, v, v) * (1 - Saturation) / 3 + c * Saturation;
				vec3 val = valgray * Contrast - (Contrast - 1.0) * 0.5;
				val += Brightness * 0.5;
				return max(val, vec3(0.0, 0.0, 0.0));
			}
			#elif defined(COLOR_CORRECT_MODE1)
			vec3 colorCorrect(vec3 c)
			{
				float v = dot(c, vec3(0.3, 0.56, 0.14));
				vec3 valgray = mix(vec3(v, v, v), c, Saturation);
				vec3 val = valgray * Contrast - (Contrast - 1.0) * 0.5;
				val += Brightness * 0.5;
				return max(val, vec3(0.0, 0.0, 0.0));
			}
			#elif defined(COLOR_CORRECT_MODE2)
			vec3 colorCorrect(vec3 c)
			{
				float v = pow(dot(pow(c, vec3(2.2, 2.2, 2.2)), vec3(0.2126, 0.7152, 0.0722)), 1.0/2.2);
				vec3 valgray = mix(vec3(v, v, v), c, Saturation);
				vec3 val = valgray * Contrast - (Contrast - 1.0) * 0.5;
				val += Brightness * 0.5;
				return max(val, vec3(0.0, 0.0, 0.0));
			}
			#else
			vec3 colorCorrect(vec3 c) { return c; }
			#endif

			void main()
			{
				vec3 coord = vec3(texCoord, 1.0);
				vec2 sampleCoord = vec2(dot(TextureRow0.xyz, coord), dot(TextureRow1.xyz, coord));
                if(Underwater.y>0.0) {
                    // A two-pixel world-centre shimmer. Fade completely at HUD
                    // edges, and disable while menus are open. No extra pass.
                    vec2 edge=smoothstep(vec2(.08),vec2(.22),texCoord)*smoothstep(vec2(.08),vec2(.22),vec2(1)-texCoord);
                    float mask=edge.x*edge.y*Underwater.y;
                    float t=Underwater.x;
                    vec2 ripple=vec2(sin(texCoord.y*23.0+t*1.3)+.35*sin(texCoord.y*47.0-t*.7),
                                     sin(texCoord.x*19.0-t*1.1)+.3*sin(texCoord.x*41.0+t*.9));
                    sampleCoord=clamp(sampleCoord+ripple*SourceSize.xy*1.4*mask,SourceSize.xy*.5,vec2(1)-SourceSize.xy*.5);
                }
				vec3 color = Upscale != 0 ? upscaleSGSR(sampleCoord) : adaptiveSharpen(sampleCoord);
				#ifdef AMBIENT_OCCLUSION
				color *= ambientVisibility(sampleCoord);
				#endif
				color = liftShadows(gammaCorrect(colorCorrect(color)));
			#if defined(HDR_MODE)
				outColor = vec4(linearHdr(color), 1.0f);
			#else
				outColor = vec4(dither(color), 1.0f);
			#endif
			}
		)";
	}
	else if (filename == "shaders/BloomExtract.frag")
	{
		return R"(
			layout(push_constant) uniform BloomPushConstants
			{
				float SampleWeights0;
				float SampleWeights1;
				float SampleWeights2;
				float SampleWeights3;
				float SampleWeights4;
				float SampleWeights5;
				float SampleWeights6;
				float SampleWeights7;
				float Intensity;
				float Threshold;
			};

			layout(binding = 0) uniform sampler2D texSampler;
			layout(location = 0) in vec2 texCoord;
			layout(location = 0) out vec4 outColor;

			void main()
			{
				outColor = vec4(max(texture(texSampler, texCoord).rgb - Threshold, 0.0) * Intensity, 0.0);
			}
		)";
	}
	else if (filename == "shaders/BloomCombine.frag")
	{
		return R"(
			layout(push_constant) uniform BloomPushConstants
			{
				float SampleWeights0;
				float SampleWeights1;
				float SampleWeights2;
				float SampleWeights3;
				float SampleWeights4;
				float SampleWeights5;
				float SampleWeights6;
				float SampleWeights7;
				float Intensity;
				float Threshold;
			};

			layout(binding = 0) uniform sampler2D texSampler;
			layout(location = 0) in vec2 texCoord;
			layout(location = 0) out vec4 outColor;

			void main()
			{
				outColor = texture(texSampler, texCoord);
			}
		)";
	}
	else if (filename == "shaders/Blur.frag")
	{
		return R"(
			layout(push_constant) uniform BloomPushConstants
			{
				float SampleWeights0;
				float SampleWeights1;
				float SampleWeights2;
				float SampleWeights3;
				float SampleWeights4;
				float SampleWeights5;
				float SampleWeights6;
				float SampleWeights7;
				float Intensity;
				float Threshold;
			};

			layout(binding = 0) uniform sampler2D texSampler;
			layout(location = 0) in vec2 texCoord;
			layout(location = 0) out vec4 outColor;

			void main()
			{
			#if defined(BLUR_HORIZONTAL)
				outColor =
					textureOffset(texSampler, texCoord, ivec2( 0, 0)) * SampleWeights0 +
					textureOffset(texSampler, texCoord, ivec2( 1, 0)) * SampleWeights1 +
					textureOffset(texSampler, texCoord, ivec2(-1, 0)) * SampleWeights2 +
					textureOffset(texSampler, texCoord, ivec2( 2, 0)) * SampleWeights3 +
					textureOffset(texSampler, texCoord, ivec2(-2, 0)) * SampleWeights4 +
					textureOffset(texSampler, texCoord, ivec2( 3, 0)) * SampleWeights5 +
					textureOffset(texSampler, texCoord, ivec2(-3, 0)) * SampleWeights6;
			#else
				outColor =
					textureOffset(texSampler, texCoord, ivec2(0, 0)) * SampleWeights0 +
					textureOffset(texSampler, texCoord, ivec2(0, 1)) * SampleWeights1 +
					textureOffset(texSampler, texCoord, ivec2(0,-1)) * SampleWeights2 +
					textureOffset(texSampler, texCoord, ivec2(0, 2)) * SampleWeights3 +
					textureOffset(texSampler, texCoord, ivec2(0,-2)) * SampleWeights4 +
					textureOffset(texSampler, texCoord, ivec2(0, 3)) * SampleWeights5 +
					textureOffset(texSampler, texCoord, ivec2(0,-3)) * SampleWeights6;
			#endif
			}
		)";
	}

	return {};
}
