#include "openrune/net/Presentation.h"
#include <algorithm>
#include <cmath>

namespace openrune::net
{
void PresentationTimeline::reset()
{
    history.clear();offsets.clear();intervals.clear();started=false;lastNow=0;cursor=offset=0;
    auto resets=metrics.resets+1;metrics={};metrics.resets=resets;
}
bool PresentationTimeline::push(const Snapshot& snapshot,uint64_t arrival)
{
    if(!snapshot.id || !snapshot.tick || snapshot.tick>ClockSync::MaxTime/1000 ||
        arrival>ClockSync::MaxTime || (started && arrival<lastNow) || snapshot.world.size()>MaxEntities)return false;
    if(!history.empty() && (arrival<history.back().arrival || snapshot.id<=history.back().snapshot.id || snapshot.tick<history.back().snapshot.tick))return false;
    const double serverMs=double(snapshot.tick)*(1000.0/60);
    // Don't invent a second of travel after an outage, host stall or suspend.
    if(!history.empty() && (arrival-history.back().arrival>500 || serverMs-history.back().serverMs>500))reset();
    if(!history.empty())intervals.push_back({arrival,serverMs-history.back().serverMs});
    while(!intervals.empty() && (intervals.size()>32 || arrival-intervals.front().first>2000))intervals.pop_front();
    history.push_back({snapshot,arrival,serverMs});
    if(history.size()>Capacity)history.pop_front();
    offsets.push_back({arrival,double(arrival)-serverMs});
    while(!offsets.empty() && (offsets.size()>32 || arrival-offsets.front().first>2000))offsets.pop_front();
    std::vector<double> values;values.reserve(offsets.size());
    for(auto [at,value]:offsets){(void)at;values.push_back(value);}
    std::sort(values.begin(),values.end());offset=values.front();
    // Arrival variation, not RTT: constant path latency doesn't need a bigger buffer.
    double interval=MinimumDelayMs;for(auto [at,value]:intervals){(void)at;interval=std::max(interval,value);}
    metrics.targetDelayMs=std::clamp(interval+values[(values.size()-1)*95/100]-offset,MinimumDelayMs,MaximumDelayMs);
    if(!started){cursor=serverMs;lastNow=arrival;started=true;}
    return true;
}
PresentationTimeline::Frame PresentationTimeline::sample(uint64_t now)
{
    if(history.empty() || now<lastNow || now<history.back().arrival || now>ClockSync::MaxTime)return {};
    const double dt=double(now-lastNow);lastNow=now;
    const double target=double(now)-offset-metrics.targetDelayMs;
    const double oldest=history.front().serverMs,newest=history.back().serverMs;
    if(dt>250)cursor=std::max(cursor,std::clamp(target,oldest,newest));
    else if(target>oldest || cursor>oldest) {
        const double error=target-(cursor+dt);
        // Delay adaptation slews the clock; it must never rewind remote motion.
        cursor+=dt*std::clamp(1+error*.01,.90,1.10);
    }
    const double floor=std::max(oldest,newest-MaximumDelayMs);
    if(cursor<floor){cursor=floor;++metrics.catchups;}
    cursor=std::clamp(cursor,floor,newest);
    metrics.bufferedMs=newest-cursor;
    if(cursor>=newest)++metrics.holds;
    for(size_t i=1;i<history.size();++i)if(history[i].serverMs>=cursor) {
        const auto& a=history[i-1];const auto& b=history[i];
        double span=b.serverMs-a.serverMs;
        return {&a.snapshot,&b.snapshot,span>0?std::clamp((cursor-a.serverMs)/span,0.0,1.0):1};
    }
    return {&history.back().snapshot,&history.back().snapshot,1};
}
}
