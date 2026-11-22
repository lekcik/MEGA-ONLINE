#pragma once

#include "core/Math3D.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace mega {

enum class PlayerRole : uint8_t {
    Human   = 0,
    Monster = 1
};

enum class NetMode : uint8_t {
    Solo   = 0,
    Host   = 1,
    Client = 2
};

enum class MatchPhase : uint8_t {
    Lobby      = 0,
    Playing    = 1,
    HumanWon   = 2,
    MonsterWon = 3
};

struct NetInputFrame {
    uint32_t seq = 0;
    uint32_t lastAckEventId = 0;
    uint8_t role = 0;
    float moveForward = 0.0f;
    float moveRight = 0.0f;
    float yaw = 0.0f;
    float pitch = 0.0f;
    uint16_t flags = 0;
    float dragDx = 0.0f;
    float dragDy = 0.0f;
    Vec3 rayOrig{};
    Vec3 rayDir{0.0f, 0.0f, -1.0f};
    int32_t craftOptionIdx = -1;
    int8_t quickUseSlot = -1;
};

struct NetDoorDelta {
    uint16_t index = 0;
    float angle = 0.0f;
    float angularVel = 0.0f;
    uint8_t locked = 0;
};

struct NetExtraItem {
    uint16_t index = 0;
    Vec3 pos{};
    uint8_t type = 0;
    uint8_t rotX = 0;
    uint8_t rotY = 0;
    uint8_t rotZ = 0;
    uint8_t durability = 0;
    uint8_t pickedUp = 0;
};

struct NetSoundEvent {
    uint32_t eventId = 0;
    uint8_t soundId = 0;
    uint8_t spatial = 1;
    Vec3 pos{};
    float volume = 1.0f;
    float param1 = 0.5f;
    float param2 = 0.5f;
};

struct NetPackedItem {
    uint16_t id = 0;
    uint8_t type = 0;
    uint8_t gx = 0;
    uint8_t gy = 0;
    uint8_t gz = 0;
    uint8_t rotX = 0;
    uint8_t rotY = 0;
    uint8_t rotZ = 0;
    uint8_t durability = 0;
    uint8_t inBackSlot = 0;
};

struct NetLockpickPin {
    float pos = 0.0f;
    float target = 0.5f;
    uint8_t pinned = 0;
    float brokenTimer = 0.0f;
};

struct NetLockpickState {
    uint8_t active = 0;
    int16_t targetDoorIdx = -1;
    float pickX = 0.5f;
    float pickY = -0.05f;
    NetLockpickPin pins[3]{};
};

struct NetHumanSnapshot {
    Vec3 pos{};
    Vec3 vel{};
    float yaw = 0.0f;
    float pitch = 0.0f;
    float health = 100.0f;
    float hunger = 100.0f;
    float thirst = 100.0f;
    float energy = 100.0f;
    float battery = 100.0f;
    uint8_t hasFlashlight = 0;
    uint8_t flashlightOn = 0;
    uint8_t hasHeldItem = 0;
    uint8_t heldPlaceMode = 0;
    uint8_t draggingItem = 0;
    uint8_t heldItemType = 0;
    uint8_t heldRotX = 0;
    uint8_t heldRotY = 0;
    uint8_t heldRotZ = 0;
    uint8_t dragItemType = 0;
    uint8_t dragRotX = 0;
    uint8_t dragRotY = 0;
    uint8_t dragRotZ = 0;
    uint8_t backpackMode = 0;
    Vec3 backpackGroundPos{};
    float backpackGroundYaw = 0.0f;
    float backpackViewYaw = 0.0f;
    float backpackViewPitch = 0.0f;
    float meleeSwingTimer = 0.0f;
    float damageFlash = 0.0f;
    int16_t hiddenLockerIdx = -1;
    NetLockpickState lockpick{};
    std::vector<NetPackedItem> backpackItems;
};

struct NetMonsterSnapshot {
    Vec3 pos{};
    Vec3 vel{};
    float yaw = 0.0f;
    float pitch = 0.0f;
    float hp = 140.0f;
    float maxHp = 140.0f;
    float stamina = 100.0f;
    float dashCooldown = 0.0f;
    float dashTimer = 0.0f;
    float attackCooldown = 0.0f;
    float attackSwingTimer = 0.0f;
    float hurtFlash = 0.0f;
    uint8_t nightVision = 0;
    uint8_t sprinting = 0;
    Vec3 footPos[2]{};
    float pelvisOffsetY = 0.0f;
    float armAngle[2]{};
    float torsoLeanX = 0.0f;
};

struct NetWorldSnapshot {
    uint32_t serverTick = 0;
    uint32_t ackInputSeq = 0;
    uint32_t roundId = 1;
    uint64_t baseSeed = 0;
    uint8_t matchPhase = 0;
    int16_t streamRegX = 0;
    int16_t streamRegZ = 0;

    NetHumanSnapshot human;
    NetMonsterSnapshot monster;

    std::vector<NetDoorDelta> doors;
    uint16_t baseItemCount = 0;
    std::vector<uint8_t> baseItemPickedUpBits;
    std::vector<NetExtraItem> extraItems;
    std::vector<NetSoundEvent> soundEvents;
};

struct NetEndpoint {
    uint32_t ipv4 = 0; // Network byte order
    uint16_t port = 0; // Network byte order

