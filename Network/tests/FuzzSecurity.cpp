#include "openrune/net/SecureChannel.h"
#include "openrune/net/ClockSync.h"
#include <algorithm>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data,size_t size)
{
    using namespace openrune::net;
    // Public fuzz fixture only. The implementation's invitation generator uses OS entropy.
    InvitationKey key{}; key.fill(1);
    SecureChannel server(true,key,"fuzz",{127,0,0,1,1,2});
    if (size <= 1200) server.receive({data,size});
    server.step(0); server.step(300);
    if (server.state() != SecureState::Established && !server.takePlaintext().empty()) std::abort();
    for (const auto& packet : server.takeDatagrams()) if (packet.size() > 1200) std::abort();
    ClockSync clock;
    for (uint64_t t : {1000,1300,1600})
    {
        auto p = clock.request(t);
        if (!p || !clock.reply(p->id,t+1010,t+1010,t+20)) std::abort();
    }
    auto integer = [&](size_t offset)
    { uint64_t n = 0; for (size_t j = 0; j < 8; ++j) n = (n<<8)|data[offset+j]; return n; };
    uint64_t now = 2000;
    for (size_t i = 0; i+24 <= std::min(size,size_t(256)); i+=24,now+=300)
    {
        auto p = clock.request(now);
        if (!p) continue;
        clock.reply(p->id,integer(i),integer(i+8),now+40);
        if (clock.reply(p->id,integer(i),integer(i+8),now+40)) std::abort();
        if (auto mapped = clock.toLocal(integer(i+16),now+40); mapped && *mapped > now+40) std::abort();
    }
    return 0;
}
