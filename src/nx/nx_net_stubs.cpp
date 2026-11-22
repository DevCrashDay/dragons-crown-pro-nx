#include <array>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "crypto.h"
#include "net/addr.h"
#include "net/connection_test.h"
#include "net/crypto.h"
#include "net/identity.h"
#include "net/json.h"
#include "net/lobby_link.h"
#include "net/online_config.h"
#include "net/online_version.h"
#include "net/peer_socket.h"
#include "net/relay_ends.h"
#include "net/session_watch.h"
#include "net/stun.h"
#include "net/upnp.h"

namespace trg {
FILE* OpenFile(const std::string& path, const char* mode) { return std::fopen(path.c_str(), mode); }
bool RemoveFile(const std::string& path) { return std::remove(path.c_str()) == 0; }
bool MoveFileOver(const std::string& from, const std::string& to) {
    std::remove(to.c_str());
    return std::rename(from.c_str(), to.c_str()) == 0;
}
} // namespace trg

#define u128 nx_u128
extern "C" {
#include <switch/crypto/sha256.h>
}
#undef u128

namespace dcp::crypto {
struct Sha256Stream::Impl {
    Sha256Context ctx{};
};
Sha256Stream::Sha256Stream() : impl_(std::make_unique<Impl>()) {
    sha256ContextCreate(&impl_->ctx);
}
Sha256Stream::~Sha256Stream() = default;
bool Sha256Stream::Update(const void* data, size_t size) {
    if (!impl_) return false;
    sha256ContextUpdate(&impl_->ctx, data, size);
    return true;
}
bool Sha256Stream::Finish(uint8_t (&digest)[32]) {
    if (!impl_) return false;
    sha256ContextGetHash(&impl_->ctx, digest);
    return true;
}

struct AesKey::Impl {};
AesKey::AesKey() = default;
AesKey::~AesKey() = default;
bool AesKey::Init(AesMode, const uint8_t*) { return false; }
bool AesKey::Encrypt(uint8_t*, size_t, uint8_t*) { return false; }
bool AesKey::Decrypt(uint8_t*, size_t, uint8_t*) { return false; }
} // namespace dcp::crypto

