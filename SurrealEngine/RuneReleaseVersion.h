#pragma once
#include <array>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace RuneUpdates {
struct Version {
    std::array<unsigned,3> core{};
    std::vector<std::string> pre;
    static std::optional<Version> Parse(std::string_view s) {
        if(s.empty() || s.size()>96)return {};
        if(s.front()=='v')s.remove_prefix(1);
        auto ids=[](std::string_view part,bool pre)->bool {
            if(part.empty())return false;
            size_t start=0;
            while(start<part.size()) {
                auto end=part.find('.',start);if(end==part.npos)end=part.size();
                auto id=part.substr(start,end-start);if(id.empty())return false;
                bool numeric=true;
                for(char c:id){if(!(c>='0'&&c<='9'))numeric=false;if(!((c>='0'&&c<='9')||(c>='A'&&c<='Z')||(c>='a'&&c<='z')||c=='-'))return false;}
                if(pre&&numeric&&id.size()>1&&id[0]=='0')return false;
                if(end==part.size())return true;start=end+1;
            }return false;
        };
        auto plus=s.find('+');if(plus!=s.npos){if(!ids(s.substr(plus+1),false))return {};s=s.substr(0,plus);}
        Version out;auto dash=s.find('-');
        if(dash!=s.npos){auto part=s.substr(dash+1);if(!ids(part,true))return {};size_t start=0;while(start<part.size()){auto end=part.find('.',start);if(end==part.npos)end=part.size();out.pre.emplace_back(part.substr(start,end-start));start=end+1;}s=s.substr(0,dash);}
        for(int i=0;i<3;++i){auto end=s.find('.');if(i==2){if(end!=s.npos)return {};end=s.size();}else if(end==s.npos)return {};
            auto n=s.substr(0,end);if(n.empty()||(n.size()>1&&n[0]=='0'))return {};
            auto parsed=std::from_chars(n.data(),n.data()+n.size(),out.core[i]);if(parsed.ec!=std::errc()||parsed.ptr!=n.data()+n.size())return {};
            if(i<2)s.remove_prefix(end+1);
        }return out;
    }
    int Compare(const Version& b) const {
        if(core!=b.core)return core<b.core?-1:1;
        if(pre.empty()||b.pre.empty())return pre.empty()?(b.pre.empty()?0:1):-1;
        for(size_t i=0;i<pre.size()&&i<b.pre.size();++i){auto& a=pre[i];auto& z=b.pre[i];if(a==z)continue;
            auto numeric=[](const std::string& id){return id.find_first_not_of("0123456789")==id.npos;};bool an=numeric(a),bn=numeric(z);
            if(an!=bn)return an?-1:1;
            if(an&&a.size()!=z.size())return a.size()<z.size()?-1:1;
            return a<z?-1:1;
        }return pre.size()==b.pre.size()?0:(pre.size()<b.pre.size()?-1:1);
    }
};
}
