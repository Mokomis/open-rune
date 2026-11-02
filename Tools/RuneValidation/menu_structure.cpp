#include "SurrealEngine/RuneMenuSections.h"
#include <map>
#include <set>
#include <iostream>
#include <stdexcept>
int main(){
    std::set<std::string> defined,seen;
    auto add=[&](std::string key,std::string,std::string,std::vector<std::string>,int,int,int){defined.insert(key);};
    #include "SurrealEngine/RuneDesktopSettings.inc"
    for(const auto& page:RuneMenuSections())for(const auto& key:page.keys){
        if(!defined.contains(key)||!seen.insert(key).second)throw std::runtime_error("Invalid or duplicate setting: "+key);
    }
    if(seen!=defined)throw std::runtime_error("A desktop setting has no parent page");
    std::cout<<"PASS: "<<seen.size()<<" Windows settings each have one parent across "<<RuneMenuSections().size()<<" sections\n";
}