namespace dcp::net {

uint64_t now_ms() { return 0; }
bool online_compatible(int) { return false; }
const std::string& port_version() {
    static const std::string v = "1.4.0";
    return v;
}
const std::string& host_platform() {
    static const std::string p = "switch";
    return p;
}
std::string online_name(const std::string& a, const std::string&) { return a; }
AddressKind classify_text(const std::string&) { return AddressKind{}; }
std::string hex_encode(const void*, size_t) { return {}; }
bool hex_decode(std::string_view, std::vector<uint8_t>&) { return false; }
std::string ip_text(uint32_t) { return "0.0.0.0"; }
bool parse_ip(const std::string&, uint32_t*) { return false; }
uint32_t parse_ip(const std::string&) { return 0; }
bool parse_bool(const std::string&, bool def) { return def; }
std::string u64_text(uint64_t) { return "0"; }
bool is_lan(AddressKind) { return false; }
bool is_lan(uint32_t) { return false; }
uint32_t default_route_ip() { return 0; }
std::vector<uint32_t> local_ipv4_addresses() { return {}; }
std::string endpoint_text(const Endpoint&) { return {}; }
std::string endpoint_choice_text(const EndpointChoice&, bool) { return {}; }
EndpointChoice choose_endpoint(uint32_t, bool, uint16_t, StunState, const Endpoint&, const Endpoint&) { return {}; }
std::string lobby_endpoint_text(const std::string&, const std::string&) { return {}; }
void apply_environment(OnlineConfig&) {}
Identity load_identity(const std::filesystem::path&, bool) { return {}; }
Json hello_body(const Identity&, const std::string&, const std::string&, bool, bool, bool) { return {}; }
bool appearance_capability(const Json&) { return false; }
bool boss_battle_session_capability(const Json&) { return false; }
RelayCapability relay_capability(const Json&) { return {}; }
bool story_session_capability(const Json&) { return false; }
uint32_t relay_token(const std::array<uint8_t, 16>&) { return 0; }
std::vector<std::pair<std::string, uint16_t>> stun_servers(const std::string&) { return {}; }
std::string system_type_text(const std::string&, const std::string&) { return {}; }

ConnectionReport run_connection_test(const OnlineConfig&, const Identity&, const std::string&, const std::string&, uint16_t) {
    return {};
}

int64_t Json::as_int(int64_t def) const { return def; }
uint64_t Json::as_u64() const { return 0; }
const std::string& Json::as_string() const {
    static const std::string empty;
    return empty;
}
std::string Json::dump() const { return "{}"; }
const std::vector<Json>& Json::items() const {
    static const std::vector<Json> empty;
    return empty;
}
const Json& Json::operator[](std::string_view) const {
    static const Json null_json;
    return null_json;
}
Json& Json::push(Json) { return *this; }
Json& Json::set(std::string_view, Json) { return *this; }

LobbyLink::LobbyLink(Options) {}
LobbyLink::~LobbyLink() = default;
void LobbyLink::start() {}
void LobbyLink::release_held() {}
bool LobbyLink::poll(LobbyMessage&) { return false; }
uint32_t LobbyLink::request(const std::string&, Json, bool) { return 0; }
bool LobbyLink::send_binary(std::vector<uint8_t>&&) { return false; }
void LobbyLink::set_binary_sink(std::function<void(const uint8_t*, size_t)>) {}

PeerSocket::~PeerSocket() = default;
std::unique_ptr<PeerSocket> PeerSocket::open(const Options&, std::string*) { return nullptr; }
void PeerSocket::close(int) {}
int PeerSocket::connect(uint64_t) { return -1; }
void PeerSocket::disable_relay() {}
void PeerSocket::enable_relay(uint32_t, RelaySend) {}
void PeerSocket::forget_peer(uint64_t) {}
uint64_t PeerSocket::peer_at(const Endpoint&) const { return 0; }
void PeerSocket::punch(const Endpoint&) {}
size_t PeerSocket::queued_bytes(int) const { return 0; }
bool PeerSocket::readable(int) const { return false; }
bool PeerSocket::receive(int, std::vector<uint8_t>&) { return false; }
Endpoint PeerSocket::remote(int) const { return {}; }
bool PeerSocket::send(int, const void*, size_t) { return false; }
bool PeerSocket::send_comms(uint64_t, const uint8_t*, size_t) { return false; }
void PeerSocket::set_peer(uint64_t, std::vector<Endpoint>, uint16_t) {}
void PeerSocket::set_room(const RoomKey&, uint64_t) {}
LinkState PeerSocket::state(int) const { return LinkState::kClosed; }
LinkStats PeerSocket::stats(int) const { return {}; }
StunState PeerSocket::stun(Endpoint*) const { return StunState{}; }
std::vector<PeerSocket::CommsPacket> PeerSocket::take_comms() { return {}; }

void RelayEnds::Deliver(const uint8_t*, size_t) {}
void RelayEnds::Pause(bool) {}
bool RelayEnds::Send(std::vector<uint8_t>&&) { return false; }
void RelayEnds::Set(PeerSocket::RelaySend, std::shared_ptr<PeerSocket>) {}

std::vector<std::string> SessionWatch::PeerLeft(uint64_t, uint64_t, const std::string&) { return {}; }
std::vector<std::string> SessionWatch::Update(uint64_t, const WatchSession&, const std::vector<WatchLink>&) { return {}; }

namespace upnp {
PortMapping::~PortMapping() = default;
void PortMapping::start(uint16_t) {}
Status PortMapping::status() const { return {}; }
} // namespace upnp

} // namespace dcp::net

namespace DcpUltrawide {
bool SideMaskWanted() { return false; }
} // namespace DcpUltrawide

extern "C" {
int dcp_build_boot_image(const char*, const char*, const char*, char*, size_t) { return 0; }
int dcp_custom_appearance(const char*) { return 0; }
void dcp_remote_appearance_clear(void) {}
const void* dcp_remote_appearance_of(uint64_t) { return nullptr; }
uint64_t dcp_remote_appearance_serial(void) { return 0; }
void dcp_remote_appearance_set(uint64_t, const void*, size_t) {}
void dcp_remote_appearance_start(const char*, const char*) {}
void dcp_activate_text_language(const char*) {}
int dcp_text_composed_glyphs(const char*, const char*, void*, size_t) { return 0; }
int dcp_text_cyrillic_cells(const char*, const char*, void*, size_t) { return 0; }
int dcp_text_cyrillic_metrics(const char*, const char*, void*, size_t) { return 0; }
int dcp_text_default_bone(const char*, void*, size_t) { return 0; }
int dcp_text_font_probe(const char*, void*, size_t) { return 0; }
int dcp_text_native_default_bone(const char*, void*, size_t) { return 0; }
int dcp_text_stage_overlay(const char*, const char*, void*, size_t) { return 0; }
int dcp_text_translation(const char*, const char*, void*, size_t) { return 0; }
}
