#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <vector>

namespace openrune::net
{
// Local observations only. Never adjusts simulation, transport or peer clocks.
// Fixed bounds also apply when a render loop runs without a frame cap.
class TimingWindow
{
public:
    struct Summary { size_t count=0; double mean=0, p95=0, maximum=0; };
    static constexpr uint64_t HorizonMs=2000;
    static constexpr size_t Capacity=1024;
    bool add(uint64_t now,double value)
    {
        if(!std::isfinite(value) || value<0 || (seen && now<last))return false;
        seen=true;last=now;trim(now);
        if(samples.size()==Capacity)samples.pop_front();
        samples.push_back({now,value});return true;
    }
    Summary summary(uint64_t now)
    {
        if(seen && now<last)return {};
        trim(now);if(samples.empty())return {};
        std::vector<double> values;values.reserve(samples.size());double total=0;
        for(auto s:samples){values.push_back(s.value);total+=s.value;}
        std::sort(values.begin(),values.end());
        return {values.size(),total/values.size(),values[(values.size()-1)*95/100],values.back()};
    }
    void reset(){samples.clear();seen=false;last=0;}
private:
    struct Sample { uint64_t at; double value; };
    std::deque<Sample> samples;
    bool seen=false;uint64_t last=0;
    void trim(uint64_t now){while(!samples.empty() && now-samples.front().at>HorizonMs)samples.pop_front();}
};
}
