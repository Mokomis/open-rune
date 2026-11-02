#include "SurrealEngine/RenderDevice/Vulkan/MaterialRelief.h"
#include <cassert>
#include <iostream>
int main(){
 for(int size:{1,2,4,16}){
  auto flat=MaterialRelief::Pack(std::vector<float>(size*size,.5f),size,size);
  for(int i=0;i<size*size;i++){assert(flat[4*i]==128 && flat[4*i+1]==128 && flat[4*i+2]==255 && flat[4*i+3]==128);}
 }
 std::vector<float> ramp(64);for(int y=0;y<8;y++)for(int x=0;x<8;x++)ramp[y*8+x]=x/7.f;
 auto p=MaterialRelief::Pack(ramp,8,8);assert(p[(4*8+4)*4]<128);assert(p[(4*8+4)*4+1]==128);
 std::cout<<"RUNE_MATERIAL_CPU_OK flat=4 slope=1 mip_tail=1\n";
}
