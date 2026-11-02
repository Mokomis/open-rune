#include "openrune/net/SecureChannel.h"
#include <mbedtls/ssl.h>
#include <mbedtls/ssl_cookie.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/platform_util.h>
#include <psa/crypto.h>
#include <algorithm>
#include <cstring>
#include <deque>
#include <stdexcept>
#include <mutex>

namespace openrune::net
{
namespace
{
// The pinned default PSA build has process-global state. Serialize library
// entry points across independent channels; each channel still has one owner.
std::mutex& cryptoMutex() { static std::mutex mutex; return mutex; }
struct Random
{
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    Random()
    {
        const std::lock_guard lock(cryptoMutex());
        // Mbed TLS 3.6's default TLS configuration also uses the PSA subsystem.
        // Initialize once per process; never tear it down while other peers exist.
        static const auto initialized = psa_crypto_init();
        if (initialized != PSA_SUCCESS) throw std::runtime_error("PSA initialization failed");
        mbedtls_entropy_init(&entropy); mbedtls_ctr_drbg_init(&drbg);
        constexpr unsigned char label[] = "Open Rune DTLS v1";
        if (mbedtls_ctr_drbg_seed(&drbg,mbedtls_entropy_func,&entropy,label,sizeof(label)-1))
        {
            mbedtls_ctr_drbg_free(&drbg); mbedtls_entropy_free(&entropy);
            throw std::runtime_error("secure entropy initialization failed");
        }
    }
    ~Random() { const std::lock_guard lock(cryptoMutex()); mbedtls_ctr_drbg_free(&drbg); mbedtls_entropy_free(&entropy); }
};
constexpr size_t QueueLimit = 64, MaxDatagram = 1200, MaxPlaintext = 1105;
}
InvitationKey generateInvitationKey()
{
    Random random;
    InvitationKey key{};
    {
        const std::lock_guard lock(cryptoMutex());
        if (mbedtls_ctr_drbg_random(&random.drbg,key.data(),key.size())) throw std::runtime_error("secure random generation failed");
    }
    return key;
}
struct SecureChannel::Impl
{
    Random random;
    mbedtls_ssl_config config;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_cookie_ctx cookie;
    bool server;
    Bytes transportId;
    SecureState status = SecureState::Handshaking;
    int lastError = 0;
    uint64_t now = 0, started = 0, timerAt = 0;
    uint32_t intermediate = 0, finalDelay = 0;
    bool startedClock = false;
    std::deque<Bytes> input;
    std::vector<Bytes> output, plaintext;
    static constexpr int suites[]{MBEDTLS_TLS_ECDHE_PSK_WITH_CHACHA20_POLY1305_SHA256,0};
    inline static const char* protocols[]{ProtocolAlpn,nullptr};
    Impl(bool isServer, Bytes id) : server(isServer), transportId(std::move(id))
    {
        mbedtls_ssl_config_init(&config); mbedtls_ssl_init(&ssl); mbedtls_ssl_cookie_init(&cookie);
    }
    ~Impl()
    {
        const std::lock_guard lock(cryptoMutex());
        mbedtls_ssl_free(&ssl); mbedtls_ssl_config_free(&config); mbedtls_ssl_cookie_free(&cookie);
    }
    void require(int result)
    {
        if (result) throw std::runtime_error("DTLS configuration failed: "+std::to_string(result));
    }
    void configure(const InvitationKey& key, const std::string& identity)
    {
        const std::lock_guard lock(cryptoMutex());
        if (identity.empty() || identity.size() > 64 || transportId.empty() || transportId.size() > 64 ||
            std::all_of(key.begin(),key.end(),[](auto x){return !x;})) throw std::invalid_argument("invalid invitation or peer binding");
        require(mbedtls_ssl_config_defaults(&config,server ? MBEDTLS_SSL_IS_SERVER : MBEDTLS_SSL_IS_CLIENT,
            MBEDTLS_SSL_TRANSPORT_DATAGRAM,MBEDTLS_SSL_PRESET_DEFAULT));
        mbedtls_ssl_conf_rng(&config,mbedtls_ctr_drbg_random,&random.drbg);
        mbedtls_ssl_conf_min_tls_version(&config,MBEDTLS_SSL_VERSION_TLS1_2);
        mbedtls_ssl_conf_max_tls_version(&config,MBEDTLS_SSL_VERSION_TLS1_2);
        mbedtls_ssl_conf_ciphersuites(&config,suites);
        require(mbedtls_ssl_conf_alpn_protocols(&config,protocols));
        require(mbedtls_ssl_conf_psk(&config,key.data(),key.size(),reinterpret_cast<const unsigned char*>(identity.data()),identity.size()));
        mbedtls_ssl_conf_dtls_anti_replay(&config,MBEDTLS_SSL_ANTI_REPLAY_ENABLED);
        mbedtls_ssl_conf_handshake_timeout(&config,250,2000);
        mbedtls_ssl_conf_renegotiation(&config,MBEDTLS_SSL_RENEGOTIATION_DISABLED);
        mbedtls_ssl_conf_session_tickets(&config,MBEDTLS_SSL_SESSION_TICKETS_DISABLED);
        if (server)
        {
            require(mbedtls_ssl_cookie_setup(&cookie,mbedtls_ctr_drbg_random,&random.drbg));
            mbedtls_ssl_conf_dtls_cookies(&config,mbedtls_ssl_cookie_write,mbedtls_ssl_cookie_check,&cookie);
        }
        require(mbedtls_ssl_setup(&ssl,&config));
        bind();
    }
    void bind()
    {
        mbedtls_ssl_set_bio(&ssl,this,write,read,nullptr);
        mbedtls_ssl_set_timer_cb(&ssl,this,setTimer,getTimer);
        mbedtls_ssl_set_mtu(&ssl,MaxDatagram);
        if (server) require(mbedtls_ssl_set_client_transport_id(&ssl,transportId.data(),transportId.size()));
    }
    static int write(void* context,const unsigned char* bytes,size_t length)
    {
        auto& p = *static_cast<Impl*>(context);
        if (length > MaxDatagram) return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
        if (p.output.size() >= QueueLimit) return MBEDTLS_ERR_SSL_WANT_WRITE;
        p.output.emplace_back(bytes,bytes+length);
        return int(length);
    }
    static int read(void* context,unsigned char* bytes,size_t capacity)
    {
        auto& p = *static_cast<Impl*>(context);
        if (p.input.empty()) return MBEDTLS_ERR_SSL_WANT_READ;
        auto datagram = std::move(p.input.front()); p.input.pop_front();
        if (datagram.size() > capacity) return MBEDTLS_ERR_SSL_BUFFER_TOO_SMALL;
        std::memcpy(bytes,datagram.data(),datagram.size());
        return int(datagram.size());
    }
    static void setTimer(void* context,uint32_t middle,uint32_t end)
    {
        auto& p = *static_cast<Impl*>(context);
        p.timerAt = p.now; p.intermediate = middle; p.finalDelay = end;
    }
    static int getTimer(void* context)
    {
        const auto& p = *static_cast<Impl*>(context);
        if (!p.finalDelay) return -1;
        const auto elapsed = p.now-p.timerAt;
        return elapsed >= p.finalDelay ? 2 : elapsed >= p.intermediate ? 1 : 0;
    }
    void fail(int error)
    {
        lastError = error; status = SecureState::Failed;
        input.clear(); output.clear(); plaintext.clear();
    }
};
SecureChannel::SecureChannel(bool server,const InvitationKey& key,const std::string& identity,Bytes id)
    : impl(std::make_unique<Impl>(server,std::move(id))) { impl->configure(key,identity); }
SecureChannel::~SecureChannel() = default;
bool SecureChannel::receive(std::span<const uint8_t> bytes)
{
    if (impl->status == SecureState::Failed || bytes.empty() || bytes.size() > MaxDatagram || impl->input.size() >= QueueLimit) return false;
    // Late application records from a previous connection must not abort a new
    // handshake. Application data has no valid consumer until key establishment.
    if (impl->status == SecureState::Handshaking && bytes[0] == 23) return false;
    impl->input.emplace_back(bytes.begin(),bytes.end()); return true;
}
void SecureChannel::step(uint64_t now)
{
    const std::lock_guard lock(cryptoMutex());
    auto& p = *impl;
    if (p.status == SecureState::Failed) return;
    if (now < p.now || now > uint64_t(INT64_MAX)/4) { p.fail(MBEDTLS_ERR_SSL_BAD_INPUT_DATA); return; }
    p.now = now;
    if (!p.startedClock) { p.started = now; p.startedClock = true; }
    if (p.status == SecureState::Handshaking)
    {
        if (now-p.started >= 10000) { p.fail(MBEDTLS_ERR_SSL_TIMEOUT); return; }
        const auto result = mbedtls_ssl_handshake(&p.ssl);
        if (result == MBEDTLS_ERR_SSL_HELLO_VERIFY_REQUIRED)
        {
            const auto reset = mbedtls_ssl_session_reset(&p.ssl);
            if (reset) { p.fail(reset); return; }
            p.bind(); return;
        }
        if (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE) return;
        if (result) { p.fail(result); return; }
        const auto* alpn = mbedtls_ssl_get_alpn_protocol(&p.ssl);
        if (!alpn || std::strcmp(alpn,ProtocolAlpn)) { p.fail(MBEDTLS_ERR_SSL_BAD_INPUT_DATA); return; }
        p.status = SecureState::Established;
    }
    for (unsigned work = 0; work < 32 && p.plaintext.size() < QueueLimit; ++work)
    {
        std::array<unsigned char,MaxPlaintext+1> bytes{};
        const auto n = mbedtls_ssl_read(&p.ssl,bytes.data(),bytes.size());
        if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) break;
        if (n <= 0 || size_t(n) > MaxPlaintext) { p.fail(n ? n : MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY); break; }
        p.plaintext.emplace_back(bytes.begin(),bytes.begin()+n);
    }
}
bool SecureChannel::send(std::span<const uint8_t> bytes)
{
    const std::lock_guard lock(cryptoMutex());
    auto& p = *impl;
    if (p.status != SecureState::Established || bytes.empty() || bytes.size() > MaxPlaintext || p.output.size() >= QueueLimit) return false;
    const auto n = mbedtls_ssl_write(&p.ssl,bytes.data(),bytes.size());
    // With a free slot, one <=1105 byte DTLS record fits our 1200-byte output.
    // Any unexpected partial/nonblocking result is fatal; never retry different plaintext.
    if (n != int(bytes.size())) { p.fail(n); return false; }
    return true;
}
std::vector<Bytes> SecureChannel::takeDatagrams() { std::vector<Bytes> result; result.swap(impl->output); return result; }
std::vector<Bytes> SecureChannel::takePlaintext() { std::vector<Bytes> result; result.swap(impl->plaintext); return result; }
SecureState SecureChannel::state() const { return impl->status; }
int SecureChannel::error() const { return impl->lastError; }
uint64_t SecureChannel::randomSessionId()
{
    const std::lock_guard lock(cryptoMutex());
    if (!impl->server || impl->status != SecureState::Established) throw std::logic_error("session creation requires authenticated server");
    uint64_t id = 0;
    do { if (mbedtls_ctr_drbg_random(&impl->random.drbg,reinterpret_cast<unsigned char*>(&id),sizeof(id))) throw std::runtime_error("session entropy failed"); } while (!id);
    return id;
}
}
