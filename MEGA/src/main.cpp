#include "audio/AudioEngine.hpp"
#include "core/Config.hpp"
#include "core/Math3D.hpp"
#include "core/Platform.hpp"
#include "game/GameWorld.hpp"
#include "net/NetSession.hpp"
#include "render/BillboardRenderer.hpp"
#include "render/Framebuffer.hpp"
#include "render/GlApi.hpp"
#include "render/HudRenderer.hpp"
#include "render/RobotRenderer.hpp"
#include "render/Shader.hpp"
#include "render/TextureAtlas.hpp"
#include "render/WorldMesher.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace mega;

static const char* WORLD_VS = R"(#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec3 aUVLayer;
layout (location = 3) in float aAO;
layout (location = 4) in float aEmissive;

out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vUVLayer;
out float vAO;
out float vEmissive;
out float vViewDist;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
uniform vec3 uPartScale;
uniform float uTexLayerOverride;
uniform float uEmissiveOverride;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = normalize(mat3(uModel) * aNormal);

    vec2 uv = aUVLayer.xy;
    if (uPartScale.x > 0.001) {
        if (abs(aNormal.x) > 0.5) uv *= vec2(uPartScale.z, uPartScale.y);
        else if (abs(aNormal.y) > 0.5) uv *= vec2(uPartScale.x, uPartScale.z);
        else uv *= vec2(uPartScale.x, uPartScale.y);
    }
    float layer = (uTexLayerOverride >= 0.0) ? uTexLayerOverride : aUVLayer.z;
    vUVLayer = vec3(uv, layer);
    vAO = aAO;
    vEmissive = (uEmissiveOverride >= 0.0) ? uEmissiveOverride : aEmissive;

    vec4 viewPos = uView * worldPos;
    vViewDist = length(viewPos.xyz);
    gl_Position = uProj * viewPos;
}
)";

// Real-Time 3D Per-Pixel Lighting with Pixelated Texel-Snapped Light & Shadow Bands
static const char* WORLD_FS = R"(#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec3 vUVLayer;
in float vAO;
in float vEmissive;
in float vViewDist;

out vec4 FragColor;

uniform sampler2DArray uAtlas;
uniform vec3 uCamPos;
uniform vec3 uCamDir;
uniform float uFlashlight;
uniform vec3 uTint;

uniform float uAmbientBrightness;
uniform float uFlashlightIntensity;
uniform float uFlashlightRadius;
uniform vec3 uFlashlightColor;
uniform float uObjectEmissiveScale;
uniform float uFogDensity;
uniform float uLightTexelSnap;
uniform float uLightSteps;
uniform float uLightDitherStrength;

const int MAX_LIGHTS = 48;
uniform int uLightCount;
uniform vec4 uLightPosRadius[MAX_LIGHTS];
uniform vec4 uLightColorIntensity[MAX_LIGHTS];

float texLuma(vec3 uvl) {
    vec3 c = texture(uAtlas, uvl).rgb;
    return dot(c, vec3(0.299, 0.587, 0.114));
}

float bayer2x2(vec2 p) {
    int x = int(mod(p.x, 2.0));
    int y = int(mod(p.y, 2.0));
    int idx = y * 2 + x;
    float m[4] = float[4](-0.375, 0.125, 0.375, -0.125);
    return m[idx];
}

void main() {
    vec4 tex = texture(uAtlas, vUVLayer);
    if (tex.a < 0.5) discard;

    if (vEmissive > 0.5) {
        vec3 lampCol = tex.rgb * vec3(1.55, 1.75, 2.05) * uObjectEmissiveScale;
        float fog = 1.0 - exp(-max(0.0, vViewDist - 6.0) * uFogDensity);
        FragColor = vec4(mix(lampCol, vec3(0.015, 0.02, 0.028), clamp(fog * 0.65, 0.0, 1.0)), 1.0);
        return;
    }

    vec3 absN = abs(vNormal);
    vec3 samplePos = vWorldPos;
    if (uLightTexelSnap > 1.0) {
        vec3 snapped = (floor(vWorldPos * uLightTexelSnap) + 0.5) / uLightTexelSnap;
        if (absN.x < 0.5) samplePos.x = snapped.x;
        if (absN.y < 0.5) samplePos.y = snapped.y;
        if (absN.z < 0.5) samplePos.z = snapped.z;
    }

    vec3 N = normalize(vNormal);
    vec3 T = (abs(N.y) > 0.5) ? vec3(1.0, 0.0, 0.0) : vec3(-N.z, 0.0, N.x);
    vec3 B = cross(N, T);

    float eps = 1.0 / 32.0;
    vec2 snappedUV = floor(vUVLayer.xy * 32.0) / 32.0 + vec2(0.5 / 32.0);
    float h0 = texLuma(vec3(snappedUV, vUVLayer.z));
    float hx = texLuma(vec3(snappedUV + vec2(eps, 0.0), vUVLayer.z)) - h0;
    float hy = texLuma(vec3(snappedUV + vec2(0.0, eps), vUVLayer.z)) - h0;
    N = normalize(N - (T * hx + B * hy) * 1.45);

    int layerId = int(vUVLayer.z + 0.5);
    float specStrength = 0.15;
    float shininess = 16.0;
    if (layerId == 2 || layerId == 4) {
        specStrength = 0.65;
        shininess = 32.0;
    } else if (layerId == 5) {
        specStrength = 0.42;
        shininess = 24.0;
    } else if (layerId == 3) {
        specStrength = 0.12;
        shininess = 12.0;
    }

    vec3 V = normalize(uCamPos - samplePos);
    vec3 totalDiffuse = vec3(0.82, 0.90, 1.0) * uAmbientBrightness * vAO;
    vec3 totalSpec = vec3(0.0);

    for (int i = 0; i < uLightCount; ++i) {
        vec3 lPos = uLightPosRadius[i].xyz;
        float radius = uLightPosRadius[i].w;
        vec3 toLight = lPos - samplePos;
        float distSq = dot(toLight, toLight);
        if (distSq > radius * radius) continue;

        float dist = sqrt(distSq);
        vec3 L = toLight / max(dist, 0.001);

        float normDist = dist / radius;
        float window = clamp(1.0 - normDist * normDist, 0.0, 1.0);
        float atten = (window * window) / (1.0 + 0.20 * dist + 0.08 * distSq);

        vec3 lCol = uLightColorIntensity[i].rgb * uLightColorIntensity[i].w;
        float ndotl = max(dot(N, L), 0.0);
        float wrapDiff = max(dot(N, L) * 0.75 + 0.25, 0.0) * 0.45 + ndotl * 0.65;
        totalDiffuse += lCol * wrapDiff * atten;

        if (ndotl > 0.0) {
            vec3 H = normalize(L + V);
            float spec = pow(max(dot(N, H), 0.0), shininess) * specStrength;
            totalSpec += lCol * spec * atten;
        }
    }

    if (uFlashlight > 0.5) {
        vec3 toLight = uCamPos - samplePos;
        float fDist = length(toLight);
        if (fDist < uFlashlightRadius) {
            vec3 L = toLight / max(fDist, 0.001);
            float spotCos = dot(-L, normalize(uCamDir));
            float cone = smoothstep(0.84, 0.955, spotCos);
            if (cone > 0.0) {
                float fAtten = cone * clamp(1.0 - fDist / uFlashlightRadius, 0.0, 1.0)
                               / (1.0 + 0.08 * fDist + 0.015 * fDist * fDist);
                vec3 fCol = uFlashlightColor * uFlashlightIntensity;
                float ndotl = max(dot(N, L), 0.0);
                totalDiffuse += fCol * ndotl * fAtten;

                vec3 H = normalize(L + V);
                float spec = pow(max(dot(N, H), 0.0), shininess) * specStrength;
                totalSpec += fCol * spec * fAtten;
            }
        }
    }

    if (uLightSteps > 1.0) {
        vec2 texelCoord = floor(vUVLayer.xy * 32.0);
        float dither = bayer2x2(texelCoord) * uLightDitherStrength;
        float diffLum = max(max(totalDiffuse.r, totalDiffuse.g), totalDiffuse.b);
        if (diffLum > 0.0001) {
            float qLum = max(0.0, floor(diffLum * uLightSteps + dither + 0.5) / uLightSteps);
            totalDiffuse *= (qLum / diffLum);
        }
        totalSpec = floor(totalSpec * uLightSteps + 0.5) / uLightSteps;
    }

    vec3 litColor = (tex.rgb * uTint) * (totalDiffuse * mix(0.35, 1.0, vAO)) + totalSpec * mix(0.5, 1.0, vAO);

    float fogFactor = 1.0 - exp(-max(0.0, vViewDist - 4.0) * uFogDensity);
    vec3 fogColor = vec3(0.014, 0.018, 0.024);
    vec3 finalCol = mix(litColor, fogColor, clamp(fogFactor, 0.0, 1.0));

    FragColor = vec4(finalCol, 1.0);
}
)";

