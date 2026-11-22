#include "audio/AudioEngine.hpp"
#include <algorithm>
#include <cmath>

namespace mega {

namespace {

inline float nextRandSigned(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return (static_cast<float>(s & 0xFFFFu) / 32768.0f) - 1.0f;
}

inline bool segmentHitsAABB(const Vec3& a, const Vec3& b, const AABB& box, float& outThickness) {
    Vec3 d = b - a;
    float len = length(d);
    if (len < 1e-5f) return false;
    Vec3 dir = d / len;

    float tmin = 0.0f;
    float tmax = len;
    float o[3] = {a.x, a.y, a.z};
    float dv[3] = {dir.x, dir.y, dir.z};
    float mn[3] = {box.min.x, box.min.y, box.min.z};
    float mx[3] = {box.max.x, box.max.y, box.max.z};

    for (int i = 0; i < 3; ++i) {
        if (std::abs(dv[i]) < 1e-6f) {
            if (o[i] < mn[i] || o[i] > mx[i]) return false;
        } else {
            float inv = 1.0f / dv[i];
            float t1 = (mn[i] - o[i]) * inv;
            float t2 = (mx[i] - o[i]) * inv;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return false;
        }
    }
    outThickness = std::max(0.0f, tmax - tmin);
    return outThickness > 0.005f;
}

float durationForSound(SoundId id) {
    switch (id) {
        case SoundId::HitEnemy:        return 0.12f;
        case SoundId::PlayerHurt:      return 0.24f;
        case SoundId::Pickup:          return 0.12f;
        case SoundId::LevelDescend:    return 0.55f;
        case SoundId::RobotStep:       return 0.24f;
        case SoundId::RobotAlertPulse: return 0.28f;
        case SoundId::LockPinSet:      return 0.07f;
        case SoundId::LockBreak:       return 0.20f;
        case SoundId::LockOpen:        return 0.25f;
        case SoundId::LockpickSnap:    return 0.15f;
        case SoundId::PlayerStep:      return 0.12f;
        case SoundId::FlashlightClick: return 0.055f;
        case SoundId::DoorCreak:       return 0.22f;
        case SoundId::DoorImpact:      return 0.24f;
    }
    return 0.15f;
}

} // namespace

AudioEngine::~AudioEngine() {
    if (stream_) {
        SDL_PauseAudioStreamDevice(stream_);
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
    }
}

void SDLCALL AudioEngine::audioStreamCallback(void* userdata, SDL_AudioStream* stream,
                                              int additionalAmount, int /*totalAmount*/) {
    if (!userdata || !stream || additionalAmount <= 0) return;
    auto* self = static_cast<AudioEngine*>(userdata);

    int framesNeeded = additionalAmount / (2 * static_cast<int>(sizeof(float)));
    if (framesNeeded <= 0) return;
    framesNeeded = std::min(framesNeeded, 4096);

    std::vector<float> buf(static_cast<size_t>(framesNeeded * 2), 0.0f);
    self->renderSamples(buf.data(), framesNeeded);
    SDL_PutAudioStreamData(stream, buf.data(), framesNeeded * 2 * static_cast<int>(sizeof(float)));
}

bool AudioEngine::init() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        reverbL_.assign(REVERB_BUF_LEN, 0.0f);
        reverbR_.assign(REVERB_BUF_LEN, 0.0f);
        reverbWrite_ = 0;
    }

    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        return false;
    }

    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq = SAMPLE_RATE;

    // Asynchronous callback driven directly by SDL3's audio thread:
    // never underruns or stutters when main-thread FPS dips!
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                        &AudioEngine::audioStreamCallback, this);
    if (!stream_) return false;
    initialized_ = true;
    SDL_ResumeAudioStreamDevice(stream_);
    return true;
}

void AudioEngine::applySettings(const GameSettings& settings) {
    std::lock_guard<std::mutex> lock(mutex_);
    settings_ = settings;
}

