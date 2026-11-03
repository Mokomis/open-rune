#include "SurrealEngine/RuneMenuSections.h"
#include <map>
#include <set>
#include <iostream>
#include <stdexcept>
int main(){
    std::set<std::string> defined,seen;
    std::vector<std::string> shadowChoices;int shadowDefault=-1,shadowMaximum=-1;
    auto add=[&](std::string key,std::string,std::string,std::vector<std::string> choices,int initial,int,int maximum){
        defined.insert(key);
        if(key=="shadowLights"){shadowChoices=choices;shadowDefault=initial;shadowMaximum=maximum;}
    };
    #include "SurrealEngine/RuneDesktopSettings.inc"
    for(const auto& page:RuneMenuSections())for(const auto& key:page.keys){
        if(!defined.contains(key)||!seen.insert(key).second)throw std::runtime_error("Invalid or duplicate setting: "+key);
    }
    if(seen!=defined)throw std::runtime_error("A desktop setting has no parent page");
    if(shadowDefault!=0||shadowMaximum!=4||shadowChoices!=std::vector<std::string>{"One nearby","Three nearby","Four nearby","Six nearby","Eight nearby"})
        throw std::runtime_error("Shadow choices must preserve old indices and expose the Windows budgets");
    std::cout<<"PASS: "<<seen.size()<<" Windows settings each have one parent across "<<RuneMenuSections().size()<<" sections\n";
}