int main(int argc, char** argv) {
    bool visualTest = false;
    bool cliHost = false;
    bool cliJoin = false;
    bool cliSolo = false;
    std::string cliJoinIp;
    std::string cliPunchCode;
    uint16_t cliPort = NetSession::DEFAULT_PORT;
    PlayerRole cliHostRole = PlayerRole::Human;
    uint64_t worldSeed = 0x4D4547410001ULL;
    bool seedProvided = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--visual-test") == 0) {
            visualTest = true;
            cliSolo = true;
        } else if (std::strcmp(argv[i], "--host") == 0) {
            cliHost = true;
        } else if (std::strcmp(argv[i], "--solo") == 0) {
            cliSolo = true;
        } else if (std::strcmp(argv[i], "--join") == 0 && i + 1 < argc) {
            cliJoin = true;
            cliJoinIp = argv[++i];
        } else if (std::strcmp(argv[i], "--punch") == 0 && i + 1 < argc) {
            cliPunchCode = argv[++i];
        } else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            int p = std::atoi(argv[++i]);
            if (p > 0 && p <= 65535) cliPort = static_cast<uint16_t>(p);
        } else if (std::strcmp(argv[i], "--role") == 0 && i + 1 < argc) {
            std::string r = argv[++i];
            cliHostRole = (r == "monster" || r == "Monster") ? PlayerRole::Monster : PlayerRole::Human;
        } else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            worldSeed = static_cast<uint64_t>(std::strtoull(argv[++i], nullptr, 10));
            seedProvided = true;
        }
    }

    if (!seedProvided) {
        worldSeed ^= static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        if (worldSeed == 0) worldSeed = 0x4D4547410001ULL;
    }

    platform::Window* window = platform::createWindow(
        "MEGA // 1v1 Survival vs Stalker",
        cfg::WINDOW_WIDTH, cfg::WINDOW_HEIGHT
    );
    if (!window) {
        std::cerr << "Failed to create OpenGL 3.3 Core window\n";
        return 1;
    }
    if (!initGlApi()) {
        std::cerr << "Failed to initialize OpenGL 3.3 Core functions\n";
        platform::destroyWindow(window);
        return 1;
    }
    platform::setSwapInterval(1);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    {
    Framebuffer fbo;
    if (!fbo.init(cfg::RETRO_WIDTH, cfg::RETRO_HEIGHT)) return 1;

    Shader worldShader;
    if (!worldShader.compile(WORLD_VS, WORLD_FS)) return 1;

    TextureAtlas atlas;
    if (!atlas.init()) return 1;

    BillboardRenderer billboards;
    if (!billboards.init()) return 1;

    RobotRenderer robotRenderer;
    if (!robotRenderer.init()) return 1;

    HudRenderer hud;
    if (!hud.init(cfg::RETRO_WIDTH, cfg::RETRO_HEIGHT)) return 1;

    AudioEngine audio;
    audio.init();

    GameWorld gw;
    gw.settings = GameSettings::loadOrCreate("mega.cfg");
    gw.lootTables = LootTables::loadOrCreate("loot_tables.cfg");
    audio.applySettings(gw.settings);

    NetSession net;
    LobbyMenuState menu;
    menu.localIpStr = NetSession::detectLocalLanIp();
    menu.hostPortStr = std::to_string(cliPort);
    menu.joinPortStr = std::to_string(cliPort);
    menu.joinIpStr = cliJoinIp;
    menu.hostRole = cliHostRole;

    WorldMesher mesher;

    auto wireAudioHookForHost = [&]() {
        if (net.mode() == NetMode::Host) {
            audio.setEventHook([&](SoundId id, bool spatial, const Vec3& pos, float vol, float p1, float p2) {
                gw.recordSoundEvent(id, spatial, pos, vol, p1, p2);
            });
        } else {
            audio.setEventHook(nullptr);
        }
    };

    auto startSoloGame = [&]() {
        net.stop();
        wireAudioHookForHost();
        gw.startNewRun(worldSeed);
        mesher.syncWorld(gw.map.world);
        gw.worldStreamChanged = false;
        menu.visible = false;
        menu.activeMatch = true;
    };

    auto startHostGame = [&](uint16_t port, PlayerRole role, const std::string& guestCode) {
        if (!net.startHost(port, role, worldSeed)) {
            menu.statusMessage = net.statusText();
            return false;
        }
        if (!guestCode.empty()) {
            net.setPunchTarget(guestCode, port);
        }
        wireAudioHookForHost();
        gw.startOnlineMatch(worldSeed, role, 1);
        mesher.syncWorld(gw.map.world);
        gw.worldStreamChanged = false;
        menu.visible = false;
        menu.activeMatch = true;
        menu.statusMessage.clear();
        std::cout << "[NET] Hosting 1v1 on UDP port " << port
                  << " | Room Code: " << net.roomCode()
                  << " | " << net.upnpStatus()
                  << " | Local LAN IP: " << menu.localIpStr
                  << " | Your Role: " << (role == PlayerRole::Human ? "HUMAN" : "MONSTER") << "\n";
        return true;
    };

    auto startJoinGame = [&](const std::string& codeOrIp, uint16_t port) {
        if (!net.startClient(codeOrIp, port)) {
            menu.statusMessage = net.statusText();
            return false;
        }
        wireAudioHookForHost();
        menu.statusMessage = "PUNCHING / CONNECTING TO " + codeOrIp;
        menu.visible = true;
        std::cout << "[NET] Connecting to " << codeOrIp << "...\n";
        return true;
    };

    bool clientWorldSynced = false;
    uint32_t clientSyncedRoundId = 0;

    if (cliHost) {
        startHostGame(cliPort, cliHostRole, cliPunchCode);
    } else if (cliJoin) {
        startJoinGame(cliJoinIp, cliPort);
    } else if (cliSolo) {
        startSoloGame();
    } else {
        // Default: load background world and present the Host / Join / Solo Lobby Menu
        gw.startNewRun(worldSeed);
        mesher.syncWorld(gw.map.world);
        gw.worldStreamChanged = false;
        menu.visible = true;
        menu.activeMatch = false;
    }

    bool running = true;
    bool mouseCaptured = (!menu.visible && !visualTest);
    platform::setRelativeMouseMode(window, mouseCaptured);

    // Visual test state machine (--visual-test)
    struct VisualTestState {
        int phase = 0;
        int frame = 0;
        int curPin = 0;
        float flickDy = 0.0f;
        bool initialized = false;
    } vt;
    if (visualTest && !gw.doors.empty()) {
        gw.inventory.lockpicks = 3;
        gw.doors[0].locked = true;
        gw.doors[0].angle = 0.0f;
        Vec3 toDoor = gw.doors[0].hingePos - gw.player.eyePosition();
        gw.player.yaw = std::atan2(toDoor.x, -toDoor.z);
        gw.player.pitch = -0.05f;
        gw.lockpick.open(0, 777ULL, 0.0f, &gw.doors[0].hingePos);
        vt.initialized = true;
    }

    uint64_t prevTicks = platform::getTicksMs();
    float accumulator = 0.0f;
    float totalTime = 0.0f;

    int winW = cfg::WINDOW_WIDTH;
    int winH = cfg::WINDOW_HEIGHT;
    std::vector<BillboardInstance> spriteQueue;
    std::vector<RobotRenderState> robotQueue;
    std::vector<DoorRenderState> doorQueue;
    std::vector<ItemRenderState> itemQueue;

    auto reloadConfig = [&]() {
        gw.settings = GameSettings::loadOrCreate("mega.cfg");
        gw.lootTables = LootTables::loadOrCreate("loot_tables.cfg");
        audio.applySettings(gw.settings);
        for (auto& l : gw.map.pointLights) {
            l.color = {gw.settings.lampColorR, gw.settings.lampColorG, gw.settings.lampColorB};
            l.intensity = gw.settings.lampIntensity;
            l.radius = gw.settings.lampRadius;
        }
    };

    float mouseWinX = static_cast<float>(winW) * 0.5f;
    float mouseWinY = static_cast<float>(winH) * 0.5f;
    bool prevFreeCursor = false;
    bool rmbDownInPack = false;
    bool rmbRotatedPack = false;
    float rmbPressX = 0.0f;
    float rmbPressY = 0.0f;
    float rmbLastX = 0.0f;
    float rmbLastY = 0.0f;
    bool lmbDown = false;
    bool eDown = false;

    // Network replication state
    uint32_t serverTick = 1;
    uint32_t clientInputSeq = 1;
    uint32_t lastClientInputSeq = 0;
    uint32_t lastClientAckEventId = 0;
    NetInputFrame lastRemoteInput{};
    bool hasRemoteInput = false;

    struct QueuedClientEvents {
        uint16_t flags = 0;
        int32_t craftOptionIdx = -1;
        int8_t quickUseSlot = -1;
        float dragDx = 0.0f;
        float dragDy = 0.0f;
        Vec3 rayOrig{};
        Vec3 rayDir{0.0f, 0.0f, -1.0f};
        bool hasRay = false;
    } clientPending;

    auto computeCursorRay = [&](Vec3& outOrig, Vec3& outDir) {
        outOrig = gw.activeEyePosition();
        if (!(gw.onlineMode && gw.localRole == PlayerRole::Monster) &&
            (gw.backpackMode == BackpackMode::InHands || gw.draggingItem)) {
            float ndcX = (2.0f * mouseWinX / std::max(1, winW)) - 1.0f;
            float ndcY = 1.0f - (2.0f * mouseWinY / std::max(1, winH));
            float aspect = static_cast<float>(cfg::RETRO_WIDTH) / static_cast<float>(cfg::RETRO_HEIGHT);
            float tanHalf = std::tan(radians(cfg::FOV_DEGREES) * 0.5f);
            Vec3 fwd = gw.player.forwardDir();
            Vec3 right = gw.player.rightDir();
            Vec3 up = normalize(cross(right, fwd));
            outDir = normalize(fwd + right * (ndcX * aspect * tanHalf) + up * (ndcY * tanHalf));
        } else {
            outDir = gw.activeForwardDir();
        }
    };

    while (running) {
        uint64_t nowTicks = platform::getTicksMs();
        float frameDt = static_cast<float>(nowTicks - prevTicks) / 1000.0f;
        prevTicks = nowTicks;
        if (frameDt > 0.1f) frameDt = 0.1f;
        double nowSec = static_cast<double>(nowTicks) / 1000.0;

        // 1. Poll UDP network state (game packets + STUN + NAT punch)
        net.poll(nowSec);
        if (net.isBound()) {
            menu.myRoomCodeStr = net.roomCode();
            menu.natStatusStr = net.upnpStatus();
        }
        if (net.mode() == NetMode::Client) {
            if (!net.isConnected()) {
                if (menu.visible && menu.screen == 2) {
                    menu.statusMessage = net.statusText();
                }
            } else if (!clientWorldSynced || net.negotiatedRoundId() != clientSyncedRoundId) {
                gw.startOnlineMatch(net.negotiatedSeed(), net.localRole(), net.negotiatedRoundId());
                mesher.syncWorld(gw.map.world);
                gw.worldStreamChanged = false;
                clientWorldSynced = true;
                clientSyncedRoundId = net.negotiatedRoundId();
                menu.visible = false;
                menu.activeMatch = true;
                menu.statusMessage.clear();
                mouseCaptured = true;
                platform::setRelativeMouseMode(window, true);
                std::cout << "[NET] Synced Client match seed=" << net.negotiatedSeed()
                          << " round=" << clientSyncedRoundId
                          << " role=" << (gw.localRole == PlayerRole::Monster ? "MONSTER" : "HUMAN") << "\n";
            }
        }

        if (menu.visible && mouseCaptured) {
            mouseCaptured = false;
            platform::setRelativeMouseMode(window, false);
        }

        accumulator += frameDt * gw.godTimeScale;
        totalTime += frameDt * gw.godTimeScale;

        float yawDelta = 0.0f;
        float pitchDelta = 0.0f;

        // Local per-frame Monster one-shot triggers (for Solo/Host Monster)
        MonsterInput localMonsterOneShot;

        platform::Event ev{};
        while (platform::pollEvent(window, ev)) {
            if (ev.type == platform::EventType::Quit) {
                running = false;
            } else if (ev.type == platform::EventType::WindowResized) {
                winW = ev.width;
                winH = ev.height;
            } else if (ev.type == platform::EventType::FocusLost) {
                mouseCaptured = false;
                lmbDown = false;
                eDown = false;
                platform::setRelativeMouseMode(window, false);
            } else if (ev.type == platform::EventType::FocusGained) {
                prevTicks = platform::getTicksMs();
                accumulator = 0.0f;
            } else if (ev.type == platform::EventType::KeyUp) {
                if (ev.key == platform::Key::E) eDown = false;
            } else if (menu.visible) {
                // =============================================================
                // LOBBY MENU INPUT HANDLING
                // =============================================================
                bool typingText = (ev.type == platform::EventType::TextInput) ||
                                  (ev.type == platform::EventType::KeyDown && ev.textChar != 0);
                if (typingText) {
                    std::string* targetStr = nullptr;
                    size_t maxLen = 24;
                    bool allowCodeChars = false;
                    if (menu.screen == 1 && menu.cursor == 1) {
                        targetStr = &menu.hostPortStr;
                        maxLen = 5;
                    } else if (menu.screen == 1 && menu.cursor == 2) {
                        targetStr = &menu.hostGuestCodeStr;
                        maxLen = 24;
                        allowCodeChars = true;
                    } else if (menu.screen == 2 && menu.cursor == 0) {
                        targetStr = &menu.joinIpStr;
                        maxLen = 32;
                        allowCodeChars = true;
                    } else if (menu.screen == 2 && menu.cursor == 1) {
                        targetStr = &menu.joinPortStr;
                        maxLen = 5;
                    }
                    if (targetStr) {
                        char ch = ev.textChar;
                        if (allowCodeChars && ch >= 'a' && ch <= 'z') {
                            ch = static_cast<char>(ch - 'a' + 'A');
                        }
                        bool valid = (ch >= '0' && ch <= '9') ||
                                     (allowCodeChars && (ch == '.' || ch == ':' || ch == '-' || (ch >= 'A' && ch <= 'Z')));
                        if (valid && targetStr->size() < maxLen) {
                            targetStr->push_back(ch);
                        }
                    }
                }
                if (ev.type == platform::EventType::KeyDown) {
                    int maxItems = (menu.screen == 0) ? (menu.activeMatch ? 4 : 3) : (menu.screen == 1 ? 5 : 4);
                    bool inTextField = (menu.screen == 1 && (menu.cursor == 1 || menu.cursor == 2)) ||
                                       (menu.screen == 2 && (menu.cursor == 0 || menu.cursor == 1));
                    if (ev.key == platform::Key::Up || (!inTextField && ev.key == platform::Key::W)) {
                        menu.cursor = (menu.cursor + maxItems - 1) % maxItems;
                    } else if (ev.key == platform::Key::Down || (!inTextField && ev.key == platform::Key::S) || ev.key == platform::Key::Tab) {
                        menu.cursor = (menu.cursor + 1) % maxItems;
                    } else if (ev.key == platform::Key::Left || ev.key == platform::Key::Right) {
                        if (menu.screen == 1 && menu.cursor == 0) {
                            menu.hostRole = (menu.hostRole == PlayerRole::Human) ? PlayerRole::Monster : PlayerRole::Human;
                        }
                    } else if (ev.key == platform::Key::Backspace) {
                        std::string* targetStr = nullptr;
                        if (menu.screen == 1 && menu.cursor == 1) targetStr = &menu.hostPortStr;
                        else if (menu.screen == 1 && menu.cursor == 2) targetStr = &menu.hostGuestCodeStr;
                        else if (menu.screen == 2 && menu.cursor == 0) targetStr = &menu.joinIpStr;
                        else if (menu.screen == 2 && menu.cursor == 1) targetStr = &menu.joinPortStr;
                        if (targetStr && !targetStr->empty()) targetStr->pop_back();
                    } else if (ev.key == platform::Key::Escape || ev.key == platform::Key::F1) {
                        if (menu.screen != 0) {
                            menu.screen = 0;
                            menu.cursor = 0;
                        } else if (menu.activeMatch) {
                            menu.visible = false;
                            mouseCaptured = true;
                            platform::setRelativeMouseMode(window, true);
                        }
                    } else if (ev.key == platform::Key::Enter) {
                        if (menu.screen == 0) {
                            if (menu.cursor == 0) {
                                menu.screen = 1;
                                menu.cursor = 0;
                                menu.statusMessage.clear();
                                int hp = std::atoi(menu.hostPortStr.c_str());
                                uint16_t port = (hp > 0 && hp <= 65535) ? static_cast<uint16_t>(hp) : NetSession::DEFAULT_PORT;
                                net.prepareLobbySocket(port, true);
                                menu.myRoomCodeStr = net.roomCode();
                                menu.natStatusStr = net.upnpStatus();
                            } else if (menu.cursor == 1) {
                                menu.screen = 2;
                                menu.cursor = 0;
                                menu.statusMessage.clear();
                                net.prepareLobbySocket(0, false);
                                menu.myRoomCodeStr = net.roomCode();
                                menu.natStatusStr = net.upnpStatus();
                            } else if (menu.cursor == 2) {
                                startSoloGame();
                                mouseCaptured = true;
                                platform::setRelativeMouseMode(window, true);
                            } else if (menu.cursor == 3 && menu.activeMatch) {
                                menu.visible = false;
                                mouseCaptured = true;
                                platform::setRelativeMouseMode(window, true);
                            }
                        } else if (menu.screen == 1) {
                            if (menu.cursor == 0) {
                                menu.hostRole = (menu.hostRole == PlayerRole::Human) ? PlayerRole::Monster : PlayerRole::Human;
                            } else if (menu.cursor == 4) {
                                menu.screen = 0;
                                menu.cursor = 0;
                            } else {
                                int p = std::atoi(menu.hostPortStr.c_str());
                                uint16_t port = (p > 0 && p <= 65535) ? static_cast<uint16_t>(p) : NetSession::DEFAULT_PORT;
                                worldSeed ^= platform::getTicksMs();
                                if (startHostGame(port, menu.hostRole, menu.hostGuestCodeStr)) {
                                    mouseCaptured = true;
                                    platform::setRelativeMouseMode(window, true);
                                }
                            }
                        } else if (menu.screen == 2) {
                            if (menu.cursor == 3) {
                                menu.screen = 0;
                                menu.cursor = 0;
                            } else {
                                int p = std::atoi(menu.joinPortStr.c_str());
                                uint16_t port = (p > 0 && p <= 65535) ? static_cast<uint16_t>(p) : NetSession::DEFAULT_PORT;
                                clientWorldSynced = false;
                                startJoinGame(menu.joinIpStr, port);
                            }
                        }
                    }
                }
            } else if (ev.type == platform::EventType::KeyDown) {
                if (ev.key == platform::Key::F1) {
                    menu.visible = true;
                    menu.screen = 0;
                    menu.cursor = 0;
                    mouseCaptured = false;
                    platform::setRelativeMouseMode(window, false);
                    continue;
                }

                bool isMonsterRole = (gw.onlineMode && gw.localRole == PlayerRole::Monster);
                bool isClient = (net.mode() == NetMode::Client);

                if (ev.key == platform::Key::Escape) {
                    if (!isMonsterRole && gw.lockpick.active) {
                        if (isClient) clientPending.flags |= (1u << 14);
                        else gw.lockpick.close();
                    } else if (!isMonsterRole && gw.draggingItem) {
                        Vec3 ro, rd;
                        computeCursorRay(ro, rd);
                        if (isClient) {
                            clientPending.flags |= (1u << 14);
                            clientPending.rayOrig = ro;
                            clientPending.rayDir = rd;
                            clientPending.hasRay = true;
                        } else {
                            gw.handleSecondaryClick(ro, rd, audio);
                        }
                    } else if (!isMonsterRole && gw.heldPlaceMode) {
                        if (isClient) clientPending.flags |= (1u << 14);
                        else {
                            gw.heldPlaceMode = false;
                            gw.placementPreview = GhostPreviewState{};
                        }
                    } else if (!isMonsterRole && gw.backpackMode == BackpackMode::InHands) {
                        if (isClient) clientPending.flags |= (1u << 14);
                        else gw.toggleBackpackInHands();
                    } else {
                        mouseCaptured = !mouseCaptured;
                        platform::setRelativeMouseMode(window, mouseCaptured);
                    }
                } else if (ev.key == platform::Key::M) {
                    gw.showMinimap = !gw.showMinimap;
                } else if (ev.key == platform::Key::F5) {
                    reloadConfig();
                } else if (ev.key == platform::Key::Q && ev.ctrl) {
                    running = false;
                } else if (isMonsterRole) {
                    // =========================================================
                    // MONSTER CONTROLS (No backpack/gun/item keys)
                    // =========================================================
                    if (ev.key == platform::Key::N) {
                        if (isClient) clientPending.flags |= (1u << 3);
                        else localMonsterOneShot.nightVisionToggle = true;
                    } else if (ev.key == platform::Key::Space) {
                        if (isClient) clientPending.flags |= (1u << 2);
                        else localMonsterOneShot.dashPressed = true;
                    } else if (ev.key == platform::Key::E) {
                        eDown = true;
                        if (isClient) clientPending.flags |= (1u << 6);
                        else localMonsterOneShot.interactPressed = true;
                    } else if (ev.key == platform::Key::R && net.mode() == NetMode::Host && gw.gameOver) {
                        worldSeed += 1337ULL;
                        uint32_t nextRound = gw.roundId + 1;
                        gw.startOnlineMatch(worldSeed, net.hostRole(), nextRound);
                        mesher.syncWorld(gw.map.world);
                        gw.worldStreamChanged = false;
                        net.sendHandshakeAck(worldSeed, net.hostRole(), nextRound);
                    }
                } else {
                    // =========================================================
                    // HUMAN CONTROLS (No shooting / no weapon switching!)
                    // =========================================================
                    if (ev.key == platform::Key::Tab || ev.key == platform::Key::I) {
                        if (isClient) clientPending.flags |= (1u << 10);
                        else gw.toggleBackpackInHands();
                    } else if (ev.key == platform::Key::B) {
                        if (isClient) clientPending.flags |= (1u << 9);
                        else gw.togglePlaceBackpackOnGround(audio);
                    } else if (ev.key == platform::Key::T) {
                        if (isClient) clientPending.flags |= (1u << 11);
                        else gw.rotateActiveItem(0);
                    } else if (ev.key == platform::Key::Y) {
                        if (isClient) clientPending.flags |= (1u << 13);
                        else gw.rotateActiveItem(2);
                    } else if (ev.key == platform::Key::F) {
                        if (isClient) {
                            clientPending.flags |= (1u << 3);
                        } else if (gw.inventory.hasFlashlight) {
                            if (gw.inventory.flashlightBattery > 0.0f) {
                                gw.flashlightOn = !gw.flashlightOn;
                                audio.play3D(SoundId::FlashlightClick, gw.player.eyePosition(), 0.95f);
                            } else {
                                audio.play3D(SoundId::FlashlightClick, gw.player.eyePosition(), 0.45f);
                            }
                        }
                    } else if (ev.key == platform::Key::E && gw.backpackMode != BackpackMode::InHands) {
                        if (isClient) {
                            clientPending.flags |= (1u << 6);
                        } else {
                            if (gw.hasHeldItem && gw.hoveredDoorIdx < 0 && gw.hoveredItemIdx < 0) {
                                gw.useHeldItem(audio);
                            } else {
                                gw.beginInteract(audio);
                                gw.endInteract();
                            }
                        }
                    } else if (ev.key == platform::Key::Num1) {
                        if (isClient) {
                            if (!gw.availableCrafts.empty()) clientPending.craftOptionIdx = 0;
                            else clientPending.quickUseSlot = 0;
                        } else {
                            if (!gw.availableCrafts.empty()) gw.performCraft(0, audio);
                            else gw.useInventoryItem(ItemType::Battery, audio);
                        }
                    } else if (ev.key == platform::Key::Num2) {
                        if (isClient) {
                            if (gw.availableCrafts.size() >= 2) clientPending.craftOptionIdx = 1;
                            else clientPending.quickUseSlot = 1;
                        } else {
                            if (gw.availableCrafts.size() >= 2) gw.performCraft(1, audio);
                            else gw.useInventoryItem(ItemType::Food, audio);
                        }
                    } else if (ev.key == platform::Key::Num3) {
                        if (isClient) {
                            if (gw.availableCrafts.size() >= 3) clientPending.craftOptionIdx = 2;
                            else clientPending.quickUseSlot = 2;
                        } else {
                            if (gw.availableCrafts.size() >= 3) gw.performCraft(2, audio);
                            else gw.useInventoryItem(ItemType::Water, audio);
                        }
                    } else if (ev.key == platform::Key::G && !gw.onlineMode) {
                        gw.godMode = !gw.godMode;
                        if (!gw.godMode) {
                            gw.godNoclip = false;
                            gw.godInvisible = false;
                            gw.godSpeedMult = 1.0f;
                            gw.godTimeScale = 1.0f;
                        }
                    } else if (ev.key == platform::Key::N && !gw.onlineMode) {
                        gw.godNoclip = !gw.godNoclip;
                        if (gw.godNoclip) gw.godMode = true;
                    } else if (ev.key == platform::Key::V && !gw.onlineMode) {
                        gw.godInvisible = !gw.godInvisible;
                        if (gw.godInvisible) gw.godMode = true;
                    } else if (ev.key == platform::Key::RightBracket && !gw.onlineMode) {
                        gw.godMode = true;
                        gw.godSpeedMult = std::min(8.0f, gw.godSpeedMult * 2.0f);
                    } else if (ev.key == platform::Key::LeftBracket && !gw.onlineMode) {
                        gw.godSpeedMult = std::max(1.0f, gw.godSpeedMult * 0.5f);
                    } else if (ev.key == platform::Key::Period && !gw.onlineMode) {
                        gw.godMode = true;
                        gw.godTimeScale = std::min(8.0f, gw.godTimeScale * 2.0f);
                    } else if (ev.key == platform::Key::Comma && !gw.onlineMode) {
                        gw.godTimeScale = std::max(1.0f, gw.godTimeScale * 0.5f);
                    } else if (ev.key == platform::Key::R) {
                        if (gw.draggingItem || gw.hasHeldItem) {
                            if (isClient) clientPending.flags |= (1u << 12);
                            else gw.rotateActiveItem(1);
                        } else if (!gw.onlineMode) {
                            reloadConfig();
                            worldSeed += 1337ULL;
                            gw.startNewRun(worldSeed);
                            mesher.syncWorld(gw.map.world);
                            gw.worldStreamChanged = false;
                        } else if (net.mode() == NetMode::Host && gw.gameOver) {
                            worldSeed += 1337ULL;
                            uint32_t nextRound = gw.roundId + 1;
                            gw.startOnlineMatch(worldSeed, net.hostRole(), nextRound);
                            mesher.syncWorld(gw.map.world);
                            gw.worldStreamChanged = false;
                            net.sendHandshakeAck(worldSeed, net.hostRole(), nextRound);
                        }
                    }
                }
            } else if (ev.type == platform::EventType::MouseMove) {
                bool isMonsterRole = (gw.onlineMode && gw.localRole == PlayerRole::Monster);
                bool isClient = (net.mode() == NetMode::Client);
                bool freeCursor = !isMonsterRole && (gw.backpackMode == BackpackMode::InHands || gw.draggingItem);
                if (freeCursor) {
                    mouseWinX = ev.x;
                    mouseWinY = ev.y;
                    if (rmbDownInPack && gw.backpackMode == BackpackMode::InHands && !gw.draggingItem) {
                        float dx = mouseWinX - rmbLastX;
                        float dy = mouseWinY - rmbLastY;
                        rmbLastX = mouseWinX;
                        rmbLastY = mouseWinY;
                        if (std::hypot(mouseWinX - rmbPressX, mouseWinY - rmbPressY) > 5.0f) {
                            rmbRotatedPack = true;
                        }
                        if (rmbRotatedPack) {
                            if (isClient) {
                                clientPending.flags |= (1u << 15);
                                clientPending.dragDx += dx;
                                clientPending.dragDy += dy;
                            } else {
                                gw.rotateBackpackView(dx, dy);
                            }
                        }
                    }
                } else if (mouseCaptured) {
                    if (isMonsterRole) {
                        if (eDown) {
                            if (isClient) {
                                clientPending.dragDx += ev.xrel;
                                clientPending.dragDy += ev.yrel;
                            } else {
                                localMonsterOneShot.dragDx += ev.xrel;
                                localMonsterOneShot.dragDy += ev.yrel;
                            }
                        } else {
                            yawDelta += ev.xrel * cfg::MOUSE_SENS;
                            pitchDelta -= ev.yrel * cfg::MOUSE_SENS;
                        }
                    } else {
                        if (gw.lockpick.active) {
                            if (isClient) {
                                clientPending.dragDx += ev.xrel;
                                clientPending.dragDy += ev.yrel;
                            } else {
                                gw.lockpick.handleMouseMove(ev.xrel, ev.yrel);
                            }
                        } else if (lmbDown && (gw.grabbedDoorIdx >= 0 || isClient)) {
                            if (isClient) {
                                clientPending.dragDx += ev.xrel;
                                clientPending.dragDy += ev.yrel;
                            } else {
                                gw.dragGrabbedDoor(ev.xrel, ev.yrel);
                            }
                        } else {
                            yawDelta += ev.xrel * cfg::MOUSE_SENS;
                            pitchDelta -= ev.yrel * cfg::MOUSE_SENS;
                        }
                    }
                }
            } else if (ev.type == platform::EventType::MouseButtonUp) {
                bool isMonsterRole = (gw.onlineMode && gw.localRole == PlayerRole::Monster);
                bool isClient = (net.mode() == NetMode::Client);
                if (ev.button == platform::MouseButton::Left) {
                    lmbDown = false;
                    if (!isMonsterRole && !isClient) {
                        gw.endInteract();
                    }
                } else if (ev.button == platform::MouseButton::Right) {
                    if (!isMonsterRole && rmbDownInPack) {
                        bool wasSpin = rmbRotatedPack;
                        rmbDownInPack = false;
                        rmbRotatedPack = false;
                        if (!wasSpin && gw.backpackMode == BackpackMode::InHands) {
                            mouseWinX = ev.x;
                            mouseWinY = ev.y;
                            Vec3 ro, rd;
                            computeCursorRay(ro, rd);
                            if (isClient) {
                                clientPending.flags |= (1u << 5);
                                clientPending.rayOrig = ro;
                                clientPending.rayDir = rd;
                                clientPending.hasRay = true;
                            } else {
                                gw.handleSecondaryClick(ro, rd, audio);
                            }
                        }
                    }
                }
            } else if (ev.type == platform::EventType::MouseButtonDown) {
                bool isMonsterRole = (gw.onlineMode && gw.localRole == PlayerRole::Monster);
                bool isClient = (net.mode() == NetMode::Client);
                bool freeCursor = !isMonsterRole && (gw.backpackMode == BackpackMode::InHands || gw.draggingItem);
                if (freeCursor) {
                    mouseWinX = ev.x;
                    mouseWinY = ev.y;
                    Vec3 ro, rd;
                    computeCursorRay(ro, rd);
                    if (ev.button == platform::MouseButton::Left) {
                        if (isClient) {
                            clientPending.flags |= (1u << 4);
                            clientPending.rayOrig = ro;
                            clientPending.rayDir = rd;
                            clientPending.hasRay = true;
                        } else {
                            gw.handlePrimaryClick(ro, rd, audio);
                        }
                    } else if (ev.button == platform::MouseButton::Right) {
                        if (gw.backpackMode == BackpackMode::InHands && !gw.draggingItem) {
                            rmbDownInPack = true;
                            rmbRotatedPack = false;
                            rmbPressX = mouseWinX;
                            rmbPressY = mouseWinY;
                            rmbLastX = mouseWinX;
                            rmbLastY = mouseWinY;
                        } else {
                            if (isClient) {
                                clientPending.flags |= (1u << 5);
                                clientPending.rayOrig = ro;
                                clientPending.rayDir = rd;
                                clientPending.hasRay = true;
                            } else {
                                gw.handleSecondaryClick(ro, rd, audio);
                            }
                        }
                    }
                } else if (!mouseCaptured) {
                    mouseCaptured = true;
                    platform::setRelativeMouseMode(window, true);
                } else if (isMonsterRole) {
                    if (ev.button == platform::MouseButton::Left) {
                        if (isClient) clientPending.flags |= (1u << 4);
                        else localMonsterOneShot.attackPressed = true;
                    } else if (ev.button == platform::MouseButton::Right) {
                        if (isClient) clientPending.flags |= (1u << 2);
                        else localMonsterOneShot.dashPressed = true;
                    }
                } else {
                    Vec3 ro, rd;
                    computeCursorRay(ro, rd);
                    if (ev.button == platform::MouseButton::Left) {
                        lmbDown = true;
                        if (isClient) {
                            clientPending.flags |= (1u << 4);
                            clientPending.rayOrig = ro;
                            clientPending.rayDir = rd;
                            clientPending.hasRay = true;
                        } else {
                            // Strictly interaction + melee weapons ONLY (NO SHOOTING!)
                            gw.handlePrimaryClick(ro, rd, audio);
                        }
                    } else if (ev.button == platform::MouseButton::Right) {
                        if (isClient) {
                            clientPending.flags |= (1u << 5);
                            clientPending.rayOrig = ro;
                            clientPending.rayDir = rd;
                            clientPending.hasRay = true;
                        } else {
                            gw.handleSecondaryClick(ro, rd, audio);
                        }
                    }
                }
            }
        }

        // Automatically unlock mouse cursor when Human backpack is in hands or dragging item
        bool wantFreeCursor = !menu.visible &&
                              !(gw.onlineMode && gw.localRole == PlayerRole::Monster) &&
                              (gw.backpackMode == BackpackMode::InHands || gw.draggingItem);
        if (wantFreeCursor != prevFreeCursor) {
            prevFreeCursor = wantFreeCursor;
            platform::setRelativeMouseMode(window, !wantFreeCursor && mouseCaptured && !visualTest);
            if (wantFreeCursor) {
                mouseWinX = static_cast<float>(winW) * 0.5f;
                mouseWinY = static_cast<float>(winH) * 0.5f;
            }
        }

        auto isHeld = [&](platform::Key k) {
            return !menu.visible && platform::isKeyDown(window, k);
        };

        PlayerInput input;
        if (isHeld(platform::Key::W)) input.moveForward += 1.0f;
        if (isHeld(platform::Key::S)) input.moveForward -= 1.0f;
        if (isHeld(platform::Key::D)) input.moveRight += 1.0f;
        if (isHeld(platform::Key::A)) input.moveRight -= 1.0f;
        if (isHeld(platform::Key::Space)) input.moveUp += 1.0f;
        if (isHeld(platform::Key::LCtrl) || isHeld(platform::Key::C)) input.moveUp -= 1.0f;
        input.jump = isHeld(platform::Key::Space);
        input.sprint = isHeld(platform::Key::LShift);
        input.noclip = gw.godNoclip;
        input.yawDelta = yawDelta;
        input.pitchDelta = pitchDelta;

        int simSteps = 0;
        while (accumulator >= cfg::FIXED_DT && simSteps < 16) {
            simSteps++;
            if (visualTest && vt.initialized) {
                auto& lp = gw.lockpick;
                vt.frame++;
                if (vt.phase == 0) {
                    if (vt.frame >= 48) { vt.phase = 1; vt.frame = 0; }
                } else if (vt.phase == 1) {
                    if (vt.frame <= 35) lp.handleMouseMove(0.0f, -1.5f);
                    else if (vt.frame == 36) lp.handleMouseMove(0.0f, 35.0f);
                    if (vt.frame >= 85) { vt.phase = 2; vt.frame = 0; }
                } else if (vt.phase == 2) {
                    if (vt.frame == 1) lp.handleMouseMove(0.0f, -7.0f);
                    else if (vt.frame == 3) lp.handleMouseMove(0.0f, 20.0f);
                    if (vt.frame >= 65) { vt.phase = 3; vt.frame = 0; }
                } else if (vt.phase == 3) {
                    if (vt.frame == 1) lp.handleMouseMove(0.0f, -32.0f);
                    else if (vt.frame == 3) lp.handleMouseMove(0.0f, 35.0f);
                    if (vt.frame >= 100) { vt.phase = 4; vt.frame = 0; vt.curPin = 0; }
                } else if (vt.phase == 4) {
                    if (!lp.active) {
                        vt.phase = 5;
                        vt.frame = 0;
                    } else {
                        float targetX = static_cast<float>(vt.curPin) + 0.5f;
                        if (vt.frame == 1) {
                            lp.handleMouseMove(0.0f, 40.0f);
                            float dxNeeded = (targetX - lp.pickX) / 0.0050f;
                            lp.handleMouseMove(dxNeeded, 0.0f);
                            vt.flickDy = -(lp.pins[vt.curPin].target * 24.5f + 1.5f);
                        } else if (vt.frame == 5) {
                            lp.handleMouseMove(0.0f, -15.0f);
                        } else if (vt.frame == 8) {
                            lp.handleMouseMove(0.0f, vt.flickDy);
                        } else if (vt.frame == 10) {
                            lp.handleMouseMove(0.0f, 35.0f);
                        } else if (vt.frame >= 65) {
                            if (lp.pins[vt.curPin].pinned) vt.curPin++;
                            vt.frame = 0;
                        }
                    }
                } else if (vt.phase == 5) {
                    if (vt.frame >= 90) running = false;
                }
            }

            if (net.mode() == NetMode::Solo) {
                if (!menu.visible) {
                    gw.update(input, cfg::FIXED_DT, audio);
                }
            } else if (net.mode() == NetMode::Host) {
                std::vector<NetInputFrame> remoteBatch;
                net.popAllClientInputs(remoteBatch);
                for (const auto& rf : remoteBatch) {
                    if (rf.seq > lastClientInputSeq) lastClientInputSeq = rf.seq;
                    if (rf.lastAckEventId > lastClientAckEventId) lastClientAckEventId = rf.lastAckEventId;
                }

                if (gw.localRole == PlayerRole::Human) {
                    // Host is Human, Client is Monster
                    gw.update(input, cfg::FIXED_DT, audio);
                    if (!remoteBatch.empty()) {
                        for (const auto& rf : remoteBatch) {
                            gw.applyRemoteMonsterInput(rf, cfg::FIXED_DT, audio);
                            lastRemoteInput = rf;
                            hasRemoteInput = true;
                        }
                    } else if (hasRemoteInput) {
                        NetInputFrame held = lastRemoteInput;
                        held.flags &= static_cast<uint16_t>((1u << 0) | (1u << 7)); // Keep sprint & interactHeld only
                        held.dragDx = 0.0f;
                        held.dragDy = 0.0f;
                        gw.applyRemoteMonsterInput(held, cfg::FIXED_DT, audio);
                    }
                } else {
                    // Host is Monster, Client is Human
                    MonsterInput mIn = localMonsterOneShot;
                    mIn.moveForward = input.moveForward;
                    mIn.moveRight = input.moveRight;
                    mIn.yawDelta = input.yawDelta;
                    mIn.pitchDelta = input.pitchDelta;
                    mIn.sprint = input.sprint;
                    mIn.interactHeld = eDown;
                    gw.updateMonster(mIn, cfg::FIXED_DT, audio, true);
                    localMonsterOneShot = MonsterInput{};

                    if (!remoteBatch.empty()) {
                        for (const auto& rf : remoteBatch) {
                            gw.applyRemoteHumanInput(rf, cfg::FIXED_DT, audio);
                            lastRemoteInput = rf;
                            hasRemoteInput = true;
                        }
                    } else if (hasRemoteInput) {
                        NetInputFrame held = lastRemoteInput;
                        held.flags &= static_cast<uint16_t>((1u << 0) | (1u << 1) | (1u << 7));
                        held.craftOptionIdx = -1;
                        held.quickUseSlot = -1;
                        held.dragDx = 0.0f;
                        held.dragDy = 0.0f;
                        gw.applyRemoteHumanInput(held, cfg::FIXED_DT, audio);
                    } else {
                        PlayerInput idle{};
                        gw.update(idle, cfg::FIXED_DT, audio);
                    }
                }

                serverTick++;
                if (net.isConnected() && ((serverTick % 2 == 0) || !remoteBatch.empty() || gw.gameOver)) {
                    NetWorldSnapshot snap = gw.buildSnapshot(serverTick, lastClientInputSeq, lastClientAckEventId);
                    net.sendWorldSnapshot(snap);
                }
            } else if (net.mode() == NetMode::Client) {
                // Local camera responsiveness (instant look without roundtrip lag)
                if (gw.localRole == PlayerRole::Monster) {
                    gw.monster.yaw += input.yawDelta;
                    gw.monster.pitch = clampf(gw.monster.pitch + input.pitchDelta, -1.48f, 1.48f);
                } else if (!gw.isCameraPausedForInventory()) {
                    gw.player.yaw += input.yawDelta;
                    gw.player.pitch = clampf(gw.player.pitch + input.pitchDelta, -1.48f, 1.48f);
                }

                if (clientWorldSynced && net.isConnected()) {
                    NetInputFrame outF;
                    outF.seq = clientInputSeq++;
                    outF.lastAckEventId = gw.lastPlayedRemoteSoundId;
                    outF.role = static_cast<uint8_t>(gw.localRole);
                    outF.moveForward = input.moveForward;
                    outF.moveRight = input.moveRight;
                    outF.yaw = (gw.localRole == PlayerRole::Monster) ? gw.monster.yaw : gw.player.yaw;
                    outF.pitch = (gw.localRole == PlayerRole::Monster) ? gw.monster.pitch : gw.player.pitch;
                    outF.flags = clientPending.flags;
                    if (input.sprint) outF.flags |= (1u << 0);
                    if (input.jump && gw.localRole == PlayerRole::Human) outF.flags |= (1u << 1);
                    if ((gw.localRole == PlayerRole::Monster && eDown) ||
                        (gw.localRole == PlayerRole::Human && lmbDown)) {
                        outF.flags |= (1u << 7);
                    }
                    outF.dragDx = clientPending.dragDx;
                    outF.dragDy = clientPending.dragDy;
                    if (clientPending.hasRay) {
                        outF.rayOrig = clientPending.rayOrig;
                        outF.rayDir = clientPending.rayDir;
                    } else {
                        computeCursorRay(outF.rayOrig, outF.rayDir);
                    }
                    outF.craftOptionIdx = clientPending.craftOptionIdx;
                    outF.quickUseSlot = clientPending.quickUseSlot;
                    net.sendClientInput(outF);

                    clientPending = QueuedClientEvents{};
                }

                NetWorldSnapshot snap;
                if (net.popLatestSnapshot(snap)) {
                    gw.applySnapshot(snap, audio, true);
                }
                gw.updateDoorsAndEffectsOnly(cfg::FIXED_DT, audio);
            }

            input.yawDelta = 0.0f;
            input.pitchDelta = 0.0f;
            accumulator -= cfg::FIXED_DT;

            if (gw.worldStreamChanged) {
                mesher.syncWorld(gw.map.world);
                gw.worldStreamChanged = false;
            }
        }

        audio.update();

        fbo.bind();
        glClearColor(0.014f, 0.018f, 0.024f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        bool renderAsMonster = (gw.onlineMode && gw.localRole == PlayerRole::Monster);
        float aspect = static_cast<float>(cfg::RETRO_WIDTH) / static_cast<float>(cfg::RETRO_HEIGHT);
        Mat4 proj = Mat4::perspective(radians(cfg::FOV_DEGREES), aspect, cfg::NEAR_PLANE, cfg::FAR_PLANE);
        Mat4 view = gw.activeViewMatrix();
        Vec3 camPos = gw.activeEyePosition();
        Vec3 camDir = gw.activeForwardDir();

        worldShader.use();
        worldShader.setMat4("uModel", Mat4::identity());
        worldShader.setVec3("uPartScale", {0.0f, 0.0f, 0.0f});
        worldShader.setFloat("uTexLayerOverride", -1.0f);
        worldShader.setFloat("uEmissiveOverride", -1.0f);
        worldShader.setVec3("uTint", {1.0f, 1.0f, 1.0f});
        worldShader.setMat4("uProj", proj);
        worldShader.setMat4("uView", view);
        worldShader.setInt("uAtlas", 0);
        worldShader.setVec3("uCamPos", camPos);
        worldShader.setVec3("uCamDir", camDir);
        worldShader.setFloat("uFlashlight", (!renderAsMonster && gw.flashlightOn) ? 1.0f : 0.0f);

        worldShader.setFloat("uAmbientBrightness", gw.settings.ambientBrightness);
        worldShader.setFloat("uFlashlightIntensity", gw.settings.flashlightIntensity);
        worldShader.setFloat("uFlashlightRadius", gw.settings.flashlightRadius);
        worldShader.setVec3("uFlashlightColor", {gw.settings.flashlightColorR, gw.settings.flashlightColorG, gw.settings.flashlightColorB});
        worldShader.setFloat("uObjectEmissiveScale", gw.settings.objectEmissiveScale);
        worldShader.setFloat("uFogDensity", gw.settings.fogDensity);
        worldShader.setFloat("uLightTexelSnap", gw.settings.lightTexelSnap);
        worldShader.setFloat("uLightSteps", gw.settings.lightSteps);
        worldShader.setFloat("uLightDitherStrength", gw.settings.lightDitherStrength);

        auto sortedLights = gw.map.pointLights;
        gw.gatherRobotLights(sortedLights);
        if (renderAsMonster && gw.flashlightOn && gw.health > 0.0f) {
            Vec3 beamPos = gw.player.eyePosition() + gw.player.forwardDir() * 1.6f;
            sortedLights.push_back({beamPos,
                                    {gw.settings.flashlightColorR, gw.settings.flashlightColorG, gw.settings.flashlightColorB},
                                    gw.settings.flashlightRadius * 0.65f,
                                    gw.settings.flashlightIntensity * 1.1f});
        }
        std::sort(sortedLights.begin(), sortedLights.end(),
                  [&](const PointLight3D& a, const PointLight3D& b) {
                      return lengthSq(a.pos - camPos) < lengthSq(b.pos - camPos);
                  });

        int count = std::min(48, static_cast<int>(sortedLights.size()));
        worldShader.setInt("uLightCount", count);
        if (count > 0) {
            std::vector<float> posRad(static_cast<size_t>(count * 4));
            std::vector<float> colInt(static_cast<size_t>(count * 4));
            for (int i = 0; i < count; ++i) {
                posRad[i * 4 + 0] = sortedLights[i].pos.x;
                posRad[i * 4 + 1] = sortedLights[i].pos.y;
                posRad[i * 4 + 2] = sortedLights[i].pos.z;
                posRad[i * 4 + 3] = sortedLights[i].radius;

                colInt[i * 4 + 0] = sortedLights[i].color.x;
                colInt[i * 4 + 1] = sortedLights[i].color.y;
                colInt[i * 4 + 2] = sortedLights[i].color.z;
                colInt[i * 4 + 3] = sortedLights[i].intensity;
            }
            glUniform4fv(glGetUniformLocation(worldShader.id(), "uLightPosRadius"), count, posRad.data());
            glUniform4fv(glGetUniformLocation(worldShader.id(), "uLightColorIntensity"), count, colInt.data());
        }

        atlas.bind(0);
        mesher.render();

        if (!renderAsMonster) {
            gw.gatherRobots(robotQueue);
            robotRenderer.render(view, proj, camPos, robotQueue, gw.settings);

            Vec3 ro, rd;
            computeCursorRay(ro, rd);
            gw.updateCursorHover(ro, rd);
        }

        gw.gatherInteractables(doorQueue, itemQueue);
        BackpackRenderState bpState = gw.gatherBackpackState();
        robotRenderer.renderInteractables(worldShader, view, proj, camPos, doorQueue, itemQueue,
                                          bpState,
                                          renderAsMonster ? GhostPreviewState{} : gw.placementPreview,
                                          !renderAsMonster && gw.hasHeldItem,
                                          gw.heldItemType, gw.heldItemOrient,
                                          gw.heldItemWorldMatrix());

        if (renderAsMonster) {
            SurvivorRenderState surv;
            gw.gatherSurvivorState(surv);
            if (surv.visible) {
                robotRenderer.renderSurvivor(worldShader, surv);
            }
            if (gw.monster.hp > 0.0f) {
                robotRenderer.renderMonsterClaws(worldShader, camPos, camDir, gw.monster.rightDir(),
                                                 gw.monster.attackSwingTimer, gw.monster.dashTimer);
            }
        }

        gw.gatherBillboards(spriteQueue, totalTime);
        billboards.render(view, proj, camPos, spriteQueue);

        if (menu.visible) {
            hud.renderMenu(menu);
        } else {
            hud.render(gw, net.mode(), net.isConnected());
        }

        fbo.present(winW, winH, renderAsMonster && gw.monster.nightVision);
        platform::swapBuffers(window);
    }
    }

    platform::destroyWindow(window);
    return 0;
}
