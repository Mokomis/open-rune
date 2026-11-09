#pragma once

// Small, bounded reader for our offline KTX1 subset. No driver or game dependencies.
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace TexturePack
{
constexpr size_t MaxFileBytes = 24 * 1024 * 1024;
constexpr size_t BudgetBytes = 128 * 1024 * 1024;
constexpr uint32_t RGBA8 = 0x8058, ASTC4 = 0x93b0, ASTC6 = 0x93b4;
inline uint64_t HashByte(uint64_t h, uint8_t b) { return (h ^ b) * 1099511628211ull; }
inline std::string Key(std::string name)
{
    if (name.empty() || name.size() > 240 || name.front() == '.' || name.back() == '.' || name.find("..") != std::string::npos) return {};
    for (char& c : name)
    {
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-' || c == '~')) return {};
    }
    for(size_t i=0;(i=name.find('~',i))!=std::string::npos;i+=3)
        if(name.substr(i,3)!="~20" && name.substr(i,3)!="~7e") return {};
    return name;
}
inline std::string IdentityKey(const std::string& identity)
{
    std::string encoded;
    for(char c:identity) encoded += c==' ' ? "~20" : c=='~' ? "~7e" : std::string(1,c);
    return Key(encoded);
}
struct Level { int width, height; std::vector<uint8_t> bytes; };
struct Image { uint32_t format = 0; size_t bytes = 0; std::vector<Level> levels; };
inline Image Decode(const std::vector<uint8_t>& data)
{
    static const uint8_t magic[] = {0xab,0x4b,0x54,0x58,0x20,0x31,0x31,0xbb,0x0d,0x0a,0x1a,0x0a};
    if (data.size() < 64 || data.size() > MaxFileBytes || !std::equal(magic, magic+12, data.begin())) throw std::runtime_error("KTX signature/size");
    auto word = [&](size_t offset) -> uint32_t {
        if (offset > data.size()-4) throw std::runtime_error("KTX truncated word");
        return uint32_t(data[offset]) | uint32_t(data[offset+1])<<8 | uint32_t(data[offset+2])<<16 | uint32_t(data[offset+3])<<24;
    };
    Image out; out.format = word(28);
    bool compressed = out.format == ASTC4 || out.format == ASTC6;
    if (word(12)!=0x04030201 || word(20)!=1 || word(32)!=0x1908 ||
        word(40)==0 || word(44)!=0 || word(48)!=0 || word(52)!=1 || word(60)!=0 ||
        (compressed ? (word(16)!=0 || word(24)!=0) : (out.format!=RGBA8 || word(16)!=0x1401 || word(24)!=0x1908)))
        throw std::runtime_error("Unsupported KTX layout");
    uint32_t w=word(36), h=word(40), count=word(56);
    if (!w || w>2048 || h>2048 || (w&(w-1)) || (h&(h-1))) throw std::runtime_error("KTX dimensions");
    uint32_t expectedCount=1;
    for (auto size=std::max(w,h);size>1;size/=2) ++expectedCount;
    if (count!=expectedCount) throw std::runtime_error("KTX requires complete mip chain");
    size_t pos=64;
    for (uint32_t i=0;i<count;++i)
    {
        uint32_t size=word(pos);pos+=4;
        uint32_t block=out.format==ASTC6?6:4;
        size_t expected=compressed?size_t((w+block-1)/block)*((h+block-1)/block)*16:size_t(w)*h*4;
        if (size!=expected || pos>data.size() || size>data.size()-pos) throw std::runtime_error("KTX mip size");
        out.levels.push_back({int(w),int(h),{data.begin()+pos,data.begin()+pos+size}});
        out.bytes+=size;pos+=size;w=std::max(1u,w/2);h=std::max(1u,h/2);
    }
    if (pos!=data.size()) throw std::runtime_error("KTX trailing data");
    return out;
}
inline Image Read(const std::filesystem::path& path)
{
    std::ifstream stream(path,std::ios::binary|std::ios::ate);
    auto size=stream.tellg();
    if (!stream || size<64 || size>std::streamoff(MaxFileBytes)) throw std::runtime_error("KTX missing/oversized");
    std::vector<uint8_t> data(static_cast<size_t>(size));stream.seekg(0);
    if (!stream.read(reinterpret_cast<char*>(data.data()),size)) throw std::runtime_error("KTX read failed");
    return Decode(data);
}
struct Entry { int width=0,height=0; uint64_t hash=0; };
inline std::map<std::string,Entry> ReadIndex(const std::filesystem::path& path)
{
    std::ifstream f(path,std::ios::binary|std::ios::ate);
    if (!f) return {};
    if (f.tellg()>1024*1024) throw std::runtime_error("Texture index too large");
    f.seekg(0);std::string line;
    // The index is portable text. Binary streams retain CR from Windows CRLF.
    auto readLine=[&]() {
        if (!std::getline(f,line)) return false;
        if (!line.empty() && line.back()=='\r') line.pop_back();
        return true;
    };
    if (!readLine() || line!="OPENRUNE_TEXTURES_1") throw std::runtime_error("Texture index version");
    std::map<std::string,Entry> entries;
    while (readLine())
    {
        std::istringstream row(line);std::string name,extra;Entry entry;
        if (!(row>>name>>entry.width>>entry.height>>std::hex>>entry.hash) || row>>extra || Key(name)!=name || name.empty() ||
            entry.width<1 || entry.height<1 || entry.width>1024 || entry.height>1024 || entries.size()>=8192 || !entries.emplace(name,entry).second)
            throw std::runtime_error("Invalid texture index entry");
    }
    return entries;
}
}