float AudioEngine::soundVolumeScale(SoundId id) const {
    float base = settings_.masterVolume;
    switch (id) {
        case SoundId::PlayerStep:      return base * settings_.volPlayerStep;
        case SoundId::RobotStep:       return base * settings_.volRobotStep;
        case SoundId::RobotAlertPulse: return base * settings_.volRobotAlert;
        case SoundId::DoorCreak:       return base * settings_.volDoorCreak;
        case SoundId::DoorImpact:      return base * settings_.volDoorImpact;
        case SoundId::FlashlightClick: return base * settings_.volFlashlight;
        case SoundId::Pickup:          return base * settings_.volPickup;
        case SoundId::PlayerHurt:      return base * settings_.volPlayerHurt;
        case SoundId::LockPinSet:      return base * settings_.volLockPinSet;
        case SoundId::LockBreak:       return base * settings_.volLockBreak;
        case SoundId::LockOpen:        return base * settings_.volLockOpen;
        case SoundId::LockpickSnap:    return base * settings_.volLockpickSnap;
        case SoundId::HitEnemy:        return base * settings_.volWeapon;
        case SoundId::LevelDescend:    return base * 0.85f;
    }
    return base;
}

float AudioEngine::measureObstacleThickness(const Vec3& from, const Vec3& to) const {
    if (!world_) return 0.0f;

    auto segmentVoxelThickness = [&](const Vec3& a, const Vec3& b) -> float {
        Vec3 diff = b - a;
        float dist = length(diff);
        if (dist < 0.25f) return 0.0f;

        Vec3 dir = diff / dist;
        constexpr float STEP = 0.25f;
        float thick = 0.0f;

        for (float t = 0.24f; t < dist - 0.20f; t += STEP) {
            Vec3 p = a + dir * t;
            int ix = static_cast<int>(std::floor(p.x));
            int iy = static_cast<int>(std::floor(p.y));
            int iz = static_cast<int>(std::floor(p.z));
            Cell c = world_->getCell(ix, iy, iz);
            if (c.isOpaque()) {
                float density = (c.type == BlockType::Concrete || c.type == BlockType::DarkSteel ||
                                 c.type == BlockType::TechPanel)
                                    ? 1.0f
                                    : 0.65f;
                thick += STEP * density;
            }
        }

        for (const auto& doorBox : closedDoors_) {
            float dt = 0.0f;
            if (segmentHitsAABB(a, b, doorBox, dt)) {
                thick += std::max(0.28f, dt * 2.2f);
            }
        }
        return thick;
    };

    float directThick = segmentVoxelThickness(from, to);

    // If direct line-of-sight is blocked by diagonal concrete mass between perpendicular tunnels,
    // check L-shaped corridor diffraction paths via the two orthogonal 90-degree corners:
    // Corner A = (to.x, midY, from.z) and Corner B = (from.x, midY, to.z).
    if (directThick > 0.5f && std::abs(to.x - from.x) > 1.5f && std::abs(to.z - from.z) > 1.5f) {
        float midY = 0.5f * (from.y + to.y);
        Vec3 cornerA{to.x, midY, from.z};
        Vec3 cornerB{from.x, midY, to.z};

        // 90-degree tunnel corner bend adds ~1.35m equivalent diffraction obstacle + extra travel distance penalty
        float directDist = length(to - from);
        float lDist = std::abs(to.x - from.x) + std::abs(to.z - from.z);
        float detourPenalty = std::max(0.0f, lDist - directDist) * 0.06f;
        constexpr float CORNER_DIFFRACTION_EQUIV = 1.35f;

        float pathAThick = segmentVoxelThickness(from, cornerA) + segmentVoxelThickness(cornerA, to) +
                           CORNER_DIFFRACTION_EQUIV + detourPenalty;
        float pathBThick = segmentVoxelThickness(from, cornerB) + segmentVoxelThickness(cornerB, to) +
                           CORNER_DIFFRACTION_EQUIV + detourPenalty;

        directThick = std::min({directThick, pathAThick, pathBThick});
    }

    // Cap maximum effective wall thickness so multi-meter solid fill between rooms doesn't zero out sound
    return std::min(directThick, 8.0f);
}

