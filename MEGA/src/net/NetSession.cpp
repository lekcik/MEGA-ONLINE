#include "net/NetSession.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <random>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <arpa/inet.h>
  #include <cerrno>
  #include <fcntl.h>
  #include <netdb.h>
  #include <netinet/in.h>
  #include <sys/select.h>
  #include <sys/socket.h>
  #include <unistd.h>
#endif

namespace mega {

namespace {

constexpr uint8_t MAGIC0 = 'M';
constexpr uint8_t MAGIC1 = 'G';
constexpr uint8_t MAGIC2 = 'N';
constexpr uint8_t MAGIC3 = '2';

class ByteWriter {
public:
    std::vector<uint8_t> buf;

    void writeU8(uint8_t v) { buf.push_back(v); }
    void writeI16(int16_t v) { writeU16(static_cast<uint16_t>(v)); }
    void writeU16(uint16_t v) {
        buf.push_back(static_cast<uint8_t>(v & 0xFF));
        buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    }
    void writeI32(int32_t v) { writeU32(static_cast<uint32_t>(v)); }
    void writeU32(uint32_t v) {
        for (int i = 0; i < 4; ++i) buf.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
    }
    void writeU64(uint64_t v) {
        for (int i = 0; i < 8; ++i) buf.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
    }
    void writeFloat(float f) {
        uint32_t u = 0;
        std::memcpy(&u, &f, 4);
        writeU32(u);
    }
    void writeVec3(const Vec3& v) {
        writeFloat(v.x);
        writeFloat(v.y);
        writeFloat(v.z);
    }
    void writeHeader(uint8_t type) {
        writeU8(MAGIC0);
        writeU8(MAGIC1);
        writeU8(MAGIC2);
        writeU8(MAGIC3);
        writeU8(type);
    }
};

class ByteReader {
public:
    ByteReader(const uint8_t* data, size_t len) : data_(data), len_(len) {}

    bool ok() const { return !err_; }
    size_t remaining() const { return pos_ <= len_ ? (len_ - pos_) : 0; }

    uint8_t readU8() {
        if (pos_ + 1 > len_) { err_ = true; return 0; }
        return data_[pos_++];
    }
    int16_t readI16() { return static_cast<int16_t>(readU16()); }
    uint16_t readU16() {
        if (pos_ + 2 > len_) { err_ = true; return 0; }
        uint16_t v = static_cast<uint16_t>(data_[pos_]) |
                     (static_cast<uint16_t>(data_[pos_ + 1]) << 8);
        pos_ += 2;
        return v;
    }
    int32_t readI32() { return static_cast<int32_t>(readU32()); }
    uint32_t readU32() {
        if (pos_ + 4 > len_) { err_ = true; return 0; }
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= (static_cast<uint32_t>(data_[pos_ + i]) << (i * 8));
        pos_ += 4;
        return v;
    }
    uint64_t readU64() {
        if (pos_ + 8 > len_) { err_ = true; return 0; }
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v |= (static_cast<uint64_t>(data_[pos_ + i]) << (i * 8));
        pos_ += 8;
        return v;
    }
    float readFloat() {
        uint32_t u = readU32();
        float f = 0.0f;
        std::memcpy(&f, &u, 4);
        return f;
    }
    Vec3 readVec3() {
        Vec3 v;
        v.x = readFloat();
        v.y = readFloat();
        v.z = readFloat();
        return v;
    }
    bool readHeader(uint8_t expectedType) {
        if (readU8() != MAGIC0 || readU8() != MAGIC1 ||
            readU8() != MAGIC2 || readU8() != MAGIC3) return false;
        return readU8() == expectedType && !err_;
    }

private:
    const uint8_t* data_ = nullptr;
    size_t len_ = 0;
    size_t pos_ = 0;
    bool err_ = false;
};

constexpr char B32_ALPHABET[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

uint8_t crc8Bytes(const uint8_t* data, size_t len) {
    uint8_t crc = 0x5A;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x07) : static_cast<uint8_t>(crc << 1);
        }
    }
    return crc;
}

int decodeB32Char(char c) {
    char u = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (u == 'O') u = '0';
    if (u == 'I' || u == 'L') u = '1';
    for (int i = 0; i < 32; ++i) {
        if (B32_ALPHABET[i] == u) return i;
    }
    return -1;
}

