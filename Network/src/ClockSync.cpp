#include "openrune/net/ClockSync.h"
#include <algorithm>
#include <cstdlib>

namespace openrune::net
{
bool ClockSync::advance(uint64_t now)
{
    if (now > MaxTime || now < lastNow) return false;
    lastNow = now;
    return true;
}
std::optional<ClockSync::Probe> ClockSync::request(uint64_t now)
{
    if (!advance(now) || now < nextRequest || nextId == UINT64_MAX) return {};
    if (pending && now-pending->sent <= 2000) return {};
    if (established && now-lastGood > 5000)
    { established = false; samples.clear(); }
    pending = Probe{nextId++,now};
    nextRequest = now+250;
    return pending;
}
bool ClockSync::reply(uint64_t id, uint64_t t1, uint64_t t2, uint64_t now)
{
    if (!advance(now) || !pending || pending->id != id) { ++rejects; return false; }
    const auto t0 = pending->sent;
    pending.reset(); // A matching reply is single-use, including invalid replies.
    if (t1 > MaxTime || t2 > MaxTime || t2 < t1 || t2-t1 > 250 || now-t0 > 2000 || t2-t1 > now-t0)
    { ++rejects; return false; }
    const auto rtt = now-t0-(t2-t1);
    if (rtt > 500) { ++rejects; return false; }
    const int64_t estimate = ((int64_t(t1)-int64_t(t0))+(int64_t(t2)-int64_t(now)))/2;
    if (established && now-lastGood <= 5000)
    {
        const auto tolerance = int64_t(uncertainty+rtt/2+5+(now-lastGood)/1000);
        if (std::abs(estimate-offset) > tolerance) { ++rejects; return false; }
    }
    while (!samples.empty() && now-samples.front().at > 5000) samples.pop_front();
    samples.push_back({estimate,rtt,now});
    if (samples.size() > 8) samples.pop_front();
    const auto best = std::min_element(samples.begin(),samples.end(),[](auto a,auto b){return a.rtt < b.rtt;});
    if (!established)
    {
        if (samples.size() < 3) return true;
        for (auto s : samples)
            if (std::abs(s.offset-best->offset) > int64_t((s.rtt+best->rtt)/2+5))
            { samples.clear(); ++rejects; return false; }
        offset = best->offset;
        established = true;
    }
    else
    {
        // Bounded slew: do not jump existing presentation history on every probe.
        const auto slew = int64_t(1+(now-lastGood)/1000);
        offset += std::clamp(best->offset-offset,-slew,slew);
    }
    uncertainty = best->rtt/2+1+uint64_t(std::abs(best->offset-offset))+(now-best->at)/1000;
    lastGood = now;
    return true;
}
bool ClockSync::ready(uint64_t now) const
{
    return established && now >= lastNow && now <= MaxTime && now-lastGood <= 5000;
}
std::optional<uint64_t> ClockSync::toLocal(uint64_t time, uint64_t now) const
{
    if (!ready(now) || time > MaxTime) return {};
    const int64_t mapped = int64_t(time)-offset;
    if (mapped < 0 || mapped > int64_t(MaxTime)) return {};
    const auto result = uint64_t(mapped);
    const auto error = uncertainty+(now-lastGood)/1000;
    if (result > now && result-now > error) return {};
    // Tolerated future uncertainty cannot grant future simulation time.
    return std::min(result,now);
}
}
