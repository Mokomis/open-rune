#pragma once
#include <string>
class Engine;class Point;
namespace RuneDesktopMenu {
#if defined(_WIN32) && defined(OPENRUNE_ENABLE_NETWORK)
bool Enabled(const Engine&);void Start(Engine&,bool);void Tick(Engine&);bool Draw(Engine&);
bool Mouse(Engine&,const Point&,int,int=0);bool Key(Engine&,int,bool);bool Text(Engine&,const std::string&);
std::string StatusText(const Engine&);void Stop(Engine&);float LookScale();bool InvertLook();
#else
inline bool Enabled(const Engine&){return false;}inline void Start(Engine&,bool){}inline void Tick(Engine&){}inline bool Draw(Engine&){return false;}
inline bool Mouse(Engine&,const Point&,int,int=0){return false;}inline bool Key(Engine&,int,bool){return false;}inline bool Text(Engine&,const std::string&){return false;}
inline std::string StatusText(const Engine&){return {};}inline void Stop(Engine&){}inline float LookScale(){return 1;}inline bool InvertLook(){return false;}
#endif
}