void setSocketTimeoutMs(intptr_t rawFd, int ms) {
#ifdef _WIN32
    DWORD tv = static_cast<DWORD>(ms);
    setsockopt(static_cast<SOCKET>(rawFd), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
    setsockopt(static_cast<SOCKET>(rawFd), SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
    timeval tv{};
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    setsockopt(static_cast<int>(rawFd), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(static_cast<int>(rawFd), SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

void closeOneSocket(intptr_t rawFd) {
#ifdef _WIN32
    closesocket(static_cast<SOCKET>(rawFd));
#else
    close(static_cast<int>(rawFd));
#endif
}

std::string ciFindTagValue(const std::string& xml, const std::string& tag) {
    std::string lowerXml = xml;
    std::string lowerTag = tag;
    for (char& c : lowerXml) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (char& c : lowerTag) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string open = "<" + lowerTag + ">";
    std::string close = "</" + lowerTag + ">";
    size_t a = lowerXml.find(open);
    if (a == std::string::npos) return {};
    a += open.size();
    size_t b = lowerXml.find(close, a);
    if (b == std::string::npos) return {};
    return xml.substr(a, b - a);
}

bool httpExchange(const std::string& host, uint16_t port, const std::string& req, std::string& outResp) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) return false;

#ifdef _WIN32
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { freeaddrinfo(res); return false; }
    intptr_t sfd = static_cast<intptr_t>(s);
#else
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { freeaddrinfo(res); return false; }
    intptr_t sfd = static_cast<intptr_t>(s);
#endif

    setSocketTimeoutMs(sfd, 450);
    if (connect(s, res->ai_addr, static_cast<int>(res->ai_addrlen)) != 0) {
        freeaddrinfo(res);
        closeOneSocket(sfd);
        return false;
    }
    freeaddrinfo(res);

    size_t totalSent = 0;
    while (totalSent < req.size()) {
#ifdef _WIN32
        int n = send(s, req.data() + totalSent, static_cast<int>(req.size() - totalSent), 0);
#else
        ssize_t n = send(s, req.data() + totalSent, req.size() - totalSent, 0);
#endif
        if (n <= 0) break;
        totalSent += static_cast<size_t>(n);
    }

    outResp.clear();
    char buf[2048];
    for (int iter = 0; iter < 16; ++iter) {
#ifdef _WIN32
        int n = recv(s, buf, sizeof(buf), 0);
#else
        ssize_t n = recv(s, buf, sizeof(buf), 0);
#endif
        if (n <= 0) break;
        outResp.append(buf, static_cast<size_t>(n));
        if (outResp.size() > 32768) break;
    }
    closeOneSocket(sfd);
    return !outResp.empty();
}

} // namespace

std::string NetEndpoint::toString() const {
    if (!valid()) return "0.0.0.0:0";
    char ipBuf[64]{};
    in_addr addr{};
    addr.s_addr = ipv4;
    if (!inet_ntop(AF_INET, &addr, ipBuf, sizeof(ipBuf))) {
        return "?.?.?.?";
    }
    return std::string(ipBuf) + ":" + std::to_string(ntohs(port));
}

NetSession::~NetSession() {
    stop();
}

bool NetSession::initSockets() {
#ifdef _WIN32
    if (!wsaInitialized_) {
        WSADATA wsa{};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            statusText_ = "WSASTARTUP FAILED";
            return false;
        }
        wsaInitialized_ = true;
    }
#endif
    return true;
}

void NetSession::shutdownSockets() {
    if (sockFd_ != -1) {
#ifdef _WIN32
        closesocket(static_cast<SOCKET>(sockFd_));
#else
        close(static_cast<int>(sockFd_));
#endif
        sockFd_ = -1;
    }
#ifdef _WIN32
    if (wsaInitialized_) {
        WSACleanup();
        wsaInitialized_ = false;
    }
#endif
}

void NetSession::stop() {
    if (connected_ && peer_.valid() && sockFd_ != -1) {
        ByteWriter w;
        w.writeHeader(PKT_DISCONNECT);
        sendRaw(peer_, w.buf.data(), w.buf.size());
    }
    shutdownSockets();
    boundPort_ = 0;
    mode_ = NetMode::Solo;
    connected_ = false;
    peer_ = NetEndpoint{};
    punchTarget_ = NetEndpoint{};
    publicEndpoint_ = NetEndpoint{};
    stunServers_.clear();
    stunRequested_ = false;
    upnpMapped_ = false;
    roomCode_ = "PROBING...";
    upnpStatus_ = "CHECKING UPNP...";
    pendingInputs_.clear();
    hasSnapshot_ = false;
    lastSnapshotTick_ = 0;
    statusText_ = "IDLE";
}

bool NetSession::bindUdpSocket(uint16_t port) {
    if (!initSockets()) return false;

#ifdef _WIN32
    SOCKET fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd == INVALID_SOCKET) {
        statusText_ = "SOCKET CREATE FAILED";
        return false;
    }
    u_long nonblock = 1;
    ioctlsocket(fd, FIONBIO, &nonblock);
    sockFd_ = static_cast<intptr_t>(fd);
#else
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        statusText_ = "SOCKET CREATE FAILED";
        return false;
    }
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    sockFd_ = static_cast<intptr_t>(fd);
#endif

    int reuse = 1;
    setsockopt(static_cast<decltype(socket(0,0,0))>(sockFd_), SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(static_cast<decltype(socket(0,0,0))>(sockFd_),
             reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
        statusText_ = "BIND PORT FAILED";
        shutdownSockets();
        return false;
    }

    sockaddr_in bound{};
#ifdef _WIN32
    int blen = sizeof(bound);
#else
    socklen_t blen = sizeof(bound);
#endif
    if (getsockname(static_cast<decltype(socket(0,0,0))>(sockFd_),
                    reinterpret_cast<sockaddr*>(&bound), &blen) == 0) {
        boundPort_ = ntohs(bound.sin_port);
    } else {
        boundPort_ = port;
    }
    return true;
}

