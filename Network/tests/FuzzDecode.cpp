#include "openrune/net/Transport.h"
#include <cstdlib>
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    using namespace openrune::net;
    Packet packet;
    if (decode({data,size},packet))
    {
        Bytes encoded;
        if (!encode(packet,encoded) || encoded != Bytes(data,data+size)) std::abort();
    }
    // Exercise session transitions with valid frames too: random bytes otherwise
    // spend almost all their time failing the magic/version check.
    Transport endpoint(1,1);
    uint64_t now = 0;
    for (size_t i = 0; i+4 < size && i < 256; i += 5)
    {
        now += data[i];
        const auto lane = Lane(data[i+1]%4);
        Packet p{1,1,uint64_t(data[i+2])+1,{},Message{Kind(uint8_t(lane)+1),lane,
            uint64_t(data[i+3])+1,now,uint16_t((lane == Lane::Gameplay || lane == Lane::Snapshots) ? 100 : 0),{data[i+4]}}};
        Bytes bytes;
        if (!encode(p,bytes)) std::abort();
        endpoint.receiveTrusted(bytes,now);
        endpoint.enqueue(lane,{data[i+4]},now);
        endpoint.poll(now);
        if (data[i]&1) endpoint.drain();
        if (endpoint.pending() > 161 || endpoint.buffered() > 161 || endpoint.stats().inFlight > 256*1200) std::abort();
    }
    return 0;
}
