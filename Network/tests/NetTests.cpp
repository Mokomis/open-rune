#include "openrune/net/Transport.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace openrune::net;
namespace
{
size_t checks = 0;
void check(bool value, const char* expression, int line)
{
    ++checks;
    if (!value) throw std::runtime_error(std::string("line ")+std::to_string(line)+": "+expression);
}
#define CHECK(...) check(bool(__VA_ARGS__), #__VA_ARGS__, __LINE__)
Bytes wire(const Packet& p) { Bytes b; CHECK(encode(p,b)); return b; }
Packet sample()
{
    return {0x0102030405060708ULL,0x11223344,9,{{5,7},{1,2}},
        Message{Kind::State,Lane::Gameplay,3,40,100,{0xaa,0xbb,0xcc}}};
}
void contract()
{
    auto p = sample(); auto b = wire(p); Packet decoded;
    CHECK(b.size() == 91); CHECK(decode(b,decoded)); CHECK(decoded == p);
    for (size_t n = 0; n < b.size(); ++n)
    {
        auto unchanged = p;
        CHECK(!decode(std::span(b).first(n),unchanged)); CHECK(unchanged == p);
    }
    auto bad = b; bad.push_back(0); CHECK(!decode(bad,decoded));
    for (size_t offset : {size_t(0),size_t(4),size_t(6),size_t(28),size_t(29),size_t(30),size_t(64),size_t(65),size_t(66)})
    { bad = b; bad[offset] = 255; CHECK(!decode(bad,decoded)); }
    p.acks = {{1,3},{4,5}}; CHECK(!encode(p,b));
    p.acks = {{4,5},{1,3}}; CHECK(!encode(p,b)); // Adjacent must be merged.
    p.acks = {{1,UINT64_MAX-1}}; CHECK(encode(p,b));
    p = sample(); p.message->payload.resize(MaxPayloadBytes); CHECK(encode(p,b));
    p.message->payload.push_back(0); auto previous = b; CHECK(!encode(p,b)); CHECK(previous == b);
    p = sample(); p.message->lane = Lane::Bulk; CHECK(!encode(p,b));
    ReplayWindow replay;
    CHECK(!replay.accept(0)); CHECK(!replay.accept(UINT64_MAX));
    CHECK(replay.accept(UINT64_MAX-1)); CHECK(!replay.accept(UINT64_MAX-1));
    CHECK(replay.accept(UINT64_MAX-1024)); CHECK(!replay.accept(UINT64_MAX-1025));
    auto ranges = replay.ranges(); CHECK(ranges.size() == 2); CHECK(ranges[0].last == UINT64_MAX-1);
    ReplayWindow window;
    for (auto n : {1,2,5,7,6}) CHECK(window.accept(n));
    CHECK(window.ranges() == std::vector<AckRange>({{5,7},{1,2}}));
    CHECK(window.accept(5000)); CHECK(!window.accept(7)); CHECK(window.ranges() == std::vector<AckRange>({{5000,5000}}));

    Transport tx(1,1), rx(1,1);
    CHECK(tx.enqueue(Lane::Control,{42},0)); auto first = tx.poll(0); CHECK(first);
    CHECK(rx.receiveTrusted(*first,0) == Receive::Accepted);
    CHECK(rx.receiveTrusted(*first,0) == Receive::Duplicate);
    auto delivered = rx.drain(); CHECK(delivered.size() == 1 && delivered[0].payload == Bytes{42});
    CHECK(rx.drain().empty()); auto ack = rx.poll(10); CHECK(ack);
    CHECK(tx.receiveTrusted(*ack,10) == Receive::Accepted); CHECK(tx.pending() == 0);
    CHECK(tx.stats().inFlight == 0); CHECK(!tx.poll(20)); // ACKs never elicit ACKs.
    Transport restarted(2,1), nextMap(1,2);
    CHECK(restarted.receiveTrusted(*first,0) == Receive::Invalid);
    CHECK(nextMap.receiveTrusted(*first,0) == Receive::Invalid);
    Packet forged{1,1,10,{{1,999}}, {}};
    CHECK(tx.receiveTrusted(wire(forged),20) == Receive::Invalid);
    forged.acks.clear(); forged.message = Message{Kind::Event,Lane::Control,1,30,0,{1}};
    CHECK(tx.receiveTrusted(wire(forged),20) == Receive::Invalid); // Future timestamp cannot mutate replay.
    forged.message->createdMs = 20;
    CHECK(tx.receiveTrusted(wire(forged),20) == Receive::Accepted);

    Transport bounds(1,1);
    for (size_t i = 0; i < ReliableWindow; ++i) CHECK(bounds.enqueue(Lane::Bulk,Bytes(MaxPayloadBytes,7),0));
    CHECK(!bounds.enqueue(Lane::Bulk,{1},0));
    for (int i = 0; i < 1000; ++i) CHECK(bounds.enqueue(Lane::Gameplay,{uint8_t(i)},0));
    CHECK(bounds.pending() == 65 && bounds.stats().replaced == 999);
    CHECK(!bounds.enqueue(Lane(255),{},0)); CHECK(!bounds.enqueue(Lane::Gameplay,{},0,0));
    CHECK(!bounds.enqueue(Lane::Gameplay,Bytes(769),0));
    bounds.poll(30000); CHECK(bounds.failed()); // Reliable timeout is explicit session failure.
    CHECK(!bounds.enqueue(Lane::Control,{},30001));

    Transport reorder(1,1);
    Packet event{1,1,1,{},Message{Kind::Event,Lane::Control,2,0,0,{2}}};
    CHECK(reorder.receiveTrusted(wire(event),0) == Receive::Accepted); CHECK(reorder.drain().empty());
    event.number = 2; event.message->id = 65;
    CHECK(reorder.receiveTrusted(wire(event),0) == Receive::Backpressure);
    event.message = Message{Kind::State,Lane::Gameplay,1,0,100,{9}};
    CHECK(reorder.receiveTrusted(wire(event),0) == Receive::Accepted);
    CHECK(reorder.drain().size() == 1); // Missing reliable #1 cannot block gameplay.
    event.number = 3; event.message = Message{Kind::Event,Lane::Control,1,0,0,{1}};
    CHECK(reorder.receiveTrusted(wire(event),0) == Receive::Accepted);
    auto ordered = reorder.drain(); CHECK(ordered.size() == 2 && ordered[0].id == 1 && ordered[1].id == 2);
    event.number = 4;
    CHECK(reorder.receiveTrusted(wire(event),0) == Receive::Accepted); CHECK(reorder.drain().empty());
    // Fresh packet identity does not duplicate a committed reliable message.

    Transport expiry(1,1);
    CHECK(expiry.enqueue(Lane::Gameplay,{1},0,10)); CHECK(!expiry.poll(10)); CHECK(expiry.stats().expired == 1);
    event.number = 1; event.message = Message{Kind::State,Lane::Gameplay,1,0,10,{1}};
    CHECK(expiry.receiveTrusted(wire(event),10) == Receive::Accepted); CHECK(expiry.drain().empty());
    CHECK(!expiry.poll(9)); CHECK(!expiry.enqueue(Lane::Control,{},9));
    CHECK(!expiry.poll(UINT64_MAX));
    Transport withheld(1,1);
    CHECK(withheld.enqueue(Lane::Gameplay,{1},0,10)); CHECK(withheld.update(10));
    CHECK(withheld.pending() == 0 && withheld.stats().sent == 0 && withheld.stats().inFlight == 0);
    Transport delayedConsumer(1,1);
    CHECK(delayedConsumer.receiveTrusted(wire(event),0) == Receive::Accepted);
    delayedConsumer.poll(20); CHECK(delayedConsumer.drain().empty());

    // ACK loss followed by identical packet delivery must request another ACK.
    Transport duplicateAck(1,1);
    CHECK(duplicateAck.receiveTrusted(*first,0) == Receive::Accepted); CHECK(duplicateAck.poll(0));
    CHECK(duplicateAck.receiveTrusted(*first,20) == Receive::Duplicate); CHECK(duplicateAck.poll(20));

    // Retry has a new packet identity but the same reliable delivery identity.
    Transport retry(3,1), target(3,1);
    CHECK(retry.enqueue(Lane::Control,{77},0));
    auto lost = retry.poll(0); CHECK(lost);
    auto retried = retry.poll(500); CHECK(retried);
    Packet original, replacement;
    CHECK(decode(*lost,original) && decode(*retried,replacement));
    CHECK(original.number != replacement.number && original.message == replacement.message);
    CHECK(retry.stats().retransmissions == 1 && retry.stats().losses == 1);
    CHECK(retry.stats().congestionWindow < 12000);
    CHECK(target.receiveTrusted(*retried,500) == Receive::Accepted);
    CHECK(target.drain().size() == 1);
    CHECK(target.receiveTrusted(*lost,501) == Receive::Accepted); CHECK(target.drain().empty());
    auto receipt = target.poll(510); CHECK(receipt);
    CHECK(retry.receiveTrusted(*receipt,510) == Receive::Accepted); CHECK(retry.pending() == 0);

    // A full receive window is bounded even when the application never drains it.
    Transport slow(1,1);
    for (uint64_t id = 1; id <= 64; ++id)
    {
        Packet q{1,1,id,{},Message{Kind::Event,Lane::Control,id,0,0,Bytes(768,1)}};
        CHECK(slow.receiveTrusted(wire(q),0) == Receive::Accepted);
    }
    CHECK(slow.buffered() == 64);
    Packet blocked{1,1,65,{},Message{Kind::Event,Lane::Control,65,0,0,{1}}};
    CHECK(slow.receiveTrusted(wire(blocked),0) == Receive::Backpressure);
    auto partialReceipt = slow.poll(0); CHECK(partialReceipt);
    Packet partial; CHECK(decode(*partialReceipt,partial)); CHECK(partial.acks.front().last == 64);
    CHECK(slow.drain().size() == 64);
    CHECK(slow.receiveTrusted(wire(blocked),1) == Receive::Accepted); CHECK(slow.drain().size() == 1);
}
struct Rng
{
    uint32_t state;
    uint32_t next() { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; }
};
struct Profile { unsigned delay, jitter, loss, duplicate, rate; bool blackout = false; };
struct Flight
{
    uint64_t at, serial; int destination; Bytes bytes;
    bool operator<(const Flight& other) const { return at != other.at ? at > other.at : serial > other.serial; }
};
uint64_t simulate(uint32_t seed, Profile profile, bool verbose)
{
    Rng rng{seed}; Transport endpoints[]{Transport(123,1),Transport(123,1)};
    std::priority_queue<Flight> network;
    std::array<uint64_t,2> linkFree{};
    uint64_t serial = 0, digest = 0, lastReliable[2][3]{};
    unsigned submitted[2][3]{};
    size_t peak = 0, stateCount = 0;
    auto send = [&](int from, Bytes bytes, uint64_t now)
    {
        // Independent directional serialization creates an actual bandwidth bottleneck.
        if ((profile.blackout && now >= 700 && now < 1100) || rng.next()%100 < profile.loss) return;
        auto schedule = [&](const Bytes& data)
        {
            const uint64_t cost = data.size()+100;
            linkFree[from] = std::max(linkFree[from],now)+(cost*1000+profile.rate-1)/profile.rate;
            const auto delay = from ? profile.delay/2 : profile.delay; // Deliberately asymmetric.
            const uint64_t due = linkFree[from]+delay+(profile.jitter ? rng.next()%(profile.jitter+1) : 0);
            network.push({due,serial++,1-from,data});
            CHECK(network.size() <= 4096);
        };
        schedule(bytes);
        if (rng.next()%100 < profile.duplicate) schedule(bytes);
    };
    for (uint64_t now = 0; now < 29000; ++now)
    {
        while (!network.empty() && network.top().at <= now)
        {
            auto packet = network.top(); network.pop();
            auto result = endpoints[packet.destination].receiveTrusted(packet.bytes,now);
            CHECK(result != Receive::Invalid);
        }
        for (int side = 0; side < 2; ++side)
        {
            auto& endpoint = endpoints[side];
            if (now < 4000 && now%16 == 0) CHECK(endpoint.enqueue(Lane::Gameplay,{uint8_t(now/16)},now,150));
            for (size_t l = 1; l < 3; ++l)
                if (submitted[side][l] < 100 && now%10 == 0 &&
                    endpoint.enqueue(Lane(l),Bytes(l == 2 ? 512 : 16,uint8_t(submitted[side][l]+1)),now)) ++submitted[side][l];
            for (auto& message : endpoint.drain())
            {
                const auto l = size_t(message.lane);
                if (l)
                {
                    CHECK(message.id == lastReliable[side][l]+1);
                    CHECK(message.payload.front() == uint8_t(message.id));
                    lastReliable[side][l] = message.id;
                }
                else ++stateCount;
                digest = digest*33+message.id+uint8_t(message.lane);
            }
            if (auto bytes = endpoint.poll(now)) send(side,std::move(*bytes),now);
            CHECK(!endpoint.failed()); CHECK(endpoint.pending() <= 129); CHECK(endpoint.buffered() <= 129);
            CHECK(endpoint.stats().inFlight <= 256*1200);
            peak = std::max(peak,endpoint.pending());
        }
        if (now > 5000 && endpoints[0].pending() == 0 && endpoints[1].pending() == 0 && network.empty()) break;
    }
    for (int side = 0; side < 2; ++side)
        for (size_t l = 1; l < 3; ++l) { CHECK(submitted[side][l] == 100); CHECK(lastReliable[side][l] == 100); }
    CHECK(stateCount > 0); CHECK(endpoints[0].pending() == 0 && endpoints[1].pending() == 0);
    // Regression floors for this precise burst fixture, not gameplay acceptance
    // targets. Reliability alone previously passed while fresh state starved.
    if (profile.loss == 1 && profile.rate == 80000) CHECK(stateCount >= 400);
    if (profile.loss == 3 && profile.rate == 40000) CHECK(stateCount >= 200);
    if (verbose) std::cout << "seed=" << seed << " loss=" << profile.loss << "% jitter=" << profile.jitter
        << "ms rate=" << profile.rate << "B/s delivered=400 reliable state=" << stateCount
        << " retries=" << endpoints[0].stats().retransmissions+endpoints[1].stats().retransmissions
        << " peak_pending=" << peak << " digest=" << digest << '\n';
    return digest;
}
Bytes fromHex(const std::string& text)
{
    if (text.size()%2) throw std::runtime_error("odd hex");
    Bytes b;
    for (size_t i = 0; i < text.size(); i+=2) b.push_back(uint8_t(std::stoul(text.substr(i,2),nullptr,16)));
    return b;
}
void printHex(const Bytes& b)
{
    const char* hex = "0123456789abcdef";
    for (auto v : b) std::cout << hex[v>>4] << hex[v&15];
    std::cout << '\n';
}
}
int main(int argc, char** argv)
{
    try
    {
        const std::string command = argc > 1 ? argv[1] : "contract";
        if (command == "emit") { printHex(wire(sample())); return 0; }
        if (command == "decode")
        {
            std::string line;
            while (std::getline(std::cin,line))
            {
                Packet p; auto bytes = fromHex(line);
                if (decode(bytes,p)) printHex(wire(p)); else std::cout << "invalid\n";
            }
            return 0;
        }
        if (command == "contract") contract();
        else if (command == "impairments")
        {
            for (Profile p : {Profile{0,0,0,0,200000}, Profile{20,10,1,2,80000},
                    Profile{50,30,3,5,40000}, Profile{100,80,10,10,25000,true}})
            {
                auto a = simulate(12345,p,true); auto b = simulate(12345,p,false); CHECK(a == b);
            }
        }
        else if (command == "soak")
        {
            for (uint32_t seed = 1; seed <= 100; ++seed) simulate(seed,{50,50,5,5,40000,true},seed%25 == 0);
        }
        else throw std::runtime_error("unknown command");
        std::cout << "PASS " << command << " checks=" << checks << '\n';
        return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