bool NetSession::attemptUpnpMap(uint16_t udpPort) {
    if (udpPort == 0) {
        upnpMapped_ = false;
        upnpStatus_ = "STUN P2P READY";
        return false;
    }
    upnpMapped_ = false;
    upnpStatus_ = "NO UPNP - USE PUNCH CODE";
    if (!initSockets()) return false;

#ifdef _WIN32
    SOCKET ssdp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (ssdp == INVALID_SOCKET) return false;
    intptr_t ssdpFd = static_cast<intptr_t>(ssdp);
#else
    int ssdp = socket(AF_INET, SOCK_DGRAM, 0);
    if (ssdp < 0) return false;
    intptr_t ssdpFd = static_cast<intptr_t>(ssdp);
#endif
    setSocketTimeoutMs(ssdpFd, 380);

    sockaddr_in mcast{};
    mcast.sin_family = AF_INET;
    mcast.sin_port = htons(1900);
    inet_pton(AF_INET, "239.255.255.250", &mcast.sin_addr);

    const char* msearch1 =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 1\r\n"
        "ST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n\r\n";
    const char* msearch2 =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 1\r\n"
        "ST: urn:schemas-upnp-org:service:WANIPConnection:1\r\n\r\n";

    sendto(ssdp, msearch1, static_cast<int>(std::strlen(msearch1)), 0,
           reinterpret_cast<const sockaddr*>(&mcast), sizeof(mcast));
    sendto(ssdp, msearch2, static_cast<int>(std::strlen(msearch2)), 0,
           reinterpret_cast<const sockaddr*>(&mcast), sizeof(mcast));

    std::string location;
    char rbuf[2048];
    for (int i = 0; i < 4; ++i) {
        sockaddr_in from{};
#ifdef _WIN32
        int flen = sizeof(from);
        int n = recvfrom(ssdp, rbuf, sizeof(rbuf) - 1, 0, reinterpret_cast<sockaddr*>(&from), &flen);
#else
        socklen_t flen = sizeof(from);
        ssize_t n = recvfrom(ssdp, rbuf, sizeof(rbuf) - 1, 0, reinterpret_cast<sockaddr*>(&from), &flen);
#endif
        if (n <= 0) break;
        rbuf[n] = '\0';
        std::string resp(rbuf);
        std::string lower = resp;
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        size_t pos = lower.find("location:");
        if (pos != std::string::npos) {
            pos += 9;
            while (pos < resp.size() && (resp[pos] == ' ' || resp[pos] == '\t')) ++pos;
            size_t eol = resp.find_first_of("\r\n", pos);
            location = resp.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
            if (!location.empty()) break;
        }
    }
    closeOneSocket(ssdpFd);

    if (location.rfind("http://", 0) != 0) return false;
    std::string rest = location.substr(7);
    size_t slash = rest.find('/');
    std::string hostPort = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    std::string descPath = (slash == std::string::npos) ? "/" : rest.substr(slash);

    std::string routerHost = hostPort;
    uint16_t routerPort = 80;
    size_t colon = hostPort.find(':');
    if (colon != std::string::npos) {
        routerHost = hostPort.substr(0, colon);
        int p = std::atoi(hostPort.substr(colon + 1).c_str());
        if (p > 0 && p <= 65535) routerPort = static_cast<uint16_t>(p);
    }

    std::string xmlResp;
    std::string getReq = "GET " + descPath + " HTTP/1.0\r\nHost: " + hostPort + "\r\nConnection: close\r\n\r\n";
    if (!httpExchange(routerHost, routerPort, getReq, xmlResp)) return false;

    std::string serviceType = "urn:schemas-upnp-org:service:WANIPConnection:1";
    size_t stPos = xmlResp.find("WANIPConnection");
    if (stPos == std::string::npos) {
        stPos = xmlResp.find("WANPPPConnection");
        if (stPos != std::string::npos) serviceType = "urn:schemas-upnp-org:service:WANPPPConnection:1";
    }
    if (stPos == std::string::npos) return false;

    std::string subXml = xmlResp.substr(stPos);
    std::string parsedType = ciFindTagValue(xmlResp.substr(stPos > 120 ? stPos - 120 : 0, 320), "serviceType");
    if (!parsedType.empty()) serviceType = parsedType;
    std::string controlUrl = ciFindTagValue(subXml, "controlURL");
    if (controlUrl.empty()) return false;
    if (controlUrl[0] != '/') controlUrl = "/" + controlUrl;

    std::string lanIp = detectLocalLanIp();
    std::string soapBody =
        "<?xml version=\"1.0\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body>"
        "<u:AddPortMapping xmlns:u=\"" + serviceType + "\">"
        "<NewRemoteHost></NewRemoteHost>"
        "<NewExternalPort>" + std::to_string(udpPort) + "</NewExternalPort>"
        "<NewProtocol>UDP</NewProtocol>"
        "<NewInternalPort>" + std::to_string(udpPort) + "</NewInternalPort>"
        "<NewInternalClient>" + lanIp + "</NewInternalClient>"
        "<NewEnabled>1</NewEnabled>"
        "<NewPortMappingDescription>MEGA_1V1</NewPortMappingDescription>"
        "<NewLeaseDuration>3600</NewLeaseDuration>"
        "</u:AddPortMapping>"
        "</s:Body>"
        "</s:Envelope>";

    std::string soapReq =
        "POST " + controlUrl + " HTTP/1.0\r\n"
        "Host: " + hostPort + "\r\n"
        "Content-Type: text/xml; charset=\"utf-8\"\r\n"
        "SOAPACTION: \"" + serviceType + "#AddPortMapping\"\r\n"
        "Content-Length: " + std::to_string(soapBody.size()) + "\r\n"
        "Connection: close\r\n\r\n" + soapBody;

    std::string soapResp;
    if (!httpExchange(routerHost, routerPort, soapReq, soapResp)) return false;
    if (soapResp.find("200 OK") != std::string::npos || soapResp.find("AddPortMappingResponse") != std::string::npos) {
        upnpMapped_ = true;
        upnpStatus_ = "UPNP PORT OPEN";
        return true;
    }
    return false;
}

void NetSession::sendStunBindingRequests() {
    if (sockFd_ == -1) return;
    if (stunServers_.empty()) {
        struct StunHost { const char* host; const char* port; };
        const StunHost hosts[] = {
            {"stun.l.google.com", "19302"},
            {"stun1.l.google.com", "19302"},
            {"stun.cloudflare.com", "3478"}
        };
        for (const auto& h : hosts) {
            addrinfo hints{};
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_DGRAM;
            addrinfo* res = nullptr;
            if (getaddrinfo(h.host, h.port, &hints, &res) == 0 && res) {
                const auto* sin = reinterpret_cast<const sockaddr_in*>(res->ai_addr);
                NetEndpoint ep{sin->sin_addr.s_addr, sin->sin_port};
                if (ep.valid()) stunServers_.push_back(ep);
                freeaddrinfo(res);
            }
        }
    }
    if (stunServers_.empty()) {
        if (!publicEndpoint_.valid()) roomCode_ = "OFFLINE / LAN ONLY";
        return;
    }

    if (!stunRequested_) {
        std::random_device rd;
        for (size_t i = 0; i < 12; ++i) stunTxId_[i] = static_cast<uint8_t>(rd() & 0xFF);
        stunRequested_ = true;
    }

    uint8_t pkt[20]{};
    pkt[0] = 0x00; pkt[1] = 0x01; // Binding Request
    pkt[2] = 0x00; pkt[3] = 0x00; // Length 0
    pkt[4] = 0x21; pkt[5] = 0x12; pkt[6] = 0xA4; pkt[7] = 0x42; // RFC 5389 Magic Cookie
    std::memcpy(pkt + 8, stunTxId_.data(), 12);

    for (const auto& s : stunServers_) {
        sendRaw(s, pkt, sizeof(pkt));
    }
}

