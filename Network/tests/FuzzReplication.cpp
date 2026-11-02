#include "openrune/net/Replication.h"
#include <cstdlib>
using namespace openrune::net;
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data,size_t size)
{
    std::span<const uint8_t> bytes{data,size}; Bytes out;
    auto verify=[&](bool success) { if (!success || out!=Bytes(data,data+size)) std::abort(); };
    InputBatch b; InputFeedback f; SnapshotPart p; Snapshot s; CombatOutcome combat;
    MatchStatus status;MapTravel travel;ActorEvent actor;
    if(decodeActorEvent(7,bytes,actor))verify(encodeActorEvent(7,actor,out));
    if(decodeMatchStatus(7,bytes,status))verify(encodeMatchStatus(7,status,out));
    if(decodeMapTravel(7,bytes,travel))verify(encodeMapTravel(7,travel,out));
    CloseReason close; if(decodeClose(7,bytes,close)) verify(encodeClose(7,close,out));
    if (decodeInputs(7,bytes,b))
    {
        verify(encodeInputs(7,b,out)); InputReceiver r;
        r.receive(b,1000,[](uint64_t t)->std::optional<uint64_t>{return t;});
        r.receive(b,1001,[](uint64_t t)->std::optional<uint64_t>{return t;});
        r.feedback(); r.drainActions(1001); r.held(1500);
    }
    if (decodeFeedback(7,bytes,f)) { verify(encodeFeedback(7,f,out)); InputSender sender; sender.receive(f); }
    if (decodeCombatOutcome(7,bytes,combat)) verify(encodeCombatOutcome(7,combat,out));
    if (decodeSnapshot(bytes,nullptr,s)) verify(encodeSnapshot(s,nullptr,out));
    SnapshotReceiver receiver;
    if (decodePart(7,bytes,p)) { verify(encodePart(7,p,out)); receiver.receive(p,1); receiver.receive(p,2); receiver.feedback(3); }
    // Exercise bounded decompression directly, beyond the authenticated wire header.
    if(size>=31 && size<=MaxSnapshotBytes) {
        SnapshotReceiver compressed;size_t count=(size+SnapshotPartBytes-1)/SnapshotPartBytes;
        for(size_t i=0;i<count;++i) {
            size_t end=std::min(size,(i+1)*SnapshotPartBytes);
            compressed.receive({1,0,1,uint32_t(size),uint8_t(i),uint8_t(count),Bytes(data+i*SnapshotPartBytes,data+end)},i+1);
        }
    }
    // Structured mutations reach reassembly and delta application beyond magic checks.
    World world; Entity e; e.ref={1,1}; world.push_back(e);
    Snapshot base{1,0,1,world}; Bytes full; if (!encodeSnapshot(base,nullptr,full)) std::abort();
    SnapshotPart initial{1,0,1,uint32_t(full.size()),0,1,full}; receiver.resync();
    receiver.receive(initial,10); receiver.commit(1);
    for (size_t offset=0;offset+4<size && offset<256;offset+=5)
    {
        auto changed=world; changed[0].position[0]=data[offset];
        Snapshot delta{uint64_t(offset+2),1,uint64_t(offset+2),changed}; Bytes wire;
        if (!encodeSnapshot(delta,&base,wire)) std::abort();
        auto altered=wire; altered[data[offset+1]%altered.size()]^=data[offset+2];
        Snapshot decoded; decodeSnapshot(altered,&base,decoded);
        SnapshotPart fragment{delta.id,1,delta.tick,uint32_t(altered.size()),0,1,altered};
        if (data[offset+3]&1) fragment.index=data[offset+4];
        receiver.receive(fragment,offset+20); if (receiver.candidate() && (data[offset+3]&2)) receiver.commit(receiver.candidate()->id);
        receiver.feedback(offset+21);
    }
    return 0;
}
