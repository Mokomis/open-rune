#include "surrealwidgets/core/resourcedata.h"
#include <fstream>
#include <stdexcept>

namespace
{
std::vector<uint8_t> ReadBytes(const std::string& filename)
{
	std::ifstream file(filename,std::ios::binary|std::ios::ate);
	if(!file) throw std::runtime_error("Could not open: "+filename);
	auto size=file.tellg();
	if(size<0) throw std::runtime_error("Could not read size: "+filename);
	std::vector<uint8_t> bytes((size_t)size);
	file.seekg(0);
	if(!file.read(reinterpret_cast<char*>(bytes.data()),size)) throw std::runtime_error("Could not read: "+filename);
	return bytes;
}
class AndroidResourceLoader : public ResourceLoader
{
	std::vector<SingleFontData> LoadFont(const std::string& name) override
	{
		if(name=="system") return ResourceData::LoadSystemFont();
		if(name=="monospace") return ResourceData::LoadMonospaceSystemFont();
		return {SingleFontData{ReadBytes(name+".ttf"),""}};
	}
	std::vector<uint8_t> ReadAllBytes(const std::string& filename) override {return ReadBytes(filename);}
};
std::unique_ptr<ResourceLoader>& Loader()
{
	static std::unique_ptr<ResourceLoader> loader=std::make_unique<AndroidResourceLoader>();
	return loader;
}
}
std::vector<SingleFontData> ResourceData::LoadSystemFont()
{
	for(auto* path:{"/system/fonts/Roboto-Regular.ttf","/system/fonts/RobotoStatic-Regular.ttf","/system/fonts/NotoSans-Regular.ttf"})
		if(std::ifstream(path)) return {SingleFontData{ReadBytes(path),""}};
	throw std::runtime_error("Android system font not found");
}
std::vector<SingleFontData> ResourceData::LoadMonospaceSystemFont() {return {SingleFontData{ReadBytes("/system/fonts/DroidSansMono.ttf"),""}};}
double ResourceData::GetSystemFontSize() {return 11.0;}
ResourceLoader* ResourceLoader::Get() {return Loader().get();}
void ResourceLoader::Set(std::unique_ptr<ResourceLoader> loader) {Loader()=std::move(loader);}