bool NetSession::handleStunPacket(const uint8_t* buf, size_t len) {
    if (len < 20) return false;
    if (buf[0] != 0x01 || buf[1] != 0x01) return false; // Binding Success Response
    if (buf[4] != 0x21 || buf[5] != 0x12 || buf[6] != 0xA4 || buf[7] != 0x42) return false;
    if (std::memcmp(buf + 8, stunTxId_.data(), 12) != 0) return false;

    uint16_t msgLen = static_cast<uint16_t>((buf[2] << 8) | buf[3]);
    size_t end = std::min(len, static_cast<size_t>(20 + msgLen));
    size_t pos = 20;
    NetEndpoint found{};
    while (pos + 4 <= end) {
        uint16_t attrType = static_cast<uint16_t>((buf[pos] << 8) | buf[pos + 1]);
        uint16_t attrLen = static_cast<uint16_t>((buf[pos + 2] << 8) | buf[pos + 3]);
        pos += 4;
        if (pos + attrLen > end) break;

        if (attrType == 0x0020 && attrLen >= 8 && buf[pos + 1] == 0x01) { // XOR-MAPPED-ADDRESS IPv4
            uint16_t xPort = static_cast<uint16_t>((buf[pos + 2] << 8) | buf[pos + 3]);
            uint16_t portHost = static_cast<uint16_t>(xPort ^ 0x2112);
            found.port = htons(portHost);
            uint32_t xIp = (static_cast<uint32_t>(buf[pos + 4]) << 24) |
                           (static_cast<uint32_t>(buf[pos + 5]) << 16) |
                           (static_cast<uint32_t>(buf[pos + 6]) << 8) |
                           static_cast<uint32_t>(buf[pos + 7]);
            found.ipv4 = htonl(xIp ^ 0x2112A442u);
            break;
        } else if (attrType == 0x0001 && attrLen >= 8 && buf[pos + 1] == 0x01 && !found.valid()) { // MAPPED-ADDRESS
            uint16_t portHost = static_cast<uint16_t>((buf[pos + 2] << 8) | buf[pos + 3]);
            found.port = htons(portHost);
            uint32_t ipHost = (static_cast<uint32_t>(buf[pos + 4]) << 24) |
                              (static_cast<uint32_t>(buf[pos + 5]) << 16) |
                              (static_cast<uint32_t>(buf[pos + 6]) << 8) |
                              static_cast<uint32_t>(buf[pos + 7]);
            found.ipv4 = htonl(ipHost);
        }
        pos += (attrLen + 3u) & ~3u;
    }

    if (found.valid()) {
        publicEndpoint_ = found;
        roomCode_ = encodeRoomCode(found.ipv4, found.port);
    }
    return true;
}

bool NetSession::prepareLobbySocket(uint16_t port, bool tryUpnp) {
    if (sockFd_ != -1 && boundPort_ == port && (port != 0 || boundPort_ != 0)) {
        sendStunBindingRequests();
        return true;
    }
    stop();
    if (!bindUdpSocket(port)) return false;
    if (tryUpnp && port > 0) {
        attemptUpnpMap(port);
    } else {
        upnpStatus_ = "STUN P2P READY";
    }
    sendStunBindingRequests();
    return true;
}

void NetSession::refreshStun(bool asyncNonBlocking) {
    (void)asyncNonBlocking;
    sendStunBindingRequests();
}

bool NetSession::setPunchTarget(const std::string& peerCodeOrIp, uint16_t fallbackPort) {
    NetEndpoint ep{};
    if (!parseTarget(peerCodeOrIp, fallbackPort, ep)) return false;
    punchTarget_ = ep;
    lastPunchTime_ = -10.0;
    return true;
}

bool NetSession::startHost(uint16_t port, PlayerRole hostRole, uint64_t seed) {
    bool alreadyBound = (sockFd_ != -1 && boundPort_ == port);
    if (!alreadyBound) {
        stop();
        if (!bindUdpSocket(port)) return false;
        attemptUpnpMap(port);
        sendStunBindingRequests();
    } else {
        connected_ = false;
        peer_ = NetEndpoint{};
        pendingInputs_.clear();
        hasSnapshot_ = false;
        lastSnapshotTick_ = 0;
    }

    mode_ = NetMode::Host;
    connected_ = false;
    hostRole_ = hostRole;
    localRole_ = hostRole;
    seed_ = seed;
    roundId_ = 1;
    lastSendTime_ = -10.0;
    lastRecvTime_ = 0.0;
    statusText_ = "WAITING FOR CHALLENGER";
    return true;
}

bool NetSession::startClient(const std::string& hostCodeOrIp, uint16_t fallbackPort) {
    NetEndpoint target{};
    if (!parseTarget(hostCodeOrIp, fallbackPort, target)) {
        statusText_ = "INVALID CODE OR IP";
        return false;
    }

    // Keep already-bound STUN lobby socket so the public port matches our advertised guest code!
    if (sockFd_ == -1) {
        stop();
        if (!bindUdpSocket(0)) return false;
        sendStunBindingRequests();
    } else {
        pendingInputs_.clear();
        hasSnapshot_ = false;
        lastSnapshotTick_ = 0;
    }

    mode_ = NetMode::Client;
    connected_ = false;
    peer_ = target;
    punchTarget_ = target;
    lastSendTime_ = -10.0;
    lastPunchTime_ = -10.0;
    lastRecvTime_ = 0.0;
    statusText_ = "PUNCHING / CONNECTING...";
    return true;
}

