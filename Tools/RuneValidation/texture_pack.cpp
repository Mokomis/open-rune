#include "../../SurrealEngine/RenderDevice/TexturePack.h"
#include <cassert>
#include <iostream>
#include <random>

int main(int argc,char** argv)
{
    using namespace TexturePack;
    assert(Key("Players.Head")=="players.head");
    for(auto s : {"../bad","a/b","a\\b","a..b",".a","a.",""}) assert(Key(s).empty());
    auto reject=[](const std::vector<uint8_t>& data) {try{Decode(data);return false;}catch(const std::exception&){return true;}};
    std::vector<uint8_t> valid={0xab,0x4b,0x54,0x58,0x20,0x31,0x31,0xbb,0x0d,0x0a,0x1a,0x0a};
    auto push=[&](uint32_t n){for(int i=0;i<4;i++)valid.push_back(uint8_t(n>>(8*i)));};
    for(uint32_t n : {0x04030201u,0x1401u,1u,0x1908u,RGBA8,0x1908u,2u,1u,0u,0u,1u,2u,0u})push(n);
    push(8);for(int i=0;i<8;i++)valid.push_back(127);push(4);for(int i=0;i<4;i++)valid.push_back(64);
    auto decoded=Decode(valid);assert(decoded.levels.size()==2 && decoded.bytes==12);
    for(size_t i=0;i<valid.size();i++)assert(reject({valid.begin(),valid.begin()+i}));
    for(size_t offset : {12,16,20,24,28,32,36,40,44,48,52,56,60,64,76})
    {auto broken=valid;for(int j=0;j<4;j++)broken[offset+j]=255;assert(reject(broken));}
    auto extra=valid;extra.push_back(0);assert(reject(extra));
    std::mt19937 random(71);
    for(int i=0;i<10000;i++){auto broken=valid;for(int j=0;j<3;j++)broken[random()%broken.size()]=uint8_t(random());try{Decode(broken);}catch(const std::exception&) {}}
    size_t count=0,bytes=0;
    if(argc>1)
    {
        auto index=ReadIndex(std::filesystem::path(argv[1])/"index.tsv");assert(index.size()==4);
        for(auto& f:std::filesystem::recursive_directory_iterator(argv[1]))if(f.path().extension()==".ktx")
        {auto image=Read(f.path());++count;bytes+=image.bytes;assert(image.levels.back().width==1 && image.levels.back().height==1);}
        assert(count==64);
    }
    std::cout<<"Texture pack checks passed; files="<<count<<" payload_bytes="<<bytes<<"\n";
}