void AudioEngine::measureRoomEcho(const Vec3& pos, float& outWet, float& outDelaySec) const {
    if (!world_) {
        outWet = 0.12f;
        outDelaySec = 0.06f;
        return;
    }

    static const Vec3 DIRS[9] = {
        { 1.0f, 0.0f,  0.0f}, {-1.0f, 0.0f,  0.0f},
        { 0.0f, 0.0f,  1.0f}, { 0.0f, 0.0f, -1.0f},
        { 0.707f, 0.0f,  0.707f}, {-0.707f, 0.0f,  0.707f},
        { 0.707f, 0.0f, -0.707f}, {-0.707f, 0.0f, -0.707f},
        { 0.0f, 1.0f, 0.0f}
    };

    constexpr float MAX_RAY = 22.0f;
    float hits[9];
    int solidHits = 0;

    for (int r = 0; r < 9; ++r) {
        float dist = MAX_RAY;
        for (float t = 0.45f; t < MAX_RAY; t += 0.65f) {
            Vec3 p = pos + DIRS[r] * t;
            int ix = static_cast<int>(std::floor(p.x));
            int iy = static_cast<int>(std::floor(p.y));
            int iz = static_cast<int>(std::floor(p.z));
            if (world_->getCell(ix, iy, iz).isOpaque()) {
                dist = t;
                solidHits++;
                break;
            }
        }
        hits[r] = dist;
    }

    float spanX = hits[0] + hits[1];
    float spanZ = hits[2] + hits[3];
    float longSpan = std::max(spanX, spanZ);
    float shortSpan = std::min(spanX, spanZ);
    float ceilH = hits[8];

    float avgDist = 0.0f;
    for (int i = 0; i < 8; ++i) avgDist += hits[i];
    avgDist /= 8.0f;

    float enclosedFactor = static_cast<float>(solidHits) / 9.0f;
    float corridorRatio = clampf((longSpan / std::max(1.5f, shortSpan) - 1.5f) * 0.25f, 0.0f, 1.0f);

    if (avgDist < 2.2f && longSpan < 5.0f) {
        outWet = 0.04f;
        outDelaySec = 0.025f;
    } else {
        float sizeFactor = clampf((avgDist - 2.2f) / 10.0f, 0.0f, 1.0f);
        float rawWet = (0.12f + sizeFactor * 0.36f + corridorRatio * 0.28f) * (0.45f + 0.55f * enclosedFactor);
        outWet = clampf(rawWet * settings_.echoStrength, 0.0f, 0.85f);
        float reflDist = std::clamp(longSpan * 0.65f + ceilH * 0.5f, 4.0f, 36.0f);
        outDelaySec = clampf((2.0f * reflDist) / 343.0f, 0.035f, 0.28f);
    }
}

