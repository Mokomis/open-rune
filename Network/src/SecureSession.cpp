#include "openrune/net/SecureSession.h"
#include <algorithm>
#include <stdexcept>

namespace openrune::net
{
namespace
{
Bytes frame(uint8_t type) { return {'O','R','S','1',type}; }
void append(Bytes& bytes,uint64_t value,unsigned width)
{ for (unsigned i = 0; i < width; ++i) bytes.push_back(uint8_t(value >> ((width-i-1)*8))); }
uint64_t integer(std::span<const uint8_t> bytes,size_t offset,size_t width)
{ uint64_t n = 0; for (size_t i = 0; i < width; ++i) n = (n<<8)|bytes[offset+i]; return n; }
}
SecureSession::SecureSession(bool isServer,const InvitationKey& key,const std::string& identity,Bytes address,uint32_t e)
    : channel(isServer,key,identity,std::move(address)), server(isServer), epoch(e)
{ if (!epoch) throw std::invalid_argument("zero epoch"); }
bool SecureSession::receiveDatagram(std::span<const uint8_t> bytes)
{ return !failed() && channel.receive(bytes); }
bool SecureSession::failed() const
{ return stopped || channel.state() == SecureState::Failed || (transport && transport->failed()); }
bool SecureSession::ready() const
{ return !failed() && joined && clock.ready(now); }
void SecureSession::hello(uint8_t type)
{
    auto bytes = frame(type); append(bytes,session,8); append(bytes,epoch,4); channel.send(bytes);
}
void SecureSession::handle(std::span<const uint8_t> bytes)
{
    if (bytes.size() < 5 || bytes[0] != 'O' || bytes[1] != 'R' || bytes[2] != 'S' || bytes[3] != '1')
    { stopped = true; return; }
    switch (bytes[4])
    {
    case 1: // Session identity offered only by authenticated server.
    case 2: // Confirmation from client; duplicates are idempotent.
    {
        if (bytes.size() != 17 || (bytes[4] == 1) == server || integer(bytes,13,4) != epoch)
        { stopped = true; return; }
        const auto identity = integer(bytes,5,8);
        if (!identity || (session && session != identity)) { stopped = true; return; }
        if (!session) { session = identity; transport = std::make_unique<Transport>(session,epoch); }
        joined = true;
        if (!server) hello(2);
        break;
    }
    case 3: // Clock request: only the locally recorded send time is trusted.
    {
        if (!joined || bytes.size() != 13) return;
        if (now < nextClockReply) return;
        nextClockReply = now+200;
        auto response = frame(4);
        append(response,integer(bytes,5,8),8); append(response,now,8); append(response,now,8);
        channel.send(response);
        break;
    }
    case 4:
        if (joined && bytes.size() == 29) clock.reply(integer(bytes,5,8),integer(bytes,13,8),integer(bytes,21,8),now);
        break;
    case 5:
        if (ready()) transport->receiveTrusted(bytes.subspan(5),now,clock);
        break;
    default: stopped = true;
    }
}
void SecureSession::step(uint64_t time,bool transmit)
{
    if (failed()) return;
    if (time < now || time > ClockSync::MaxTime) { stopped = true; return; }
    now = time;
    if (!started) { start = now; started = true; }
    channel.step(now);
    if (failed()) return;
    if (channel.state() != SecureState::Established) return;
    if (server && !session)
    {
        session = channel.randomSessionId();
        transport = std::make_unique<Transport>(session,epoch);
    }
    for (const auto& bytes : channel.takePlaintext())
    { handle(bytes); if (failed()) return; }
    if (server && !joined && now >= nextHello) { hello(1); nextHello = now+250; }
    if (!joined && now-start >= 15000) { stopped = true; return; }
    if (joined)
    {
        if (auto probe = clock.request(now))
        {
            auto request = frame(3); append(request,probe->id,8); channel.send(request);
        }
        if (ready()) { usable = true; lastUsable = now; }
        else if (now-(usable ? lastUsable : start) >= 20000) { stopped = true; return; }
        // Keep deadlines running during resync without consuming packet numbers
        // or claiming that withheld traffic was actually transmitted.
        if (!ready()) transport->update(now);
        else if (transmit) flush();
    }
}
void SecureSession::flush()
{
    if (!ready()) return;
    if (auto bytes=transport->poll(now)) {
        auto data=frame(5);data.insert(data.end(),bytes->begin(),bytes->end());channel.send(data);
    }
}
bool SecureSession::enqueue(Lane lane,Bytes payload,uint16_t lifetime)
{ return ready() && transport->enqueue(lane,std::move(payload),now,lifetime); }
std::vector<Message> SecureSession::drain()
{ return ready() ? transport->drain() : std::vector<Message>{}; }
std::vector<Bytes> SecureSession::takeDatagrams()
{ return failed() ? std::vector<Bytes>{} : channel.takeDatagrams(); }
}
