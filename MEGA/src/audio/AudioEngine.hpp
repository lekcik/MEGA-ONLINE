#pragma once

#include "core/Config.hpp"
#include "core/Math3D.hpp"
#include "world/WorldGrid.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace mega {

enum class SoundId : uint8_t {
    HitEnemy = 0,
    PlayerHurt,
    Pickup,
    LevelDescend,
    RobotStep,
    RobotAlertPulse,
    LockPinSet,
    LockBreak,
    LockOpen,
    LockpickSnap,
    PlayerStep,
    FlashlightClick,
    DoorCreak,
    DoorImpact
};

struct AcousticParams {
    float distance = 0.0f;
    float gainL = 1.0f;
    float gainR = 1.0f;
    float obstacleThickness = 0.0f; // Effective meters of solid material between source and listener
    float lpfCutoffHz = 14000.0f;   // Low-pass cutoff from wall thickness occlusion
    float echoWet = 0.0f;           // 0..1 conditional echo send based on surrounding architecture
    float echoDelaySec = 0.08f;     // Primary reflection delay (seconds)
};

class AudioEngine {
public:
    AudioEngine() = default;
    ~AudioEngine();

    bool init();
    void applySettings(const GameSettings& settings);

    // Update 3D listener pose and acoustic world snapshot each simulation tick
    void setListener(const Vec3& pos, const Vec3& forward, const Vec3& right,
                     const WorldGrid* world, const std::vector<AABB>& closedDoors);

    using EventHook = std::function<void(SoundId, bool spatial, const Vec3&, float, float, float)>;
    void setEventHook(EventHook hook);

    // Play non-spatial (head/UI) or 3D world-positioned sound
    void play(SoundId id, float volume = 1.0f, float param1 = 0.5f, float param2 = 0.5f);
    void play3D(SoundId id, const Vec3& worldPos, float volume = 1.0f,
                float param1 = 0.5f, float param2 = 0.5f);

    // Pure acoustic solver (also callable in unit tests without audio device)
    AcousticParams computeAcoustics(const Vec3& sourcePos) const;
    float measureObstacleThickness(const Vec3& from, const Vec3& to) const;
    void measureRoomEcho(const Vec3& pos, float& outWet, float& outDelaySec) const;
    float soundVolumeScale(SoundId id) const;

    // Synthesize stereo frames (called automatically by SDL3 audio thread callback)
    void renderSamples(float* outStereo, int frames);
    void update();

private:
    struct Voice {
        SoundId id = SoundId::Pickup;
        bool spatial = false;
        Vec3 worldPos{};
        float phase = 0.0f;
        float phase2 = 0.0f;
        float phase3 = 0.0f;
        float time = 0.0f;
        float duration = 0.2f;
        float volume = 1.0f;
        float param1 = 0.5f;   // Pitch / speed / material modifier
        float param2 = 0.5f;   // Intensity / surface hardness modifier
        float gainL = 1.0f;
        float gainR = 1.0f;
        float lpfAlpha = 1.0f; // 1-pole occlusion LPF coefficient
        float lpfState = 0.0f;
        float bpState1 = 0.0f; // Internal synthesis filter state
        float bpState2 = 0.0f;
        float echoSend = 0.0f;
        uint32_t rng = 0x9E3779B9u;
    };

    static constexpr int SAMPLE_RATE = 44100;
    static constexpr int REVERB_BUF_LEN = 16384; // Fixed prime-tap diffused reverb buffer

    static void SDLCALL audioStreamCallback(void* userdata, SDL_AudioStream* stream,
                                            int additionalAmount, int totalAmount);

    void addOrCoalesceVoiceLocked(const Voice& v);

    mutable std::mutex mutex_;
    SDL_AudioStream* stream_ = nullptr;
    std::vector<Voice> voices_;
    GameSettings settings_{};

    // 3D Listener & Cached Acoustic Environment
    Vec3 listenerPos_{0.0f, 1.56f, 0.0f};
    Vec3 listenerFwd_{0.0f, 0.0f, -1.0f};
    Vec3 listenerRight_{1.0f, 0.0f, 0.0f};
    Vec3 lastEchoProbePos_{-9999.0f, -9999.0f, -9999.0f};
    const WorldGrid* world_ = nullptr;
    std::vector<AABB> closedDoors_;
    float envEchoWet_ = 0.16f;
    float envEchoDelaySec_ = 0.08f;

    // Diffused low-pass damped stereo reverb tank (no zipper noise or slapback doubling)
    std::vector<float> reverbL_;
    std::vector<float> reverbR_;
    int reverbWrite_ = 0;
    float reverbLpfL_ = 0.0f;
    float reverbLpfR_ = 0.0f;
    float dcBlockerInL_ = 0.0f;
    float dcBlockerOutL_ = 0.0f;
    float dcBlockerInR_ = 0.0f;
    float dcBlockerOutR_ = 0.0f;

    // Ultra-subtle low-frequency room air tone (no harsh brown-noise buzz)
    float ambientPhase_ = 0.0f;
    float ambientLp1_ = 0.0f;
    float ambientLp2_ = 0.0f;
    uint32_t globalRng_ = 0x51A7C31Du;
    bool initialized_ = false;
    EventHook eventHook_;
};

} // namespace mega