void AudioEngine::setListener(const Vec3& pos, const Vec3& forward, const Vec3& right,
                              const WorldGrid* world, const std::vector<AABB>& closedDoors) {
    // Only re-probe room echo when listener moves >= 0.45m or world changes (eliminates per-frame raycast load)
    bool needProbe = (world != world_) || (lengthSq(pos - lastEchoProbePos_) >= 0.45f * 0.45f);
    float newWet = envEchoWet_;
    float newDelay = envEchoDelaySec_;

    // Keep only nearby closed doors (within soundMaxDist) for fast occlusion checks
    float maxDistSq = settings_.soundMaxDist * settings_.soundMaxDist;
    std::vector<AABB> nearDoors;
    nearDoors.reserve(16);
    for (const auto& b : closedDoors) {
        Vec3 c{(b.min.x + b.max.x) * 0.5f, (b.min.y + b.max.y) * 0.5f, (b.min.z + b.max.z) * 0.5f};
        if (lengthSq(c - pos) <= maxDistSq) {
            nearDoors.push_back(b);
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    listenerPos_ = pos;
    listenerFwd_ = lengthSq(forward) > 1e-6f ? normalize(forward) : Vec3{0.0f, 0.0f, -1.0f};
    listenerRight_ = lengthSq(right) > 1e-6f ? normalize(right) : Vec3{1.0f, 0.0f, 0.0f};
    world_ = world;
    closedDoors_ = std::move(nearDoors);
    if (needProbe) {
        lastEchoProbePos_ = pos;
        measureRoomEcho(listenerPos_, newWet, newDelay);
        envEchoWet_ = newWet;
        envEchoDelaySec_ = newDelay;
    }
}

AcousticParams AudioEngine::computeAcoustics(const Vec3& sourcePos) const {
    AcousticParams res;
    Vec3 toSrc = sourcePos - listenerPos_;
    float dist = length(toSrc);
    res.distance = dist;

    float maxD = std::max(4.0f, settings_.soundMaxDist);
    if (dist >= maxD) {
        res.gainL = 0.0f;
        res.gainR = 0.0f;
        return res;
    }

    // Configurable distance attenuation: audible up to ~80m in a straight line (default sound_max_dist=85)
    float dAtten = std::max(0.001f, settings_.soundDistanceAtten);
    float normD = dist / maxD;
    float edgeFade = clampf(1.0f - normD * normD * normD * normD, 0.0f, 1.0f);
    float distGain = edgeFade / (1.0f + dAtten * dist + (0.0003f * dAtten) * dist * dist);

    float pan = 0.0f;
    float rearFactor = 1.0f;
    if (dist > 0.08f) {
        Vec3 dir = toSrc / dist;
        pan = clampf(dot(dir, listenerRight_), -1.0f, 1.0f);
        float frontDot = dot(dir, listenerFwd_);
        if (frontDot < 0.0f) {
            rearFactor = 1.0f + frontDot * 0.16f;
        }
    }

    float thickness = measureObstacleThickness(sourcePos, listenerPos_);
    res.obstacleThickness = thickness;
    float obsAtten = settings_.soundObstacleAtten * settings_.occlusionStrength;
    float obsLpf   = settings_.soundObstacleLpf * settings_.occlusionStrength;
    float occGain = std::exp(-thickness * obsAtten);
    res.lpfCutoffHz = clampf(14500.0f * std::exp(-thickness * obsLpf), 320.0f, 14500.0f);

    // If occluded around a perpendicular tunnel beyond ~55m, fade smoothly so perp-tunnel range is ~50m
    float occludedRangeFade = 1.0f;
    if (thickness > 0.8f) {
        float perpLimit = maxD * 0.66f; // ~56m when maxD = 85m
        float perpRatio = dist / std::max(4.0f, perpLimit);
        occludedRangeFade = clampf(1.0f - std::pow(perpRatio, 3.5f), 0.0f, 1.0f);
    }

    float totalGain = distGain * occGain * occludedRangeFade * rearFactor;
    float angle = (pan + 1.0f) * 0.25f * PI;
    res.gainL = totalGain * std::cos(angle) * 1.4142f;
    res.gainR = totalGain * std::sin(angle) * 1.4142f;

    res.echoWet = clampf(envEchoWet_ * (0.35f + 0.65f * clampf(dist / 6.0f, 0.0f, 1.0f)) *
                         (0.45f + 0.55f * occGain),
                         0.0f, 0.85f);
    res.echoDelaySec = envEchoDelaySec_;
    return res;
}

void AudioEngine::addOrCoalesceVoiceLocked(const Voice& v) {
    // When a door slams/collides, immediately stop any ongoing DoorCreak so they don't clash
    if (v.id == SoundId::DoorImpact) {
        for (auto& ex : voices_) {
            if (ex.id == SoundId::DoorCreak) {
                ex.time = ex.duration;
            }
        }
    }

    // Coalesce/retrigger voices of the same category so overlapping sounds never phase-clash or pile up
    if (v.id == SoundId::DoorCreak || v.id == SoundId::PlayerStep ||
        v.id == SoundId::FlashlightClick || v.id == SoundId::LockPinSet ||
        v.id == SoundId::LockBreak || v.id == SoundId::LockpickSnap) {
        for (auto& ex : voices_) {
            if (ex.id == v.id && ex.time < ex.duration) {
                if (v.id == SoundId::DoorCreak) {
                    // Smoothly extend & modulate the active door creak in-place (keeps phase continuous!)
                    ex.volume = 0.5f * ex.volume + 0.5f * v.volume;
                    ex.param1 = 0.4f * ex.param1 + 0.6f * v.param1;
                    ex.param2 = v.param2;
                    ex.gainL = v.gainL;
                    ex.gainR = v.gainR;
                    ex.lpfAlpha = v.lpfAlpha;
                    ex.echoSend = v.echoSend;
                    if (ex.time > ex.duration * 0.35f) {
                        ex.time = ex.duration * 0.22f;
                    }
                    return;
                }
                // For transient sounds (PlayerStep, Lock clicks), replace the old tail cleanly
                ex = v;
                return;
            }
        }
    }

    // For 3D robot steps / alerts from the same approximate position, replace old tail if retriggered < 90ms
    if (v.id == SoundId::RobotStep || v.id == SoundId::RobotAlertPulse || v.id == SoundId::DoorImpact) {
        for (auto& ex : voices_) {
            if (ex.id == v.id && ex.time < ex.duration &&
                (!v.spatial || lengthSq(ex.worldPos - v.worldPos) < 1.5f * 1.5f)) {
                ex = v;
                return;
            }
        }
    }

    if (voices_.size() >= 24) {
        voices_.erase(voices_.begin());
    }
    voices_.push_back(v);
}

void AudioEngine::setEventHook(EventHook hook) {
    std::lock_guard<std::mutex> lock(mutex_);
    eventHook_ = std::move(hook);
}

void AudioEngine::play(SoundId id, float volume, float param1, float param2) {
    EventHook hookCopy;
    Vec3 lPos{};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        hookCopy = eventHook_;
        lPos = listenerPos_;
    }
    if (hookCopy) hookCopy(id, false, lPos, volume, param1, param2);

    std::lock_guard<std::mutex> lock(mutex_);
    float volScale = soundVolumeScale(id);
    float finalVol = volume * volScale;
    if (!initialized_ || finalVol <= 0.005f) return;

    Voice v;
    v.id = id;
    v.spatial = false;
    v.duration = durationForSound(id);
    v.volume = std::clamp(finalVol, 0.0f, 1.5f);
    v.param1 = param1;
    v.param2 = param2;
    v.gainL = 0.9f;
    v.gainR = 0.9f;
    v.lpfAlpha = 1.0f;
    v.echoSend = (id == SoundId::PlayerStep) ? envEchoWet_ * 0.28f : envEchoWet_ * 0.12f;
    globalRng_ ^= (globalRng_ << 13);
    globalRng_ ^= (globalRng_ >> 17);
    globalRng_ ^= (globalRng_ << 5);
    v.rng = globalRng_ | 1u;

    addOrCoalesceVoiceLocked(v);
}

void AudioEngine::play3D(SoundId id, const Vec3& worldPos, float volume, float param1, float param2) {
    EventHook hookCopy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        hookCopy = eventHook_;
    }
    if (hookCopy) hookCopy(id, true, worldPos, volume, param1, param2);

    std::lock_guard<std::mutex> lock(mutex_);
    float volScale = soundVolumeScale(id);
    float finalVol = volume * volScale;
    if (!initialized_ || finalVol <= 0.005f) return;

    AcousticParams ac = computeAcoustics(worldPos);
    if (ac.gainL + ac.gainR <= 0.008f) return;

    Voice v;
    v.id = id;
    v.spatial = true;
    v.worldPos = worldPos;
    v.duration = durationForSound(id);
    v.volume = std::clamp(finalVol, 0.0f, 1.5f);
    v.param1 = param1;
    v.param2 = param2;
    v.gainL = ac.gainL;
    v.gainR = ac.gainR;
    v.lpfAlpha = clampf(1.0f - std::exp(-2.0f * PI * ac.lpfCutoffHz / static_cast<float>(SAMPLE_RATE)),
                        0.02f, 1.0f);
    v.echoSend = ac.echoWet * 0.42f;
    globalRng_ ^= (globalRng_ << 13);
    globalRng_ ^= (globalRng_ >> 17);
    globalRng_ ^= (globalRng_ << 5);
    v.rng = globalRng_ | 1u;

    addOrCoalesceVoiceLocked(v);
}

