#include "Precomp.h"
#include "RuneCharacters.h"
#include "Utils/CommandLine.h"
#include <stdexcept>
namespace RuneCharacters {
openrune::CharacterChoice Selected(bool campaign) {
    int packed=(campaign?Campaign:Multiplayer).load();
    auto arg=commandline->GetArg("",campaign?"--campaign-character":"--character", "");
    int index=packed<0?0:packed&255,skin=packed<0?0:(packed>>8)&255;
    if(!arg.empty()) {
        index=-1;
        for(size_t i=0;i<openrune::Characters.size();++i)
            if(arg==openrune::Characters[i].className || arg==std::to_string(i)){index=int(i);break;}
        skin=commandline->GetArgInt("","--character-skin",0);
    }
    if(index<0 || index>=int(openrune::Characters.size()) || skin<0 || skin>=openrune::Characters[index].skins)
        throw std::runtime_error("Unknown Open Rune character or skin");
    return {uint8_t(index),uint8_t(skin)};
}
}
