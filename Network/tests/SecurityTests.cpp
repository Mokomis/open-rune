#include "openrune/net/SecureSession.h"
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <fstream>

using namespace openrune::net;
namespace
{
unsigned checks = 0;
unsigned cookieChallenges = 0;
void require(bool condition,int line)
{ ++checks; if (!condition) throw std::runtime_error("check failed at line "+std::to_string(line)); }
#define CHECK(...) require(bool(__VA_ARGS__),__LINE__)
void clocks()
{
    ClockSync c;
    CHECK(!c.ready(0)); CHECK(!c.toLocal(100,0));
    for (uint64_t t : {1000,1300,1600})
    {
        const auto p = c.request(t); CHECK(p);
        CHECK(c.reply(p->id,t+5020,t+5022,t+42));
        CHECK(!c.reply(p->id,t+5020,t+5022,t+42)); // Reply cannot be replayed.
    }
    CHECK(c.ready(1642)); CHECK(c.offsetMs() == 5000); CHECK(c.uncertaintyMs() == 21);
    CHECK(c.toLocal(6600,1642) == 1600); CHECK(!c.toLocal(7000,1642));
    CHECK(c.toLocal(6650,1642) == 1642); // Future uncertainty grants no future time.
    CHECK(!c.request(1600)); CHECK(!c.toLocal(UINT64_MAX,1642));
    auto p = c.request(1900); CHECK(p);
    CHECK(!c.reply(p->id,100000,100001,1942)); CHECK(c.offsetMs() == 5000);
    CHECK(!c.ready(7000)); CHECK(!c.toLocal(9000,7000));
    for (uint64_t t : {7000,7300,7600})
    { p = c.request(t); CHECK(p); CHECK(c.reply(p->id,t+6020,t+6020,t+40)); }
    CHECK(c.ready(7640)); CHECK(c.offsetMs() == 6000);
    ClockSync bad;
    p = bad.request(0); CHECK(p); CHECK(!bad.reply(p->id,30,10,50));
    p = bad.request(300); CHECK(p); CHECK(!bad.reply(p->id,400,800,500));
    p = bad.request(600); CHECK(p); CHECK(!bad.reply(p->id,600,600,1200));
    p = bad.request(1500); CHECK(p); CHECK(!bad.reply(p->id,UINT64_MAX,UINT64_MAX,1550));
    CHECK(!bad.request(ClockSync::MaxTime+1));
    ClockSync unsolicited;
    auto known = unsolicited.request(2000); CHECK(known);
    CHECK(!unsolicited.reply(known->id+1,3010,3010,2020));
    CHECK(unsolicited.reply(known->id,3010,3010,2020));
    ClockSync asym;
    for (uint64_t t : {1000,1300,1600})
    { p = asym.request(t); CHECK(p); CHECK(asym.reply(p->id,t+1100,t+1100,t+120)); }
    CHECK(asym.offsetMs() == 1040); // Actual offset 1000; 100/20 asymmetry is unknowable.
    CHECK(asym.uncertaintyMs() == 61);
    ClockSync negative;
    for (uint64_t t : {10000,10300,10600})
    { p = negative.request(t); CHECK(p); CHECK(negative.reply(p->id,t-4980,t-4980,t+40)); }
    CHECK(negative.offsetMs() == -5000); CHECK(negative.toLocal(5600,10640) == 10600);
    // Clock-mapped transport data can use independent peer epochs without changing local timers.
    Transport transport(9,1);
    Packet packet{9,1,1,{},Message{Kind::Event,Lane::Control,1,5600,0,{7}}};
    Bytes bytes; CHECK(encode(packet,bytes));
    CHECK(transport.receiveTrusted(bytes,10640,negative) == Receive::Accepted);
    CHECK(transport.drain().at(0).createdMs == 10600);
    ClockSync drift;
    for (uint64_t t = 1000; t < 60000; t+=300)
    {
        auto probe = drift.request(t); CHECK(probe);
        const auto remoteNow = t+10000+t/1000;
        CHECK(drift.reply(probe->id,remoteNow,remoteNow,t));
        if (drift.ready(t)) CHECK(drift.toLocal(remoteNow,t) == t);
    }
}
void pump(SecureChannel& server,SecureChannel& client,uint64_t t,bool drop = false)
{
    server.step(t+10000); client.step(t+50000);
    if (server.state() == SecureState::Failed || client.state() == SecureState::Failed)
        throw std::runtime_error("DTLS pump errors at "+std::to_string(t)+": "+std::to_string(server.error())+","+std::to_string(client.error()));
    for (auto& bytes : server.takeDatagrams())
    {
        if (bytes.size() > 13 && bytes[0] == 22 && bytes[13] == 3) ++cookieChallenges;
        if (!drop) CHECK(client.receive(bytes));
    }
    for (auto& bytes : client.takeDatagrams()) if (!drop) CHECK(server.receive(bytes));
}
void authentication()
{
    auto key = generateInvitationKey(); CHECK(key != generateInvitationKey());
    SecureChannel server(true,key,"client-1",{127,0,0,1,1,2});
    SecureChannel client(false,key,"client-1",{127,0,0,1,1,3});
    CHECK(!client.send({})); CHECK(!server.receive(Bytes(1201)));
    for (uint64_t t = 0; t < 5000 && (server.state() != SecureState::Established || client.state() != SecureState::Established); t+=10)
        pump(server,client,t,t < 300); // Drop opening handshake flight, recover by DTLS timer.
    if (server.state() != SecureState::Established || client.state() != SecureState::Established)
        std::cerr << "handshake errors " << server.error() << ' ' << client.error() << '\n';
    CHECK(server.state() == SecureState::Established && client.state() == SecureState::Established);
    CHECK(cookieChallenges > 0);
    CHECK(server.randomSessionId() != server.randomSessionId());
    Bytes secret{'R','u','n','e',0,1,2,3}; CHECK(client.send(secret));
    auto encrypted = client.takeDatagrams(); CHECK(encrypted.size() == 1);
    CHECK(encrypted[0] != secret); CHECK(encrypted[0].size() <= 1200);
    auto damaged = encrypted[0]; damaged.back() ^= 1;
    CHECK(server.receive(damaged)); server.step(16000); CHECK(server.takePlaintext().empty());
    CHECK(server.receive(encrypted[0])); server.step(16001);
    CHECK(server.takePlaintext() == std::vector<Bytes>{secret});
    CHECK(server.receive(encrypted[0])); server.step(16002); CHECK(server.takePlaintext().empty());
    Bytes maximum(1105,0x5a); CHECK(client.send(maximum));
    for (auto& bytes : client.takeDatagrams()) { CHECK(bytes.size() <= 1200); CHECK(server.receive(bytes)); }
    server.step(16003); CHECK(server.takePlaintext() == std::vector<Bytes>{maximum});
    CHECK(!client.send(Bytes(1106)));
    for (unsigned i = 0; i < 64; ++i) CHECK(client.send(secret));
    CHECK(!client.send(secret)); CHECK(client.takeDatagrams().size() == 64);

    for (bool badIdentity : {false,true})
    {
        auto wrong = key; if (!badIdentity) wrong[0] ^= 1;
        SecureChannel s(true,key,"client-1",{1,2});
        SecureChannel c(false,wrong,badIdentity ? "client-2" : "client-1",{3,4});
        for (uint64_t t = 0; t <= 11000; t+=50)
        {
            s.step(t); c.step(t);
            for (auto& bytes : s.takeDatagrams()) c.receive(bytes);
            for (auto& bytes : c.takeDatagrams()) s.receive(bytes);
        }
        CHECK(s.state() != SecureState::Established && c.state() != SecureState::Established);
        CHECK(s.takePlaintext().empty() && c.takePlaintext().empty());
    }
    SecureChannel restart(true,key,"client-1",{127,0,0,1,1,2});
    CHECK(!restart.receive(encrypted[0])); restart.step(10000); CHECK(restart.takePlaintext().empty());
    SecureChannel freshClient(false,key,"client-1",{127,0,0,1,1,3});
    for (uint64_t t = 0; t < 5000 && (restart.state() != SecureState::Established || freshClient.state() != SecureState::Established); t+=10)
        pump(restart,freshClient,t);
    CHECK(restart.state() == SecureState::Established);
    CHECK(restart.receive(encrypted[0])); restart.step(16000); CHECK(restart.takePlaintext().empty());
    CHECK(freshClient.send(secret)); for (auto& bytes : freshClient.takeDatagrams()) CHECK(restart.receive(bytes));
    restart.step(16001); CHECK(restart.takePlaintext() == std::vector<Bytes>{secret});
    SecureChannel silent(false,key,"peer",{1,2}); silent.step(500); silent.step(10500);
    CHECK(silent.state() == SecureState::Failed);
    SecureChannel backwards(false,key,"peer",{1,2}); backwards.step(50); backwards.step(49);
    CHECK(backwards.state() == SecureState::Failed);
    SecureChannel flooded(true,key,"peer",{1,2});
    for (unsigned i = 0; i < 64; ++i) CHECK(flooded.receive(Bytes(1200,0)));
    CHECK(!flooded.receive(Bytes(1200,0)));
}
void sessions()
{
    auto key = generateInvitationKey();
    SecureSession server(true,key,"peer",{1,2}), client(false,key,"peer",{3,4});
    CHECK(!client.enqueue(Lane::Control,{1}));
    struct Flight { uint64_t at; bool toServer; Bytes bytes; };
    std::vector<Flight> network;
    uint32_t rng = 43; unsigned emitted = 0, received = 0; bool staleObserved = false;
    auto transfer = [&](bool toServer, std::vector<Bytes> packets,uint64_t t)
    {
        for (auto& bytes : packets)
        {
            if (t >= 15000 && t < 21000) continue;
            rng = rng*1664525+1013904223;
            if (rng%100 < 3) continue;
            network.push_back({t+20+(rng%20),toServer,std::move(bytes)});
        }
        CHECK(network.size() < 128);
    };
    for (uint64_t t = 0; t < 25000; t+=5)
    {
        for (auto it = network.begin(); it != network.end();)
        {
            if (it->at > t) { ++it; continue; }
            if (it->toServer) server.receiveDatagram(it->bytes); else client.receiveDatagram(it->bytes);
            it = network.erase(it);
        }
        server.step(t+100000); client.step(t+500000+t/1000); // Different epoch, 1000 ppm drift.
        if (server.failed() || client.failed())
            std::cerr << "session failure t=" << t << " ssl=" << server.securityError() << ',' << client.securityError() << '\n';
        CHECK(!server.failed() && !client.failed());
        if (t >= 20500 && t < 21000)
        {
            CHECK(!client.ready()); CHECK(!client.enqueue(Lane::Control,{255})); staleObserved = true;
        }
        if (client.ready() && emitted < 100 && client.enqueue(Lane::Control,{uint8_t(emitted+1)})) ++emitted;
        for (auto& message : server.drain())
        {
            ++received; CHECK(message.payload == Bytes{uint8_t(received)});
            CHECK(message.createdMs <= t+100000 && t+100000-message.createdMs < 30000);
        }
        transfer(false,server.takeDatagrams(),t); transfer(true,client.takeDatagrams(),t);
    }
    CHECK(server.ready() && client.ready()); CHECK(server.sessionId() && server.sessionId() == client.sessionId());
    CHECK(staleObserved);
    CHECK(emitted == 100 && received == 100);
    CHECK(std::abs(client.clockSync().offsetMs()+400025) <= 25);
    std::cout << "secure impaired session: 100 records, offset=" << client.clockSync().offsetMs()
        << "ms uncertainty=" << client.clockSync().uncertaintyMs() << "ms\n";
    server.step(150000); client.step(550000);
    CHECK(!server.ready() && !client.ready());
    server.step(170000); client.step(570000);
    CHECK(server.failed() && client.failed());
}
}
int main(int argc,char** argv)
{
    try
    {
        if (argc == 3 && std::string(argv[1]) == "--fuzz-seed")
        {
            InvitationKey key{}; key.fill(1);
            SecureChannel client(false,key,"fuzz",{1,2}); client.step(0);
            auto packets = client.takeDatagrams(); CHECK(packets.size() == 1);
            std::ofstream file(argv[2],std::ios::binary);
            file.write(reinterpret_cast<const char*>(packets[0].data()),packets[0].size());
            CHECK(file.good()); return 0;
        }
        clocks(); authentication(); sessions(); std::cout << "PASS authentication/clocks checks=" << checks << '\n'; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
