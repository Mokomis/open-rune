#pragma once
#include <array>
#include <string>
#include <string_view>
#include <cstdint>
namespace openrune::net {
inline constexpr size_t MaxPlayerNameBytes=48,MaxPlayerNameCharacters=24;
inline bool validPlayerName(std::string_view s){
 if(s.empty()||s.size()>MaxPlayerNameBytes||s.front()==' '||s.back()==' ')return false;
 size_t i=0,count=0;while(i<s.size()){
  unsigned c=(unsigned char)s[i++],cp=c,n=0;
  if(c>=0xc2&&c<=0xdf){cp=c&31;n=1;}else if(c>=0xe0&&c<=0xef){cp=c&15;n=2;}else if(c>=128)return false;
  if(i+n>s.size())return false;for(unsigned j=0;j<n;++j){unsigned v=(unsigned char)s[i++];if((v&192)!=128)return false;cp=(cp<<6)|(v&63);}
  if((n==1&&cp<128)||(n==2&&cp<2048)||(cp>=0xd800&&cp<=0xdfff))return false;
  if(cp<128){if(!((cp>='A'&&cp<='Z')||(cp>='a'&&cp<='z')||(cp>='0'&&cp<='9')||cp==' '||cp=='-'||cp=='_'||cp=='\''||cp=='.'))return false;}
  else if(cp<160 || cp==0xa0 || cp==0xad || cp==0x34f || cp==0x61c || cp==0x180e || (cp>=0xfe00&&cp<=0xfe0f) || (cp>=0x2000&&cp<=0x206f)||cp==0xfeff||cp>=0xfff0)return false;
  if(++count>MaxPlayerNameCharacters)return false;
 }return count>0;
}
inline std::string trimPlayerName(std::string s){auto a=s.find_first_not_of(' ');if(a==std::string::npos)return {};return s.substr(a,s.find_last_not_of(' ')-a+1);}
struct PlayerProfile {std::string name="Warrior";bool bot=false;bool operator==(const PlayerProfile&)const=default;};
struct PlayerLabel {uint32_t player=0;std::string name;bool bot=false;bool operator==(const PlayerLabel&)const=default;};
inline constexpr std::array<std::string_view,64> BotNames={
    "Torvald Ironpine",
    "Sigrun Frosthand",
    "Eirik Ashwolf",
    "Brynja Stormwake",
    "Haldor Ravenshield",
    "Yrsa Emberborn",
    "Sten Wolfscar",
    "Astrid Snowfang",
    "Bjorn Stonewake",
    "Kari Blackfjord",
    "Runa Oakheart",
    "Skardi Grimholt",
    "Freydis Mistbane",
    "Ulfar Redpeak",
    "Tova Ravensong",
    "Orik Icebrand",
    "Ivar Ironhand",
    "Gudrun Stormborn",
    "Leif Ashshield",
    "Thora Wolfbane",
    "Hakon Frostshield",
    "Inga Longstride",
    "Sigurd Stonewolf",
    "Sif Emberfist",
    "Knut Rimeblade",
    "Ragnhild Raveneye",
    "Egil Oakbreaker",
    "Hilda Icevein",
    "Dag Mistwalker",
    "Alva Crowheart",
    "Finn Redthorn",
    "Valdis Stormspear",
    "Odvar Blackpine",
    "Solveig Ironwill",
    "Hallvard Snowmane",
    "Skadi Frostwake",
    "Svein Wolfmark",
    "Rurik Ashmantle",
    "Gunnar Oathkeeper",
    "Aslaug Ravenfall",
    "Bragi Grimshore",
    "Aud Stoneheart",
    "Rolf Coldwater",
    "Hervor Stormblade",
    "Asger Ironjaw",
    "Svanhild Ravensong",
    "Njal Frostbeard",
    "Thyra Ashwind",
    "Viggo Bearclaw",
    "Vilja Mistborn",
    "Stig Blackstone",
    "Siv Iceheart",
    "Frode Deepfjord",
    "Tove Silverpine",
    "Ketil Stormward",
    "Eydis Wolfmoon",
    "Odd Ravenclaw",
    "Ragnvald Emberhelm",
    "Erling Snowstride",
    "Ylva Ironroot",
    "Orm Blackwake",
    "Svala Frostthorn",
    "Arvid Stonefist",
    "Hildis Ravenspear"
};
inline uint32_t nameHash(uint32_t n){n^=n>>16;n*=0x7feb352du;n^=n>>15;n*=0x846ca68bu;return n^(n>>16);}
// A seeded permutation draws without replacement. The host keeps this seed for
// its whole session, so respawns, reconnects and map travel retain names.
inline std::string botName(uint32_t slot,uint32_t seed=0){uint32_t offset=nameHash(seed)&63,step=(nameHash(seed^0x78ce91b5u)&63)|1;return std::string(BotNames[(offset+(slot?slot-1:0)*step)&63]);}

inline std::string labelText(const PlayerLabel& label,bool scoreboard){return (scoreboard&&label.bot?"[BOT] ":"")+label.name;}
}