    bool valid() const { return ipv4 != 0 && port != 0; }
    bool operator==(const NetEndpoint& o) const { return ipv4 == o.ipv4 && port == o.port; }
    std::string toString() const;
};

class NetSession {
public:
    static constexpr uint16_t DEFAULT_PORT = 7777;
    static constexpr uint32_t PROTOCOL_VERSION = 2;

    NetSession() = default;
    ~NetSession();

    NetSession(const NetSession&) = delete;
    NetSession& operator=(const NetSession&) = delete;

    // Bind socket early in lobby for UPnP + STUN room code discovery
    bool prepareLobbySocket(uint16_t port, bool tryUpnp = true);
    bool startHost(uint16_t port, PlayerRole hostRole, uint64_t seed);
    bool startClient(const std::string& hostCodeOrIp, uint16_t fallbackPort);
    bool setPunchTarget(const std::string& peerCodeOrIp, uint16_t fallbackPort = DEFAULT_PORT);
    void refreshStun(bool asyncNonBlocking = true);
    void stop();

    // Non-blocking poll of incoming UDP packets (game + STUN + NAT punch)
    void poll(double nowSec);

    bool sendClientInput(const NetInputFrame& frame);
    bool sendWorldSnapshot(const NetWorldSnapshot& snap);
    bool sendHandshakeAck(uint64_t seed, PlayerRole hostRole, uint32_t roundId);

    bool popLatestClientInput(NetInputFrame& out);
    bool popAllClientInputs(std::vector<NetInputFrame>& out);
    bool popLatestSnapshot(NetWorldSnapshot& out);

    NetMode mode() const { return mode_; }
    bool isConnected() const { return connected_; }
    bool isBound() const { return sockFd_ != -1; }
    uint16_t boundPort() const { return boundPort_; }
    PlayerRole hostRole() const { return hostRole_; }
    PlayerRole localRole() const { return localRole_; }
    uint64_t negotiatedSeed() const { return seed_; }
    uint32_t negotiatedRoundId() const { return roundId_; }
    const std::string& statusText() const { return statusText_; }
    const std::string& roomCode() const { return roomCode_; }
    const std::string& upnpStatus() const { return upnpStatus_; }
    bool upnpMapped() const { return upnpMapped_; }
    const NetEndpoint& peer() const { return peer_; }
    const NetEndpoint& publicEndpoint() const { return publicEndpoint_; }

    static std::string detectLocalLanIp();
    static std::string encodeRoomCode(uint32_t ipv4NetOrder, uint16_t portNetOrder);
    static bool decodeRoomCode(const std::string& code, NetEndpoint& out);
    static bool parseTarget(const std::string& codeOrIp, uint16_t fallbackPort, NetEndpoint& out);

    // Pure serialization helpers (public for unit testing without sockets)
    static std::vector<uint8_t> serializeClientInput(const NetInputFrame& f);
    static bool deserializeClientInput(const uint8_t* data, size_t len, NetInputFrame& out);
    static std::vector<uint8_t> serializeWorldSnapshot(const NetWorldSnapshot& s);
    static bool deserializeWorldSnapshot(const uint8_t* data, size_t len, NetWorldSnapshot& out);

private:
    enum PacketType : uint8_t {
        PKT_HANDSHAKE_REQ  = 1,
        PKT_HANDSHAKE_ACK  = 2,
        PKT_CLIENT_INPUT   = 3,
        PKT_WORLD_SNAPSHOT = 4,
        PKT_PING           = 5,
        PKT_DISCONNECT     = 6,
        PKT_PUNCH          = 7
    };

    bool initSockets();
    void shutdownSockets();
    bool bindUdpSocket(uint16_t port);
    bool attemptUpnpMap(uint16_t udpPort);
    void sendStunBindingRequests();
    bool handleStunPacket(const uint8_t* buf, size_t len);
    bool sendRaw(const NetEndpoint& to, const uint8_t* data, size_t len);

    intptr_t sockFd_ = -1;
    uint16_t boundPort_ = 0;
    NetMode mode_ = NetMode::Solo;
    bool connected_ = false;
    bool wsaInitialized_ = false;
    PlayerRole hostRole_ = PlayerRole::Human;
    PlayerRole localRole_ = PlayerRole::Human;
    uint64_t seed_ = 0x4D4547410001ULL;
    uint32_t roundId_ = 1;
    NetEndpoint peer_{};
    NetEndpoint punchTarget_{};
    NetEndpoint publicEndpoint_{};
    std::vector<NetEndpoint> stunServers_;
    std::array<uint8_t, 12> stunTxId_{};
    bool stunRequested_ = false;
    bool upnpMapped_ = false;
    std::string roomCode_ = "PROBING...";
    std::string upnpStatus_ = "CHECKING UPNP...";
    double lastRecvTime_ = 0.0;
    double lastSendTime_ = 0.0;
    double lastStunTime_ = -30.0;
    double lastPunchTime_ = -10.0;
    std::string statusText_ = "IDLE";

    std::vector<NetInputFrame> pendingInputs_;
    bool hasSnapshot_ = false;
    NetWorldSnapshot latestSnapshot_{};
    uint32_t lastSnapshotTick_ = 0;
};

} // namespace mega