bool NetSession::sendRaw(const NetEndpoint& to, const uint8_t* data, size_t len) {
    if (sockFd_ == -1 || !to.valid() || !data || len == 0) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = to.ipv4;
    addr.sin_port = to.port;
#ifdef _WIN32
    int sent = sendto(static_cast<SOCKET>(sockFd_), reinterpret_cast<const char*>(data),
                      static_cast<int>(len), 0, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    return sent == static_cast<int>(len);
#else
    ssize_t sent = sendto(static_cast<int>(sockFd_), data, len, 0,
                          reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    return sent == static_cast<ssize_t>(len);
#endif
}

bool NetSession::sendHandshakeAck(uint64_t seed, PlayerRole hostRole, uint32_t roundId) {
    seed_ = seed;
    hostRole_ = hostRole;
    localRole_ = hostRole;
    roundId_ = roundId;
    if (mode_ != NetMode::Host || !peer_.valid()) return false;
    ByteWriter w;
    w.writeHeader(PKT_HANDSHAKE_ACK);
    w.writeU32(PROTOCOL_VERSION);
    w.writeU64(seed);
    w.writeU8(static_cast<uint8_t>(hostRole));
    w.writeU32(roundId);
    return sendRaw(peer_, w.buf.data(), w.buf.size());
}

void NetSession::poll(double nowSec) {
    if (sockFd_ == -1) return;

    // Periodic STUN refresh to keep NAT mapping fresh while waiting in lobby/pre-connect
    if (!connected_ && (nowSec - lastStunTime_ >= 12.0)) {
        sendStunBindingRequests();
        lastStunTime_ = nowSec;
    }

    // Periodic NAT hole punch packet if punch target is set & not yet connected
    if (!connected_ && punchTarget_.valid() && (nowSec - lastPunchTime_ >= 0.15)) {
        ByteWriter pw;
        pw.writeHeader(PKT_PUNCH);
        pw.writeU32(PROTOCOL_VERSION);
        sendRaw(punchTarget_, pw.buf.data(), pw.buf.size());
        lastPunchTime_ = nowSec;
    }

    if (mode_ == NetMode::Client && !connected_) {
        if (nowSec - lastSendTime_ >= 0.18) {
            ByteWriter w;
            w.writeHeader(PKT_HANDSHAKE_REQ);
            w.writeU32(PROTOCOL_VERSION);
            sendRaw(peer_, w.buf.data(), w.buf.size());
            lastSendTime_ = nowSec;
        }
    } else if (connected_ && peer_.valid() && (nowSec - lastSendTime_ >= 1.0)) {
        ByteWriter w;
        w.writeHeader(PKT_PING);
        sendRaw(peer_, w.buf.data(), w.buf.size());
        lastSendTime_ = nowSec;
    }

    uint8_t buf[8192];
    for (int iter = 0; iter < 64; ++iter) {
        sockaddr_in from{};
#ifdef _WIN32
        int fromLen = sizeof(from);
        int n = recvfrom(static_cast<SOCKET>(sockFd_), reinterpret_cast<char*>(buf),
                         sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n <= 0) break;
#else
        socklen_t fromLen = sizeof(from);
        ssize_t n = recvfrom(static_cast<int>(sockFd_), buf, sizeof(buf), 0,
                             reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n <= 0) break;
#endif
        if (handleStunPacket(buf, static_cast<size_t>(n))) continue;
        if (n < 5) continue;
        if (buf[0] != MAGIC0 || buf[1] != MAGIC1 || buf[2] != MAGIC2 || buf[3] != MAGIC3) continue;

        NetEndpoint src{from.sin_addr.s_addr, from.sin_port};
        uint8_t ptype = buf[4];

        if (mode_ == NetMode::Host) {
            if (ptype == PKT_PUNCH) {
                // Immediately reply with punch + allow peer's NAT pinhole
                ByteWriter pw;
                pw.writeHeader(PKT_PUNCH);
                pw.writeU32(PROTOCOL_VERSION);
                sendRaw(src, pw.buf.data(), pw.buf.size());
            } else if (ptype == PKT_HANDSHAKE_REQ) {
                ByteReader r(buf, static_cast<size_t>(n));
                r.readHeader(PKT_HANDSHAKE_REQ);
                uint32_t ver = r.readU32();
                if (r.ok() && ver == PROTOCOL_VERSION) {
                    peer_ = src;
                    connected_ = true;
                    lastRecvTime_ = nowSec;
                    statusText_ = "CHALLENGER CONNECTED";
                    sendHandshakeAck(seed_, hostRole_, roundId_);
                }
            } else if (src == peer_) {
                lastRecvTime_ = nowSec;
                if (ptype == PKT_CLIENT_INPUT) {
                    NetInputFrame f;
                    if (deserializeClientInput(buf, static_cast<size_t>(n), f)) {
                        pendingInputs_.push_back(f);
                        if (pendingInputs_.size() > 64) {
                            pendingInputs_.erase(pendingInputs_.begin(),
                                                 pendingInputs_.begin() + static_cast<ptrdiff_t>(pendingInputs_.size() - 64));
                        }
                    }
                } else if (ptype == PKT_DISCONNECT) {
                    connected_ = false;
                    peer_ = NetEndpoint{};
                    statusText_ = "CHALLENGER DISCONNECTED";
                }
            }
        } else if (mode_ == NetMode::Client && src == peer_) {
            lastRecvTime_ = nowSec;
            if (ptype == PKT_PUNCH) {
                // Hole punched cleanly; expedite handshake request
                ByteWriter hw;
                hw.writeHeader(PKT_HANDSHAKE_REQ);
                hw.writeU32(PROTOCOL_VERSION);
                sendRaw(peer_, hw.buf.data(), hw.buf.size());
            } else if (ptype == PKT_HANDSHAKE_ACK) {
                ByteReader r(buf, static_cast<size_t>(n));
                r.readHeader(PKT_HANDSHAKE_ACK);
                uint32_t ver = r.readU32();
                uint64_t seed = r.readU64();
                uint8_t hRole = r.readU8();
                uint32_t rId = r.readU32();
                if (r.ok() && ver == PROTOCOL_VERSION) {
                    seed_ = seed;
                    hostRole_ = (hRole == 1) ? PlayerRole::Monster : PlayerRole::Human;
                    localRole_ = (hostRole_ == PlayerRole::Human) ? PlayerRole::Monster : PlayerRole::Human;
                    roundId_ = rId;
                    connected_ = true;
                    statusText_ = "CONNECTED";
                }
            } else if (ptype == PKT_WORLD_SNAPSHOT) {
                NetWorldSnapshot snap;
                if (deserializeWorldSnapshot(buf, static_cast<size_t>(n), snap)) {
                    if (!hasSnapshot_ || snap.serverTick >= lastSnapshotTick_ || snap.roundId != roundId_) {
                        latestSnapshot_ = std::move(snap);
                        hasSnapshot_ = true;
                        lastSnapshotTick_ = latestSnapshot_.serverTick;
                        seed_ = latestSnapshot_.baseSeed;
                        roundId_ = latestSnapshot_.roundId;
                        connected_ = true;
                    }
                }
            } else if (ptype == PKT_DISCONNECT) {
                connected_ = false;
                statusText_ = "HOST DISCONNECTED";
            }
        }
    }

    if (connected_ && lastRecvTime_ > 0.0 && (nowSec - lastRecvTime_ > 8.0)) {
        connected_ = false;
        statusText_ = (mode_ == NetMode::Host) ? "CHALLENGER TIMED OUT" : "CONNECTION LOST";
        if (mode_ == NetMode::Host) peer_ = NetEndpoint{};
    }
}

bool NetSession::sendClientInput(const NetInputFrame& frame) {
    if (mode_ != NetMode::Client || !connected_ || !peer_.valid()) return false;
    auto bytes = serializeClientInput(frame);
    return sendRaw(peer_, bytes.data(), bytes.size());
}

bool NetSession::sendWorldSnapshot(const NetWorldSnapshot& snap) {
    if (mode_ != NetMode::Host || !connected_ || !peer_.valid()) return false;
    auto bytes = serializeWorldSnapshot(snap);
    return sendRaw(peer_, bytes.data(), bytes.size());
}

bool NetSession::popLatestClientInput(NetInputFrame& out) {
    if (pendingInputs_.empty()) return false;
    out = pendingInputs_.back();
    pendingInputs_.clear();
    return true;
}

bool NetSession::popAllClientInputs(std::vector<NetInputFrame>& out) {
    if (pendingInputs_.empty()) return false;
    out = std::move(pendingInputs_);
    pendingInputs_.clear();
    return true;
}

bool NetSession::popLatestSnapshot(NetWorldSnapshot& out) {
    if (!hasSnapshot_) return false;
    out = std::move(latestSnapshot_);
    hasSnapshot_ = false;
    return true;
}

std::vector<uint8_t> NetSession::serializeClientInput(const NetInputFrame& f) {
    ByteWriter w;
    w.writeHeader(PKT_CLIENT_INPUT);
    w.writeU32(f.seq);
    w.writeU32(f.lastAckEventId);
    w.writeU8(f.role);
    w.writeFloat(f.moveForward);
    w.writeFloat(f.moveRight);
    w.writeFloat(f.yaw);
    w.writeFloat(f.pitch);
    w.writeU16(f.flags);
    w.writeFloat(f.dragDx);
    w.writeFloat(f.dragDy);
    w.writeVec3(f.rayOrig);
    w.writeVec3(f.rayDir);
    w.writeI32(f.craftOptionIdx);
    w.writeU8(static_cast<uint8_t>(f.quickUseSlot));
    return w.buf;
}

bool NetSession::deserializeClientInput(const uint8_t* data, size_t len, NetInputFrame& out) {
    ByteReader r(data, len);
    if (!r.readHeader(PKT_CLIENT_INPUT)) return false;
    out.seq = r.readU32();
    out.lastAckEventId = r.readU32();
    out.role = r.readU8();
    out.moveForward = r.readFloat();
    out.moveRight = r.readFloat();
    out.yaw = r.readFloat();
    out.pitch = r.readFloat();
    out.flags = r.readU16();
    out.dragDx = r.readFloat();
    out.dragDy = r.readFloat();
    out.rayOrig = r.readVec3();
    out.rayDir = r.readVec3();
    out.craftOptionIdx = r.readI32();
    out.quickUseSlot = static_cast<int8_t>(r.readU8());
    return r.ok();
}

std::vector<uint8_t> NetSession::serializeWorldSnapshot(const NetWorldSnapshot& s) {
    ByteWriter w;
    w.writeHeader(PKT_WORLD_SNAPSHOT);
    w.writeU32(s.serverTick);
    w.writeU32(s.ackInputSeq);
    w.writeU32(s.roundId);
    w.writeU64(s.baseSeed);
    w.writeU8(s.matchPhase);
    w.writeI16(s.streamRegX);
    w.writeI16(s.streamRegZ);

    // Human
    w.writeVec3(s.human.pos);
    w.writeVec3(s.human.vel);
    w.writeFloat(s.human.yaw);
    w.writeFloat(s.human.pitch);
    w.writeFloat(s.human.health);
    w.writeFloat(s.human.hunger);
    w.writeFloat(s.human.thirst);
    w.writeFloat(s.human.energy);
    w.writeFloat(s.human.battery);
    w.writeU8(s.human.hasFlashlight);
    w.writeU8(s.human.flashlightOn);
    w.writeU8(s.human.hasHeldItem);
    w.writeU8(s.human.heldPlaceMode);
    w.writeU8(s.human.draggingItem);
    w.writeU8(s.human.heldItemType);
    w.writeU8(s.human.heldRotX);
    w.writeU8(s.human.heldRotY);
    w.writeU8(s.human.heldRotZ);
    w.writeU8(s.human.dragItemType);
    w.writeU8(s.human.dragRotX);
    w.writeU8(s.human.dragRotY);
    w.writeU8(s.human.dragRotZ);
    w.writeU8(s.human.backpackMode);
    w.writeVec3(s.human.backpackGroundPos);
    w.writeFloat(s.human.backpackGroundYaw);
    w.writeFloat(s.human.backpackViewYaw);
    w.writeFloat(s.human.backpackViewPitch);
    w.writeFloat(s.human.meleeSwingTimer);
    w.writeFloat(s.human.damageFlash);
    w.writeI16(s.human.hiddenLockerIdx);

    w.writeU8(s.human.lockpick.active);
    if (s.human.lockpick.active) {
        w.writeI16(s.human.lockpick.targetDoorIdx);
        w.writeFloat(s.human.lockpick.pickX);
        w.writeFloat(s.human.lockpick.pickY);
        for (int p = 0; p < 3; ++p) {
            w.writeFloat(s.human.lockpick.pins[p].pos);
            w.writeFloat(s.human.lockpick.pins[p].target);
            w.writeU8(s.human.lockpick.pins[p].pinned);
            w.writeFloat(s.human.lockpick.pins[p].brokenTimer);
        }
    }

    uint8_t bpCount = static_cast<uint8_t>(std::min<size_t>(s.human.backpackItems.size(), 48));
    w.writeU8(bpCount);
    for (uint8_t i = 0; i < bpCount; ++i) {
        const auto& bi = s.human.backpackItems[i];
        w.writeU16(bi.id);
        w.writeU8(bi.type);
        w.writeU8(bi.gx);
        w.writeU8(bi.gy);
        w.writeU8(bi.gz);
        w.writeU8(bi.rotX);
        w.writeU8(bi.rotY);
        w.writeU8(bi.rotZ);
        w.writeU8(bi.durability);
        w.writeU8(bi.inBackSlot);
    }

    // Monster
    w.writeVec3(s.monster.pos);
    w.writeVec3(s.monster.vel);
    w.writeFloat(s.monster.yaw);
    w.writeFloat(s.monster.pitch);
    w.writeFloat(s.monster.hp);
    w.writeFloat(s.monster.maxHp);
    w.writeFloat(s.monster.stamina);
    w.writeFloat(s.monster.dashCooldown);
    w.writeFloat(s.monster.dashTimer);
    w.writeFloat(s.monster.attackCooldown);
    w.writeFloat(s.monster.attackSwingTimer);
    w.writeFloat(s.monster.hurtFlash);
    w.writeU8(s.monster.nightVision);
    w.writeU8(s.monster.sprinting);
    w.writeVec3(s.monster.footPos[0]);
    w.writeVec3(s.monster.footPos[1]);
    w.writeFloat(s.monster.pelvisOffsetY);
    w.writeFloat(s.monster.armAngle[0]);
    w.writeFloat(s.monster.armAngle[1]);
    w.writeFloat(s.monster.torsoLeanX);

    // Doors (cap to keep UDP packet comfortably under MTU)
    uint16_t doorCount = static_cast<uint16_t>(std::min<size_t>(s.doors.size(), 96));
    w.writeU16(doorCount);
    for (uint16_t i = 0; i < doorCount; ++i) {
        w.writeU16(s.doors[i].index);
        w.writeFloat(s.doors[i].angle);
        w.writeFloat(s.doors[i].angularVel);
        w.writeU8(s.doors[i].locked);
    }

    // World items picked-up bitset + extra dynamic items
    w.writeU16(s.baseItemCount);
    uint16_t bitByteCount = static_cast<uint16_t>(std::min<size_t>(s.baseItemPickedUpBits.size(), 512));
    w.writeU16(bitByteCount);
    for (uint16_t i = 0; i < bitByteCount; ++i) {
        w.writeU8(s.baseItemPickedUpBits[i]);
    }

    uint16_t extraCount = static_cast<uint16_t>(std::min<size_t>(s.extraItems.size(), 64));
    w.writeU16(extraCount);
    for (uint16_t i = 0; i < extraCount; ++i) {
        const auto& ei = s.extraItems[i];
        w.writeU16(ei.index);
        w.writeVec3(ei.pos);
        w.writeU8(ei.type);
        w.writeU8(ei.rotX);
        w.writeU8(ei.rotY);
        w.writeU8(ei.rotZ);
        w.writeU8(ei.durability);
        w.writeU8(ei.pickedUp);
    }

    // Sound events
    uint8_t evCount = static_cast<uint8_t>(std::min<size_t>(s.soundEvents.size(), 16));
    w.writeU8(evCount);
    for (uint8_t i = 0; i < evCount; ++i) {
        const auto& ev = s.soundEvents[i];
        w.writeU32(ev.eventId);
        w.writeU8(ev.soundId);
        w.writeU8(ev.spatial);
        w.writeVec3(ev.pos);
        w.writeFloat(ev.volume);
        w.writeFloat(ev.param1);
        w.writeFloat(ev.param2);
    }

    return w.buf;
}

bool NetSession::deserializeWorldSnapshot(const uint8_t* data, size_t len, NetWorldSnapshot& out) {
    ByteReader r(data, len);
    if (!r.readHeader(PKT_WORLD_SNAPSHOT)) return false;

    out.serverTick = r.readU32();
    out.ackInputSeq = r.readU32();
    out.roundId = r.readU32();
    out.baseSeed = r.readU64();
    out.matchPhase = r.readU8();
    out.streamRegX = r.readI16();
    out.streamRegZ = r.readI16();

    // Human
    out.human.pos = r.readVec3();
    out.human.vel = r.readVec3();
    out.human.yaw = r.readFloat();
    out.human.pitch = r.readFloat();
    out.human.health = r.readFloat();
    out.human.hunger = r.readFloat();
    out.human.thirst = r.readFloat();
    out.human.energy = r.readFloat();
    out.human.battery = r.readFloat();
    out.human.hasFlashlight = r.readU8();
    out.human.flashlightOn = r.readU8();
    out.human.hasHeldItem = r.readU8();
    out.human.heldPlaceMode = r.readU8();
    out.human.draggingItem = r.readU8();
    out.human.heldItemType = r.readU8();
    out.human.heldRotX = r.readU8();
    out.human.heldRotY = r.readU8();
    out.human.heldRotZ = r.readU8();
    out.human.dragItemType = r.readU8();
    out.human.dragRotX = r.readU8();
    out.human.dragRotY = r.readU8();
    out.human.dragRotZ = r.readU8();
    out.human.backpackMode = r.readU8();
    out.human.backpackGroundPos = r.readVec3();
    out.human.backpackGroundYaw = r.readFloat();
    out.human.backpackViewYaw = r.readFloat();
    out.human.backpackViewPitch = r.readFloat();
    out.human.meleeSwingTimer = r.readFloat();
    out.human.damageFlash = r.readFloat();
    out.human.hiddenLockerIdx = r.readI16();

    out.human.lockpick.active = r.readU8();
    if (out.human.lockpick.active) {
        out.human.lockpick.targetDoorIdx = r.readI16();
        out.human.lockpick.pickX = r.readFloat();
        out.human.lockpick.pickY = r.readFloat();
        for (int p = 0; p < 3; ++p) {
            out.human.lockpick.pins[p].pos = r.readFloat();
            out.human.lockpick.pins[p].target = r.readFloat();
            out.human.lockpick.pins[p].pinned = r.readU8();
            out.human.lockpick.pins[p].brokenTimer = r.readFloat();
        }
    }

    uint8_t bpCount = r.readU8();
    if (bpCount > 48 || !r.ok()) return false;
    out.human.backpackItems.resize(bpCount);
    for (uint8_t i = 0; i < bpCount; ++i) {
        out.human.backpackItems[i].id = r.readU16();
        out.human.backpackItems[i].type = r.readU8();
        out.human.backpackItems[i].gx = r.readU8();
        out.human.backpackItems[i].gy = r.readU8();
        out.human.backpackItems[i].gz = r.readU8();
        out.human.backpackItems[i].rotX = r.readU8();
        out.human.backpackItems[i].rotY = r.readU8();
        out.human.backpackItems[i].rotZ = r.readU8();
        out.human.backpackItems[i].durability = r.readU8();
        out.human.backpackItems[i].inBackSlot = r.readU8();
    }

    // Monster
    out.monster.pos = r.readVec3();
    out.monster.vel = r.readVec3();
    out.monster.yaw = r.readFloat();
    out.monster.pitch = r.readFloat();
    out.monster.hp = r.readFloat();
    out.monster.maxHp = r.readFloat();
    out.monster.stamina = r.readFloat();
    out.monster.dashCooldown = r.readFloat();
    out.monster.dashTimer = r.readFloat();
    out.monster.attackCooldown = r.readFloat();
    out.monster.attackSwingTimer = r.readFloat();
    out.monster.hurtFlash = r.readFloat();
    out.monster.nightVision = r.readU8();
    out.monster.sprinting = r.readU8();
    out.monster.footPos[0] = r.readVec3();
    out.monster.footPos[1] = r.readVec3();
    out.monster.pelvisOffsetY = r.readFloat();
    out.monster.armAngle[0] = r.readFloat();
    out.monster.armAngle[1] = r.readFloat();
    out.monster.torsoLeanX = r.readFloat();

    uint16_t doorCount = r.readU16();
    if (doorCount > 96 || !r.ok()) return false;
    out.doors.resize(doorCount);
    for (uint16_t i = 0; i < doorCount; ++i) {
        out.doors[i].index = r.readU16();
        out.doors[i].angle = r.readFloat();
        out.doors[i].angularVel = r.readFloat();
        out.doors[i].locked = r.readU8();
    }

    out.baseItemCount = r.readU16();
    uint16_t bitByteCount = r.readU16();
    if (bitByteCount > 512 || !r.ok()) return false;
    out.baseItemPickedUpBits.resize(bitByteCount);
    for (uint16_t i = 0; i < bitByteCount; ++i) {
        out.baseItemPickedUpBits[i] = r.readU8();
    }

    uint16_t extraCount = r.readU16();
    if (extraCount > 64 || !r.ok()) return false;
    out.extraItems.resize(extraCount);
    for (uint16_t i = 0; i < extraCount; ++i) {
        out.extraItems[i].index = r.readU16();
        out.extraItems[i].pos = r.readVec3();
        out.extraItems[i].type = r.readU8();
        out.extraItems[i].rotX = r.readU8();
        out.extraItems[i].rotY = r.readU8();
        out.extraItems[i].rotZ = r.readU8();
        out.extraItems[i].durability = r.readU8();
        out.extraItems[i].pickedUp = r.readU8();
    }

    uint8_t evCount = r.readU8();
    if (evCount > 16 || !r.ok()) return false;
    out.soundEvents.resize(evCount);
    for (uint8_t i = 0; i < evCount; ++i) {
        out.soundEvents[i].eventId = r.readU32();
        out.soundEvents[i].soundId = r.readU8();
        out.soundEvents[i].spatial = r.readU8();
        out.soundEvents[i].pos = r.readVec3();
        out.soundEvents[i].volume = r.readFloat();
        out.soundEvents[i].param1 = r.readFloat();
        out.soundEvents[i].param2 = r.readFloat();
    }

    return r.ok();
}

std::string NetSession::detectLocalLanIp() {
#ifdef _WIN32
    WSADATA wsa{};
    bool started = (WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    std::string out = "127.0.0.1";
    if (s != INVALID_SOCKET) {
        sockaddr_in remote{};
        remote.sin_family = AF_INET;
        remote.sin_port = htons(53);
        inet_pton(AF_INET, "8.8.8.8", &remote.sin_addr);
        if (connect(s, reinterpret_cast<const sockaddr*>(&remote), sizeof(remote)) == 0) {
            sockaddr_in name{};
            int nlen = sizeof(name);
            if (getsockname(s, reinterpret_cast<sockaddr*>(&name), &nlen) == 0) {
                char buf[64]{};
                if (inet_ntop(AF_INET, &name.sin_addr, buf, sizeof(buf))) out = buf;
            }
        }
        closesocket(s);
    }
    if (started) WSACleanup();
    return out;
#else
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    std::string out = "127.0.0.1";
    if (s >= 0) {
        sockaddr_in remote{};
        remote.sin_family = AF_INET;
        remote.sin_port = htons(53);
        inet_pton(AF_INET, "8.8.8.8", &remote.sin_addr);
        if (connect(s, reinterpret_cast<const sockaddr*>(&remote), sizeof(remote)) == 0) {
            sockaddr_in name{};
            socklen_t nlen = sizeof(name);
            if (getsockname(s, reinterpret_cast<sockaddr*>(&name), &nlen) == 0) {
                char buf[64]{};
                if (inet_ntop(AF_INET, &name.sin_addr, buf, sizeof(buf))) out = buf;
            }
        }
        close(s);
    }
    return out;
#endif
}

std::string NetSession::encodeRoomCode(uint32_t ipv4NetOrder, uint16_t portNetOrder) {
    uint8_t bytes[7]{};
    std::memcpy(bytes, &ipv4NetOrder, 4);
    std::memcpy(bytes + 4, &portNetOrder, 2);
    bytes[6] = crc8Bytes(bytes, 6);

    uint64_t val = 0;
    for (int i = 0; i < 7; ++i) {
        val = (val << 8) | bytes[i];
    }
    // 56 bits -> 12 Base32 chars (top 4 bits padded 0, lowest 60 bits)
    std::string raw(12, '0');
    for (int i = 11; i >= 0; --i) {
        raw[static_cast<size_t>(i)] = B32_ALPHABET[val & 31u];
        val >>= 5;
    }
    return raw.substr(0, 4) + "-" + raw.substr(4, 4) + "-" + raw.substr(8, 4);
}

bool NetSession::decodeRoomCode(const std::string& code, NetEndpoint& out) {
    std::string clean;
    clean.reserve(16);
    for (char c : code) {
        if (c == '-' || c == ' ' || c == '\t') continue;
        clean.push_back(c);
    }
    if (clean.size() != 12) return false;

    uint64_t val = 0;
    for (char c : clean) {
        int d = decodeB32Char(c);
        if (d < 0) return false;
        val = (val << 5) | static_cast<uint64_t>(d);
    }
    // Reject top 4 unused bits if non-zero
    if ((val >> 56) != 0) return false;

    uint8_t bytes[7]{};
    for (int i = 6; i >= 0; --i) {
        bytes[i] = static_cast<uint8_t>(val & 0xFFu);
        val >>= 8;
    }
    if (crc8Bytes(bytes, 6) != bytes[6]) return false;

    std::memcpy(&out.ipv4, bytes, 4);
    std::memcpy(&out.port, bytes + 4, 2);
    return out.valid();
}

bool NetSession::parseTarget(const std::string& codeOrIp, uint16_t fallbackPort, NetEndpoint& out) {
    if (decodeRoomCode(codeOrIp, out)) return true;

    std::string trimmed;
    for (char c : codeOrIp) {
        if (!std::isspace(static_cast<unsigned char>(c))) trimmed.push_back(c);
    }
    if (trimmed.empty()) return false;

    std::string host = trimmed;
    uint16_t port = fallbackPort;
    size_t colon = trimmed.find(':');
    if (colon != std::string::npos) {
        host = trimmed.substr(0, colon);
        int p = std::atoi(trimmed.substr(colon + 1).c_str());
        if (p > 0 && p <= 65535) port = static_cast<uint16_t>(p);
    }

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) {
        return false;
    }
    const auto* sin = reinterpret_cast<const sockaddr_in*>(res->ai_addr);
    out.ipv4 = sin->sin_addr.s_addr;
    out.port = sin->sin_port;
    freeaddrinfo(res);
    return out.valid();
}

} // namespace mega