void AudioEngine::renderSamples(float* outStereo, int frames) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (reverbL_.size() < REVERB_BUF_LEN) {
        reverbL_.assign(REVERB_BUF_LEN, 0.0f);
        reverbR_.assign(REVERB_BUF_LEN, 0.0f);
    }
    const float dt = 1.0f / static_cast<float>(SAMPLE_RATE);

    // Fixed mutually-prime reflection delays (29ms, 43ms, 61ms, 79ms) so room reverb never pitch-glitches
    // or produces discrete "machine-gun" slapback copies when walking!
    constexpr int TAP1 = 1277;
    constexpr int TAP2 = 1907;
    constexpr int TAP3 = 2693;
    constexpr int TAP4 = 3491;
    float feedback = clampf(0.18f + envEchoWet_ * 0.28f, 0.12f, 0.46f);
    float ambGain = settings_.masterVolume * settings_.ambientVolume;

    for (int i = 0; i < frames; ++i) {
        // Ultra-smooth 2-pole low-passed (<80 Hz) ventilation air rumble (zero high-freq hiss/buzz)
        float white = nextRandSigned(globalRng_);
        ambientLp1_ += (white - ambientLp1_) * 0.008f;
        ambientLp2_ += (ambientLp1_ - ambientLp2_) * 0.008f;
        ambientPhase_ += dt * 46.0f * 2.0f * PI;
        if (ambientPhase_ > 2.0f * PI) ambientPhase_ -= 2.0f * PI;
        float amb = (ambientLp2_ * 0.035f + std::sin(ambientPhase_) * 0.0045f) * ambGain;

        float dryL = amb;
        float dryR = amb;
        float sendL = 0.0f;
        float sendR = 0.0f;

        for (auto& v : voices_) {
            if (v.time >= v.duration) continue;
            float u = v.time / v.duration;
            float n = nextRandSigned(v.rng);
            float s = 0.0f;

            switch (v.id) {
                case SoundId::PlayerStep: {
                    float pitchJit = 0.92f + v.param1 * 0.18f;
                    float sprintBoost = 0.72f + v.param2 * 0.35f;
                    float heelEnv = std::exp(-v.time * 72.0f);
                    v.phase += dt * (82.0f * pitchJit + 38.0f * heelEnv) * 2.0f * PI;
                    float thud = std::sin(v.phase) * heelEnv * 0.28f;

                    float scuffEnv = (v.time > 0.012f) ? std::exp(-(v.time - 0.012f) * 38.0f) : (v.time / 0.012f);
                    v.bpState1 += (n - v.bpState1) * 0.16f;
                    v.bpState2 += (v.bpState1 - v.bpState2) * 0.05f;
                    float scuff = (v.bpState1 - v.bpState2) * scuffEnv * 0.34f;
                    s = (thud + scuff) * sprintBoost;
                    break;
                }
                case SoundId::RobotStep: {
                    float eSub = std::exp(-v.time * 30.0f);
                    float eMet = std::exp(-v.time * 18.0f);
                    float eHit = std::exp(-v.time * 95.0f);
                    v.phase  += dt * (60.0f + 45.0f * eHit) * 2.0f * PI;
                    v.phase2 += dt * 310.0f * 2.0f * PI;
                    v.phase3 += dt * 665.0f * 2.0f * PI;

                    float sub = std::sin(v.phase) * eSub * 0.36f;
                    float steel = (std::sin(v.phase2) * 0.55f + std::sin(v.phase3) * 0.35f) * eMet * 0.16f;
                    v.bpState1 += (n - v.bpState1) * 0.20f;
                    float clank = v.bpState1 * eHit * 0.42f;
                    s = sub + steel + clank;
                    break;
                }
                case SoundId::RobotAlertPulse: {
                    float attack = clampf(v.time / 0.018f, 0.0f, 1.0f);
                    float decay = std::exp(-std::max(0.0f, v.time - 0.03f) * 12.0f);
                    v.phase  += dt * 410.0f * 2.0f * PI;
                    v.phase2 += dt * 615.0f * 2.0f * PI;
                    float tone = std::sin(v.phase) * 0.65f + std::sin(v.phase2) * 0.35f;
                    s = tone * 0.20f * attack * decay;
                    break;
                }
                case SoundId::DoorCreak: {
                    float speedNorm = clampf(v.param1, 0.08f, 1.0f);
                    bool heavyDoor = v.param2 > 0.5f;
                    float stickFreq = (heavyDoor ? 24.0f : 38.0f) + speedNorm * (heavyDoor ? 40.0f : 58.0f);
                    v.phase += dt * stickFreq * 2.0f * PI;
                    float slipPulse = std::pow(0.5f + 0.5f * std::sin(v.phase), 5.0f);

                    float m1 = (heavyDoor ? 230.0f : 420.0f) * (0.93f + speedNorm * 0.20f);
                    float m2 = (heavyDoor ? 510.0f : 910.0f) * (0.95f + speedNorm * 0.16f);
                    v.phase2 += dt * m1 * 2.0f * PI;
                    v.phase3 += dt * m2 * 2.0f * PI;

                    float body = (std::sin(v.phase2) * 0.62f + std::sin(v.phase3) * 0.38f) * (0.25f + 0.75f * slipPulse);
                    v.bpState1 += (n - v.bpState1) * 0.12f;
                    float scrape = v.bpState1 * 0.11f;
                    float shape = std::sin(u * PI);
                    s = (body * 0.22f + scrape) * shape;
                    break;
                }
                case SoundId::DoorImpact: {
                    bool heavyDoor = v.param1 > 0.5f;
                    float wallHardness = clampf(v.param2, 0.2f, 1.0f);
                    float eThud = std::exp(-v.time * (heavyDoor ? 22.0f : 36.0f));
                    float eRing = std::exp(-v.time * (heavyDoor ? 16.0f : 24.0f));
                    float eHit  = std::exp(-v.time * 115.0f);

                    float f0 = heavyDoor ? 76.0f : 130.0f;
                    float f1 = heavyDoor ? 192.0f : 380.0f;
                    float f2 = heavyDoor ? 408.0f : 780.0f;
                    v.phase  += dt * (f0 + 35.0f * eHit) * 2.0f * PI;
                    v.phase2 += dt * f1 * 2.0f * PI;
                    v.phase3 += dt * f2 * 2.0f * PI;

                    float thud = std::sin(v.phase) * eThud * (0.34f + 0.15f * wallHardness);
                    float metal = (std::sin(v.phase2) * 0.5f + std::sin(v.phase3) * 0.32f) * eRing * 0.18f;
                    v.bpState1 += (n - v.bpState1) * (heavyDoor ? 0.15f : 0.28f);
                    float slam = v.bpState1 * eHit * 0.52f;
                    s = thud + metal + slam;
                    break;
                }
                case SoundId::FlashlightClick: {
                    float c1 = std::exp(-v.time * 240.0f);
                    float t2 = v.time - 0.015f;
                    float c2 = (t2 >= 0.0f) ? std::exp(-t2 * 210.0f) : 0.0f;
                    v.phase  += dt * 2350.0f * 2.0f * PI;
                    v.phase2 += dt * 1180.0f * 2.0f * PI;
                    v.bpState1 += (n - v.bpState1) * 0.55f;
                    float click1 = (std::sin(v.phase) * 0.35f + (n - v.bpState1) * 0.85f) * c1;
                    float click2 = (std::sin(v.phase2) * 0.45f + v.bpState1 * 0.65f) * c2;
                    s = (click1 * 0.35f + click2 * 0.42f);
                    break;
                }
                case SoundId::LockPinSet: {
                    float eClick = std::exp(-v.time * 135.0f);
                    float ePing  = std::exp(-v.time * 58.0f);
                    v.phase  += dt * 1950.0f * 2.0f * PI;
                    v.phase2 += dt * 3120.0f * 2.0f * PI;
                    float trans = n * eClick * 0.38f;
                    float brass = (std::sin(v.phase) * 0.55f + std::sin(v.phase2) * 0.30f) * ePing * 0.22f;
                    s = trans + brass;
                    break;
                }
                case SoundId::LockBreak: {
                    float eSlap = std::exp(-v.time * 68.0f);
                    float eCoil = std::exp(-v.time * 20.0f);
                    v.phase  += dt * (460.0f + 170.0f * eSlap) * 2.0f * PI;
                    v.phase2 += dt * 890.0f * 2.0f * PI;
                    v.bpState1 += (n - v.bpState1) * 0.35f;
                    float slap = v.bpState1 * eSlap * 0.42f;
                    float twang = (std::sin(v.phase) * 0.45f + std::sin(v.phase2) * 0.25f) * eCoil * 0.22f;
                    s = slap + twang;
                    break;
                }
                case SoundId::LockOpen: {
                    float e1 = std::exp(-v.time * 34.0f);
                    float t2 = v.time - 0.085f;
                    float e2 = (t2 >= 0.0f) ? std::exp(-t2 * 40.0f) : 0.0f;
                    v.phase  += dt * 285.0f * 2.0f * PI;
                    v.phase2 += dt * 710.0f * 2.0f * PI;
                    v.bpState1 += (n - v.bpState1) * 0.24f;
                    float slide = (v.bpState1 * 0.42f + std::sin(v.phase) * 0.28f) * e1;
                    float clunk = (std::sin(v.phase * 0.72f) * 0.50f + std::sin(v.phase2) * 0.30f + v.bpState1 * 0.45f) * e2;
                    s = slide * 0.35f + clunk * 0.52f;
                    break;
                }
                case SoundId::LockpickSnap: {
                    float eCrack = std::exp(-v.time * 185.0f);
                    float ePing  = std::exp(-v.time * 30.0f);
                    v.phase  += dt * 3450.0f * 2.0f * PI;
                    v.phase2 += dt * 5120.0f * 2.0f * PI;
                    v.bpState1 += (n - v.bpState1) * 0.6f;
                    float crack = (n - v.bpState1) * eCrack * 0.62f;
                    float wirePing = (std::sin(v.phase) * 0.5f + std::sin(v.phase2) * 0.35f) * ePing * 0.25f;
                    s = crack + wirePing;
                    break;
                }
                case SoundId::Pickup: {
                    float eRustle = std::sin(u * PI) * std::exp(-v.time * 20.0f);
                    float eTap = std::exp(-v.time * 95.0f);
                    v.bpState1 += (n - v.bpState1) * 0.28f;
                    v.bpState2 += (v.bpState1 - v.bpState2) * 0.06f;
                    v.phase += dt * 520.0f * 2.0f * PI;
                    s = ((v.bpState1 - v.bpState2) * eRustle * 0.45f + std::sin(v.phase) * eTap * 0.16f);
                    break;
                }
                case SoundId::PlayerHurt: {
                    float eBody = std::exp(-v.time * 20.0f);
                    v.phase += dt * (72.0f + 35.0f * eBody) * 2.0f * PI;
                    v.bpState1 += (n - v.bpState1) * 0.12f;
                    s = (std::sin(v.phase) * 0.42f + v.bpState1 * 0.32f) * eBody;
                    break;
                }
                case SoundId::HitEnemy: {
                    float eHit = std::exp(-v.time * 45.0f);
                    v.phase  += dt * 890.0f * 2.0f * PI;
                    v.phase2 += dt * 1480.0f * 2.0f * PI;
                    s = (n * std::exp(-v.time * 120.0f) * 0.38f +
                         (std::sin(v.phase) * 0.28f + std::sin(v.phase2) * 0.20f) * eHit);
                    break;
                }
                case SoundId::LevelDescend: {
                    float shape = std::sin(u * PI);
                    v.phase  += dt * (58.0f + u * 24.0f) * 2.0f * PI;
                    v.phase2 += dt * 116.0f * 2.0f * PI;
                    v.bpState1 += (n - v.bpState1) * 0.08f;
                    s = (std::sin(v.phase) * 0.30f + std::sin(v.phase2) * 0.15f + v.bpState1 * 0.22f) * shape;
                    break;
                }
            }

            // Per-voice wall-thickness occlusion low-pass filter
            v.lpfState += (s - v.lpfState) * v.lpfAlpha;
            float filtered = v.lpfState * v.volume;

            float vl = filtered * v.gainL;
            float vr = filtered * v.gainR;
            dryL += vl;
            dryR += vr;
            sendL += vl * v.echoSend;
            sendR += vr * v.echoSend;

            v.time += dt;
        }

        // Fixed prime-tap diffused stereo room reverb with 900Hz low-pass air absorption
        int r1 = (reverbWrite_ - TAP1) & (REVERB_BUF_LEN - 1);
        int r2 = (reverbWrite_ - TAP2) & (REVERB_BUF_LEN - 1);
        int r3 = (reverbWrite_ - TAP3) & (REVERB_BUF_LEN - 1);
        int r4 = (reverbWrite_ - TAP4) & (REVERB_BUF_LEN - 1);

        float tapL = reverbL_[r1] * 0.42f + reverbR_[r2] * 0.28f + reverbL_[r3] * 0.18f + reverbR_[r4] * 0.12f;
        float tapR = reverbR_[r1] * 0.42f + reverbL_[r2] * 0.28f + reverbR_[r3] * 0.18f + reverbL_[r4] * 0.12f;

        reverbLpfL_ += (tapL - reverbLpfL_) * 0.14f;
        reverbLpfR_ += (tapR - reverbLpfR_) * 0.14f;

        reverbL_[reverbWrite_] = clampf(sendL + reverbLpfL_ * feedback, -0.95f, 0.95f);
        reverbR_[reverbWrite_] = clampf(sendR + reverbLpfR_ * feedback, -0.95f, 0.95f);
        reverbWrite_ = (reverbWrite_ + 1) & (REVERB_BUF_LEN - 1);

        float mixL = dryL + reverbLpfL_ * 0.45f;
        float mixR = dryR + reverbLpfR_ * 0.45f;

        // High-pass DC blocker (20 Hz) so low sub impacts never accumulate DC offset
        float outL = mixL - dcBlockerInL_ + 0.997f * dcBlockerOutL_;
        dcBlockerInL_ = mixL;
        dcBlockerOutL_ = outL;

        float outR = mixR - dcBlockerInR_ + 0.997f * dcBlockerOutR_;
        dcBlockerInR_ = mixR;
        dcBlockerOutR_ = outR;

        // Clean linear headroom up to +-0.85, gentle soft knee above +-0.85
        auto softLimit = [](float x) {
            if (x > 0.85f) return 0.85f + 0.15f * std::tanh((x - 0.85f) / 0.15f);
            if (x < -0.85f) return -0.85f + 0.15f * std::tanh((x + 0.85f) / 0.15f);
            return x;
        };

        outStereo[i * 2 + 0] = softLimit(outL);
        outStereo[i * 2 + 1] = softLimit(outR);
    }

    voices_.erase(
        std::remove_if(voices_.begin(), voices_.end(),
                       [](const Voice& v) { return v.time >= v.duration; }),
        voices_.end());
}

void AudioEngine::update() {
    // Synthesis now runs asynchronously on SDL3's dedicated audio callback thread.
}

} // namespace mega
