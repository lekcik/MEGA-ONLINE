#include "game/LockpickMinigame.hpp"
#include "core/Math3D.hpp"
#include "core/Rng.hpp"
#include <algorithm>
#include <cmath>

namespace mega {

void LockpickMinigame::open(int doorIdx, uint64_t lockSeed, float pickBreakChance,
                            const Vec3* worldPos) {
    active = true;
    targetDoorIdx = doorIdx;
    breakChance = clampf(pickBreakChance, 0.0f, 1.0f);
    unlockedJustNow = false;
    pickSnappedJustNow = false;
    contactCount = 0;
    mouseTargetX = pickX = 0.5f;
    mouseTargetY = pickY = -0.05f;
    pickVelX = pickVelY = 0.0f;
    accumFrameDy = 0.0f;
    if (worldPos) {
        lockWorldPos = *worldPos;
        hasWorldPos = true;
    } else {
        lockWorldPos = {};
        hasWorldPos = false;
    }

    rngState = (lockSeed ^ 0x9E3779B97F4A7C15ULL) | 1ULL;
    Rng rng(lockSeed ^ 0x4C4F434BULL);
    for (int i = 0; i < 3; ++i) {
        pins[i] = LockPin{};
        pins[i].target = 0.44f + rng.nextFloat() * 0.38f; // [0.44 .. 0.82]
        pins[i].kinkDir = rng.chance(0.5f) ? 1.0f : -1.0f;
    }
}

void LockpickMinigame::close() {
    active = false;
    targetDoorIdx = -1;
}

void LockpickMinigame::handleMouseMove(float dx, float dy) {
    if (!active) return;

    float newX = clampf(pickX + dx * 0.0050f, 0.14f, 2.86f);
    float oldY = pickY;
    float rawDeltaY = -dy * 0.0055f;
    float newY = clampf(pickY + rawDeltaY, -0.08f, 0.20f);

    constexpr float CHAMBER_HALF_W = 0.19f;
    constexpr float KEYWAY_CEIL_Y  = 0.02f;

    auto chamberForX = [&](float x) -> int {
        for (int i = 0; i < 3; ++i) {
            if (std::abs(x - (static_cast<float>(i) + 0.5f)) <= CHAMBER_HALF_W) return i;
        }
        return -1;
    };

    if (oldY > KEYWAY_CEIL_Y) {
        int curCh = std::clamp(static_cast<int>(std::floor(pickX)), 0, 2);
        float cCenter = static_cast<float>(curCh) + 0.5f;
        newX = clampf(newX, cCenter - CHAMBER_HALF_W, cCenter + CHAMBER_HALF_W);
    } else if (newY > KEYWAY_CEIL_Y) {
        int targetCh = chamberForX(newX);
        if (targetCh < 0) {
            newY = KEYWAY_CEIL_Y;
        } else {
            float cCenter = static_cast<float>(targetCh) + 0.5f;
            newX = clampf(newX, cCenter - CHAMBER_HALF_W, cCenter + CHAMBER_HALF_W);
        }
    }

    int ch = chamberForX(newX);
    if (ch >= 0 && newY > KEYWAY_CEIL_Y && pins[ch].pinned && newY > pins[ch].pos) {
        newY = pins[ch].pos;
    }

    // Direct 1:1 mouse binding (zero smoothing)
    pickX = mouseTargetX = newX;
    pickY = mouseTargetY = newY;

    // Accumulate raw mouse dy across all polling events in this frame (125Hz..1000Hz polling-rate independent)
    accumFrameDy += dy;
}

void LockpickMinigame::update(float dt, AudioEngine& audio) {
    if (!active) return;

    auto emitLockSound = [&](SoundId id, float vol) {
        if (hasWorldPos) audio.play3D(id, lockWorldPos, vol);
        else             audio.play(id, vol);
    };

    auto nextRoll = [&]() -> float {
        rngState ^= rngState >> 12;
        rngState ^= rngState << 25;
        rngState ^= rngState >> 27;
        uint64_t r = rngState * 0x2545F4914F6CDD1DULL;
        return static_cast<float>((r >> 40) & 0xFFFFFFu) / static_cast<float>(0x1000000u);
    };

    // Convert accumulated frame mouse movement to physical speed (px/s -> pickVelY)
    float speedPxPerSec = -accumFrameDy / std::max(dt, 0.001f);
    accumFrameDy = 0.0f;
    float instVel = clampf(speedPxPerSec * 0.0028f, -12.0f, 12.0f);
    if (std::abs(instVel) > 0.05f) {
        pickVelY = instVel;
    }

    // Pure Newtonian 240Hz spring-mass physics:
    // No artificial "light vs hard" thresholds — outcome emerges purely from kinetic energy vs spring potential!
    const int substeps = 4;
    const float subDt = dt / static_cast<float>(substeps);

    constexpr float SPRING_K    = 9.0f;
    constexpr float PRELOAD     = 0.05f;
    constexpr float AIR_DAMPING = 0.75f;
    constexpr float GRAVITY     = 1.15f;
    constexpr float TARGET_TOL  = 0.105f;

    for (int step = 0; step < substeps; ++step) {
        int activeChamber = std::clamp(static_cast<int>(std::floor(pickX)), 0, 2);

        for (int i = 0; i < 3; ++i) {
            LockPin& pin = pins[i];
            if (pin.brokenTimer > 0.0f) {
                pin.brokenTimer = std::max(0.0f, pin.brokenTimer - subDt);
            }

            // Living multi-coil spring wave harmonics
            for (int c = 0; c < 4; ++c) {
                float waveAcc = -280.0f * pin.coilWave[c] - 9.0f * pin.coilWaveVel[c];
                pin.coilWaveVel[c] += waveAcc * subDt;
                pin.coilWave[c] += pin.coilWaveVel[c] * subDt;
            }

            if (pin.pinned) {
                pin.wasTouchingPick = false;
                continue;
            }

            float chamberCenter = static_cast<float>(i) + 0.5f;
            bool underPin = (i == activeChamber) && (std::abs(pickX - chamberCenter) < 0.24f);
            bool touchingNow = underPin && (pickY >= pin.pos - 0.015f);

            // Detect each distinct physical contact/knock with the pin (5% pick break chance on regular locks, 0% on exit door)
            if (touchingNow && !pin.wasTouchingPick) {
                pin.wasTouchingPick = true;
                contactCount++;
                if (breakChance > 0.0f && nextRoll() < breakChance) {
                    pickSnappedJustNow = true;
                    pin.vel = -1.2f;
                    emitLockSound(SoundId::LockpickSnap, 1.0f);
                    return; // Stop processing this tick so GameWorld deducts the broken lockpick
                }
            } else if (!underPin || pickY < pin.pos - 0.028f) {
                pin.wasTouchingPick = false;
            }

            // Pure rigid-pick to pin contact: non-penetration + direct momentum transfer
            if (touchingNow) {
                if (pin.pos < pickY) {
                    pin.pos = pickY;
                }
                if (pickVelY > pin.vel) {
                    float dv = pickVelY - pin.vel;
                    pin.vel = pickVelY;
                    if (dv > 0.25f) {
                        for (int c = 0; c < 4; ++c) {
                            pin.coilWaveVel[c] += dv * (0.38f - c * 0.06f);
                        }
                    }
                }
            }

            // Integrate Hooke's law spring + gravity + air damping
            float prevVel = pin.vel;
            float acc = -SPRING_K * (pin.pos + PRELOAD) - GRAVITY - AIR_DAMPING * pin.vel;
            pin.vel += acc * subDt;
            pin.pos += pin.vel * subDt;

            // Physical ceiling impact: if kinetic energy carried the pin all the way to the top (0.98),
            // it slams the chamber roof, shatters/deforms the spring visual, and rebounds back down!
            if (pin.pos >= 0.98f) {
                pin.pos = 0.98f;
                pin.vel = -std::max(2.4f, std::abs(pin.vel) * 0.72f);
                pin.brokenTimer = 1.25f;
                for (int c = 0; c < 4; ++c) {
                    pin.coilWaveVel[c] += (c % 2 == 0 ? 1.0f : -1.0f) * 3.5f;
                }
                emitLockSound(SoundId::LockBreak, 0.9f);
            }

            // Shear-line catch ("підтягування"): when the pin is near target height AND moving slowly
            // (naturally at its kinetic apex — impossible to reach by slow pushing since pickY <= 0.20!)
            if (pin.brokenTimer <= 0.0f &&
                std::abs(pin.pos - pin.target) <= TARGET_TOL &&
                std::abs(pin.vel) <= 1.25f) {
                pin.pos += (pin.target - pin.pos) * std::min(1.0f, 22.0f * subDt);
                pin.vel *= 0.72f;
                if (std::abs(pin.pos - pin.target) <= 0.025f || (prevVel > 0.0f && pin.vel <= 0.0f)) {
                    pin.pinned = true;
                    pin.pos = pin.target;
                    pin.vel = 0.0f;
                    for (int c = 0; c < 4; ++c) pin.coilWaveVel[c] += 1.0f;
                    emitLockSound(SoundId::LockPinSet, 0.9f);
                }
            }

            // Bottom stop with metallic bounce
            if (pin.pos <= 0.0f) {
                pin.pos = 0.0f;
                if (pin.vel < -0.55f) {
                    for (int c = 0; c < 4; ++c) {
                        pin.coilWaveVel[c] += pin.vel * 0.25f;
                    }
                    pin.vel = -pin.vel * 0.28f;
                } else {
                    pin.vel = 0.0f;
                }
            }
        }

        // Decay pick velocity between mouse events
        pickVelY *= std::exp(-24.0f * subDt);
    }

    if (pins[0].pinned && pins[1].pinned && pins[2].pinned) {
        unlockedJustNow = true;
        emitLockSound(SoundId::LockOpen, 1.0f);
        active = false;
    }
}

} // namespace mega
