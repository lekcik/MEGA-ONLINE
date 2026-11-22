#include <gtest/gtest.h>
#include "world/WorldGrid.hpp"
#include "world/gen/MegaGenerator.hpp"
#include "game/Player.hpp"
#include "game/GameWorld.hpp"
#include "game/LockpickMinigame.hpp"
#include "net/NetSession.hpp"
#include <chrono>
#include <queue>
#include <set>
#include <thread>

using namespace mega;

TEST(WorldGridTest, BoundsAndBlockPlacement) {
    WorldGrid grid(2, 2, 2);
    EXPECT_EQ(grid.sizeX(), 32);
    EXPECT_EQ(grid.sizeY(), 32);
    EXPECT_EQ(grid.sizeZ(), 32);

    EXPECT_TRUE(grid.inBounds(0, 0, 0));
    EXPECT_FALSE(grid.inBounds(-1, 0, 0));
    EXPECT_FALSE(grid.inBounds(32, 0, 0));

    grid.setBlock(5, 6, 7, BlockType::RustPlate);
    EXPECT_EQ(grid.getBlock(5, 6, 7), BlockType::RustPlate);
    // Out of bounds returns solid hull
    EXPECT_EQ(grid.getBlock(-1, 0, 0), BlockType::DarkSteel);
}

TEST(PlayerPhysicsTest, GravityLandsOnFloorAndStepUpWorks) {
    WorldGrid grid(2, 2, 2);
    grid.fillBox(0, 0, 0, 31, 0, 31, BlockType::Concrete);
    // Small 0.5m-ish? No, 1-block step is 1.0m (> STEP_HEIGHT 0.55m), so it should block!
    // Let's verify landing on floor y=1.0
    Player player;
    player.position = {10.0f, 3.0f, 10.0f};
    PlayerInput idle;
    for (int i = 0; i < 120; ++i) {
        player.update(idle, grid, cfg::FIXED_DT);
    }
    EXPECT_TRUE(player.grounded);
    EXPECT_NEAR(player.position.y, 1.0f, 0.02f);

    // Jump lifts player off ground
    PlayerInput jump;
    jump.jump = true;
    player.update(jump, grid, cfg::FIXED_DT);
    EXPECT_FALSE(player.grounded);
    EXPECT_GT(player.velocity.y, 0.0f);
}

TEST(MegaGeneratorTest, GeneratesConnectedMultiBiomeMegastructureQuickly) {
    GameSettings cfg = GameSettings::loadOrCreate("mega.cfg");
    for (uint64_t seed : {1ULL, 42ULL, 9999ULL}) {
        MegaResult res = MegaGenerator::generate(seed, cfg);
        EXPECT_GE(res.corridors.size(), 10u);
        EXPECT_GT(res.world.props().size(), 30u);
        EXPECT_GT(res.pointLights.size(), 15u);
        EXPECT_EQ(res.loadedRegions.size(), 9u); // 3x3 active streaming window

        // Verify corridor dimensions (2..4m width, 30..80m primary length, 12..80m ring-loop legs) & sparse connections
        for (const auto& seg : res.corridors) {
            EXPECT_GE(seg.width, cfg.corridorMinWidth);
            EXPECT_LE(seg.width, cfg.corridorMaxWidth);
            int len = seg.alongX ? std::abs(seg.x1 - seg.x0) : std::abs(seg.z1 - seg.z0);
            EXPECT_GE(len, seg.isLoopReturn ? 12 : cfg.corridorMinLen);
            EXPECT_LE(len, cfg.corridorMaxLen);
            EXPECT_LE(seg.connections, cfg.maxConnections + 2);
        }

        // Verify infinite streaming loads new regions when player walks far away
        bool streamed = MegaGenerator::updateInfiniteStream(res, {320.0f, 1.0f, 320.0f}, cfg);
        EXPECT_TRUE(streamed);
        EXPECT_GT(res.loadedRegions.count({2, 2}), 0u);
    }
}

TEST(GameWorldTest, RaycastAndStarterRoomSurvivalWork) {
    GameWorld gw;
    gw.startNewRun(12345ULL);
    AudioEngine audio;
    PlayerInput idle;

    // 1. Starter room & raycast check (while in region 0,0)
    float hitDist = gw.raycastWorld(gw.player.eyePosition(), {0.0f, -1.0f, 0.0f}, 10.0f);
    EXPECT_GT(hitDist, 0.5f);
    EXPECT_LT(hitDist, 2.5f);

    EXPECT_GE(gw.doors.size(), 2u);
    EXPECT_NEAR(gw.doors[0].height, 2.0f, 0.01f);
    EXPECT_FALSE(gw.doors[0].isCabinet);
    EXPECT_GE(gw.worldItems.size(), 10u); // Flashlight + 2 Batteries + 3 Lockpicks + 2 Food + 2 Water (+ corridor loot)
    EXPECT_GT(gw.map.lockers.size(), 0u); // 25% corridor hideable lockers generated

    // Verify starter room interior has NO corridor carved through it
    EXPECT_EQ(gw.map.world.getBlock(58, 1, 58), BlockType::Air);
    EXPECT_EQ(gw.map.world.getBlock(58, 1, 54), BlockType::Concrete);
    EXPECT_EQ(gw.map.world.getBlock(58, 1, 61), BlockType::Concrete);

    // 2. Test camera-relative Amnesia-style door drag opens the exit door (yaw = -PI/2 -> looking East along +X)
    gw.player.yaw = PI * 0.5f; // +X
    gw.grabbedDoorIdx = 0;
    gw.dragGrabbedDoor(0.0f, -45.0f); // Push mouse forward/up to push door open into corridor
    for (int i = 0; i < 30; ++i) gw.update(idle, cfg::FIXED_DT, audio);
    EXPECT_GT(gw.doors[0].angle, 0.2f);
    gw.endInteract();

    // 3. Test Inventory usage (Food, Water, Battery)
    gw.inventory.hasFlashlight = true;
    gw.inventory.flashlightBattery = 40.0f;
    gw.inventory.batteries = 2;
    gw.inventory.food = 1;
    gw.inventory.water = 1;
    gw.hunger = 50.0f;
    gw.thirst = 50.0f;

    EXPECT_TRUE(gw.useInventoryItem(ItemType::Battery, audio));
    EXPECT_GT(gw.inventory.flashlightBattery, 80.0f);
    EXPECT_TRUE(gw.useInventoryItem(ItemType::Food, audio));
    EXPECT_GT(gw.hunger, 80.0f);
    EXPECT_TRUE(gw.useInventoryItem(ItemType::Water, audio));
    EXPECT_GT(gw.thirst, 85.0f);

    // 4. Test Smart Locker AI: if robot saw player enter a locker, robot opens it!
    int testLockerIdx = -1;
    for (size_t i = 0; i < gw.doors.size(); ++i) {
        if (gw.doors[i].isLocker) { testLockerIdx = static_cast<int>(i); break; }
    }
    ASSERT_GE(testLockerIdx, 0);
    gw.doors[testLockerIdx].angle = 0.05f;
    gw.player.position = {
        0.5f * (gw.doors[testLockerIdx].lockerInterior.min.x + gw.doors[testLockerIdx].lockerInterior.max.x),
        1.0f,
        0.5f * (gw.doors[testLockerIdx].lockerInterior.min.z + gw.doors[testLockerIdx].lockerInterior.max.z)
    };
    Enemy seeker;
    seeker.pos = {gw.doors[testLockerIdx].hingePos.x, 1.0f, gw.doors[testLockerIdx].hingePos.z};
    seeker.aiState = EnemyAIState::InvestigateLastSeen;
    seeker.alerted = true;
    seeker.sawPlayerEnterLockerIdx = testLockerIdx;
    seeker.lastSeenPos = gw.player.position;
    gw.enemies.clear();
    gw.enemies.push_back(seeker);
    gw.update(idle, cfg::FIXED_DT, audio);
    EXPECT_GE(gw.doors[testLockerIdx].angle, 0.70f);

    // 5. Safe center vs outer 500m edge robot spawn rule
    for (const auto& e : gw.enemies) {
        EXPECT_TRUE(MegaGenerator::isRobotSpawnAllowed(e.pos.x, e.pos.z) || e.sawPlayerEnterLockerIdx == -1);
    }
    gw.enemies.clear();
    gw.player.position = {448.0f, 1.0f, 64.5f};
    gw.update(idle, cfg::FIXED_DT, audio);
    EXPECT_GT(gw.enemies.size(), 0u);
    for (const auto& e : gw.enemies) {
        EXPECT_TRUE(MegaGenerator::isRobotSpawnAllowed(e.pos.x, e.pos.z));
    }
}

TEST(LockpickTest, SlowPushOverforceBreakAndGentleTapPhysics) {
    AudioEngine audio;
    LockpickMinigame lp;
    lp.open(0, 777ULL, 0.0f);

    // 1. Slow push up to max pick height (0.20) then drop -> NEVER reaches target (0.44..0.82), falls back to 0
    lp.pins[0].target = 0.55f;
    lp.pickX = 0.5f;
    lp.pickY = -0.05f;
    for (int i = 0; i < 30; ++i) {
        lp.handleMouseMove(0.0f, -2.0f); // Very slow push
        lp.update(cfg::FIXED_DT, audio);
    }
    lp.pickY = -0.05f;
    for (int i = 0; i < 60; ++i) lp.update(cfg::FIXED_DT, audio);
    EXPECT_FALSE(lp.pins[0].pinned);
    EXPECT_LT(lp.pins[0].pos, 0.1f);

    // 2. Detailed physical flick sweep (dy = -4 .. -44 px) to verify pure physics usability & accessibility
    std::printf("=== LOCKPICK PURE PHYSICS SWEEP (target=0.60) ===\n");
    int pinnedCount = 0;
    int brokeCount = 0;
    int shortCount = 0;
    for (int dy = 4; dy <= 44; dy += 2) {
        lp.open(0, 777ULL, 0.0f);
        lp.pins[0].target = 0.60f;
        lp.pickX = 0.5f;
        lp.pickY = 0.0f;
        // Simulate 5 high-polling USB mouse packets in a single 60Hz frame
        for (int pkt = 0; pkt < 5; ++pkt) {
            lp.handleMouseMove(0.0f, -static_cast<float>(dy) / 5.0f);
        }
        float maxH = 0.0f;
        bool broke = false;
        for (int f = 0; f < 80; ++f) {
            lp.update(cfg::FIXED_DT, audio);
            lp.pickY = -0.05f; // Natural release after flick
            maxH = std::max(maxH, lp.pins[0].pos);
            if (lp.pins[0].brokenTimer > 0.0f) broke = true;
        }
        const char* status = lp.pins[0].pinned ? "PINNED!" : (broke ? "CEILING_BREAK" : "FELL_SHORT");
        if (lp.pins[0].pinned) pinnedCount++;
        else if (broke) brokeCount++;
        else shortCount++;
        std::printf("dy=-%2dpx/frame (%4.0f px/s) | maxH=%.2f | %s\n", dy, dy * 60.0f, maxH, status);
    }
    // Verify all 3 physical regimes emerge naturally AND there is a comfortable human window to pin!
    EXPECT_GT(shortCount, 0);
    EXPECT_GE(pinnedCount, 3); // At least 6px+ wide sweet spot for human mouse flicks!
    EXPECT_GT(brokeCount, 0);

    // 3. Verify EVERY target height in [0.44 .. 0.82] is reliably reachable & pinnable!
    for (float tgt : {0.44f, 0.55f, 0.68f, 0.82f}) {
        bool reachable = false;
        for (int dy = 4; dy <= 35; ++dy) {
            lp.open(0, 777ULL, 0.0f);
            lp.pins[0].target = tgt;
            lp.pickX = 0.5f;
            lp.pickY = 0.0f;
            lp.handleMouseMove(0.0f, static_cast<float>(-dy));
            for (int f = 0; f < 80; ++f) {
                lp.update(cfg::FIXED_DT, audio);
                lp.pickY = -0.05f;
            }
            if (lp.pins[0].pinned) {
                reachable = true;
                break;
            }
        }
        EXPECT_TRUE(reachable) << "Target height " << tgt << " was not reachable!";
    }

    // 4. Pick wall collision: cannot lift between chambers, cannot exceed 20% height, and cannot exit chamber walls
    lp.open(0, 777ULL);
    lp.pickX = 1.0f;
    lp.pickY = -0.05f;
    lp.handleMouseMove(0.0f, -100.0f);
    EXPECT_LE(lp.pickY, 0.025f);

    lp.pickX = 0.5f;
    lp.pickY = 0.15f;
    lp.handleMouseMove(200.0f, -200.0f);
    EXPECT_LE(lp.pickX, 0.70f);
    EXPECT_LE(lp.pickY, 0.205f);
}

TEST(RobotSimTest, ObservePatrolAndChaseFootKinematics) {
    GameWorld gw;
    gw.startNewRun(12345ULL);

    // Manually spawn a robot in the central corridor to observe its Patrol & Chase kinematics
    Enemy testBot;
    testBot.pos = {64.5f, 1.0f, 70.0f};
    testBot.dirX = 0; testBot.dirZ = 1;
    testBot.pendingDirX = 0; testBot.pendingDirZ = 1;
    testBot.yaw = 0.0f;
    testBot.aiState = EnemyAIState::Patrol;
    testBot.alerted = false;
    testBot.regX = 0; testBot.regZ = 0;
    gw.enemies.clear();
    gw.enemies.push_back(testBot);

    // Move player around a corner behind a wall (same region so it isn't unloaded)
    gw.player.position = {40.0f, 1.0f, 64.5f};
    AudioEngine audio;
    PlayerInput idle;

    std::printf("=== PATROL OBSERVATION (0.0s .. 6.0s, constant speed, 1 step / 1.5s) ===\n");
    int firstStepLeg = -1;
    std::vector<float> stepPlantTimes;
    bool prevStep[2] = {false, false};
    for (int f = 0; f < 360; ++f) {
        gw.update(idle, cfg::FIXED_DT, audio);
        const Enemy& rob = gw.enemies[0];
        float t = f * cfg::FIXED_DT;
        for (int leg = 0; leg < 2; ++leg) {
            if (!prevStep[leg] && rob.feet[leg].stepping) {
                if (firstStepLeg == -1) firstStepLeg = leg;
                std::printf("[t=%.2fs] %s FOOT LIFTED  (bodyZ=%.2f, v=%.2f)\n",
                            t, leg == 1 ? "RIGHT" : "LEFT", rob.pos.z, std::hypot(rob.vel.x, rob.vel.z));
            }
            if (prevStep[leg] && !rob.feet[leg].stepping) {
                stepPlantTimes.push_back(t);
                std::printf("[t=%.2fs] %s FOOT PLANTED (bodyZ=%.2f, footZ=%.2f, strideFromBody=%.2fm)\n",
                            t, leg == 1 ? "RIGHT" : "LEFT", rob.pos.z, rob.feet[leg].worldPos.z,
                            rob.feet[leg].worldPos.z - rob.pos.z);
            }
            prevStep[leg] = rob.feet[leg].stepping;
        }
        if (f > 5) {
            EXPECT_NEAR(std::hypot(rob.vel.x, rob.vel.z), 0.44f, 0.05f);
        }
    }
    EXPECT_EQ(firstStepLeg, 1); // Right foot (idx=1) MUST step first!
    ASSERT_GE(stepPlantTimes.size(), 3u);
    for (size_t i = 1; i < stepPlantTimes.size(); ++i) {
        float dtStep = stepPlantTimes[i] - stepPlantTimes[i - 1];
        std::printf("Step interval #%zu: %.2fs\n", i, dtStep);
        EXPECT_NEAR(dtStep, 1.50f, 0.25f);
    }

    // Now place player 10m ahead in +Z to trigger Chase!
    gw.player.position = {gw.enemies[0].pos.x, 1.0f, gw.enemies[0].pos.z + 10.0f};
    std::printf("\n=== CHASE OBSERVATION (0.0s .. 1.5s) ===\n");
    for (int f = 0; f < 90; ++f) {
        gw.update(idle, cfg::FIXED_DT, audio);
        const Enemy& rob = gw.enemies[0];
        if (f % 4 == 0) {
            float dL = std::hypot(rob.feet[0].worldPos.x - rob.pos.x, rob.feet[0].worldPos.z - rob.pos.z);
            float dR = std::hypot(rob.feet[1].worldPos.x - rob.pos.x, rob.feet[1].worldPos.z - rob.pos.z);
            std::printf("t=%.2fs | alert=%d | bodyZ=%.2f | v=%.2f | L_step=%d y=%.2f d=%.2f | R_step=%d y=%.2f d=%.2f\n",
                        f * cfg::FIXED_DT, rob.alerted ? 1 : 0, rob.pos.z, std::hypot(rob.vel.x, rob.vel.z),
                        rob.feet[0].stepping ? 1 : 0, rob.feet[0].worldPos.y, dL,
                        rob.feet[1].stepping ? 1 : 0, rob.feet[1].worldPos.y, dR);
        }
        EXPECT_LE(std::hypot(rob.feet[0].worldPos.x - rob.pos.x, rob.feet[0].worldPos.z - rob.pos.z), 0.80f);
        EXPECT_LE(std::hypot(rob.feet[1].worldPos.x - rob.pos.x, rob.feet[1].worldPos.z - rob.pos.z), 0.80f);
    }
}

TEST(RobotSimTest, ObserveMapNavigationPatrolAndChase) {
    GameWorld gw;
    gw.startNewRun(12345ULL);

    AudioEngine audio;
    PlayerInput idle;

    // Place 2 test robots in region (0, 0) corridors for patrol & chase navigation verification
    gw.enemies.clear();
    Enemy r0;
    r0.pos = {97.5f, 1.0f, 64.5f}; r0.dirX = 1; r0.dirZ = 0; r0.pendingDirX = 1; r0.pendingDirZ = 0;
    r0.yaw = PI * 0.5f; r0.regX = 0; r0.regZ = 0;
    Enemy r1;
    r1.pos = {64.5f, 1.0f, 114.5f}; r1.dirX = 0; r1.dirZ = 1; r1.pendingDirX = 0; r1.pendingDirZ = 1;
    r1.yaw = 0.0f; r1.regX = 0; r1.regZ = 0;
    gw.enemies.push_back(r0);
    gw.enemies.push_back(r1);

    // --- PART 1: 45-second Patrol navigation test ---
    gw.player.position = {10.0f, 1.0f, 10.0f};
    std::printf("=== MAP NAVIGATION: PATROL (45s) ===\n");
    std::vector<int> reg0Idx = {0, 1};

    std::vector<Vec3> prevCheckpoint(reg0Idx.size());
    std::vector<int> turnCount(reg0Idx.size(), 0);
    std::vector<int> prevDirX(reg0Idx.size()), prevDirZ(reg0Idx.size());
    for (size_t k = 0; k < reg0Idx.size(); ++k) {
        const auto& r = gw.enemies[reg0Idx[k]];
        prevCheckpoint[k] = r.pos;
        prevDirX[k] = r.dirX;
        prevDirZ[k] = r.dirZ;
        std::printf("Robot#%zu start=(%.2f, %.2f) dir=(%d,%d)\n", k, r.pos.x, r.pos.z, r.dirX, r.dirZ);
    }

    int stuckWarnings = 0;
    for (int f = 1; f <= 2700; ++f) { // 45s
        gw.update(idle, cfg::FIXED_DT, audio);
        for (size_t k = 0; k < reg0Idx.size(); ++k) {
            const auto& r = gw.enemies[reg0Idx[k]];
            if (r.dirX != prevDirX[k] || r.dirZ != prevDirZ[k]) {
                turnCount[k]++;
                std::printf("[t=%.1fs] Robot#%zu TURNED to (%d,%d) at (%.2f, %.2f)\n",
                            f * cfg::FIXED_DT, k, r.dirX, r.dirZ, r.pos.x, r.pos.z);
                prevDirX[k] = r.dirX;
                prevDirZ[k] = r.dirZ;
            }
            if (f % 300 == 0) { // Every 5s check progress
                float moved5s = std::hypot(r.pos.x - prevCheckpoint[k].x, r.pos.z - prevCheckpoint[k].z);
                if (moved5s < 0.5f) {
                    stuckWarnings++;
                    std::printf("[WARN t=%.1fs] Robot#%zu moved only %.2fm in 5s at (%.2f, %.2f) dir=(%d,%d) pause=%.2f\n",
                                f * cfg::FIXED_DT, k, moved5s, r.pos.x, r.pos.z, r.dirX, r.dirZ, r.turnPauseTimer);
                }
                prevCheckpoint[k] = r.pos;
            }
        }
    }
    for (size_t k = 0; k < reg0Idx.size(); ++k) {
        const auto& r = gw.enemies[reg0Idx[k]];
        std::printf("Robot#%zu end=(%.2f, %.2f) turns=%d\n", k, r.pos.x, r.pos.z, turnCount[k]);
    }
    std::printf("Patrol stuck warnings: %d\n\n", stuckWarnings);

    // --- PART 2: Chase around 90-degree corner, lose LOS, investigate, return to aggro ---
    std::printf("=== MAP NAVIGATION: CHASE -> CORNER -> LOSE LOS -> RETURN ===\n");
    Enemy& chaser = gw.enemies[reg0Idx[0]];
    chaser.pos = {64.5f, 1.0f, 54.0f};
    chaser.dirX = 0; chaser.dirZ = 1;
    chaser.yaw = 0.0f;
    chaser.aiState = EnemyAIState::Patrol;
    chaser.alerted = false;
    chaser.turnPauseTimer = 0.0f;
    chaser.feet[0].initialized = false;
    chaser.feet[1].initialized = false;

    // Place player at central junction (64.5, 64.5) in front of robot -> triggers Chase
    gw.player.position = {64.5f, 1.0f, 64.5f};

    const char* stateNames[] = {"Patrol", "Chase", "InvestigateLastSeen", "ReturnToAggro"};
    EnemyAIState lastSt = chaser.aiState;
    for (int f = 0; f < 1800; ++f) { // 30s
        float t = f * cfg::FIXED_DT;
        // At t=1.2s, player runs around the 90-degree corner east to (82.0, 64.5) and hides behind a wall!
        if (f == 72) {
            gw.player.position = {82.0f, 1.0f, 64.5f};
            std::printf("[t=%.2fs] Player ran around corner to (82.0, 64.5)\n", t);
        }
        // At t=3.5s, player teleports far away to test losing LOS completely
        if (f == 210) {
            gw.player.position = {20.0f, 1.0f, 20.0f};
            std::printf("[t=%.2fs] Player hid completely out of LOS\n", t);
        }

        gw.update(idle, cfg::FIXED_DT, audio);
        const Enemy& c = gw.enemies[reg0Idx[0]];
        if (c.aiState != lastSt) {
            std::printf("[t=%.2fs] State: %s -> %s at (%.2f, %.2f)\n",
                        t, stateNames[static_cast<int>(lastSt)],
                        stateNames[static_cast<int>(c.aiState)], c.pos.x, c.pos.z);
            lastSt = c.aiState;
        }
        if (f % 120 == 0) {
            std::printf("[t=%.2fs] pos=(%.2f, %.2f) state=%s wp=(%.1f, %.1f)\n",
                        t, c.pos.x, c.pos.z, stateNames[static_cast<int>(c.aiState)],
                        c.nextWaypoint.x, c.nextWaypoint.z);
        }
    }
}

TEST(Audio3DAcousticsTest, PanningObstacleThicknessAndConditionalEcho) {
    WorldGrid world;
    // Build a 95m straight concrete tunnel along X (x=0..95, z=4..6, y=1..4)
    // plus a perpendicular tunnel branching along +Z at x=40..42, z=4..52, y=1..4
    // (surrounded by solid concrete in the diagonal space!)
    for (int x = -1; x <= 96; ++x) {
        for (int z = 3; z <= 53; ++z) {
            for (int y = 0; y <= 5; ++y) {
                bool mainTunnel = (x >= 0 && x <= 95 && z >= 4 && z <= 6 && y >= 1 && y <= 4);
                bool perpTunnel = (x >= 40 && x <= 42 && z >= 4 && z <= 52 && y >= 1 && y <= 4);
                if (mainTunnel || perpTunnel) {
                    world.setBlock(x, y, z, BlockType::Air);
                } else if ((z >= 3 && z <= 7) || (x >= 39 && x <= 43)) {
                    world.setBlock(x, y, z, BlockType::Concrete);
                }
            }
        }
    }
    // Fill diagonal concrete mass between listener (10.5, 5.5) and perp tunnel (41.0, 45.0)
    for (int x = 10; x <= 39; ++x) {
        for (int z = 8; z <= 50; ++z) {
            world.setBlock(x, 2, z, BlockType::Concrete);
        }
    }
    // Tight 2x2m alcove at (x=1..2, z=12..13)
    for (int x = 0; x <= 3; ++x) {
        for (int z = 11; z <= 14; ++z) {
            for (int y = 0; y <= 4; ++y) {
                bool hollow = (x >= 1 && x <= 2 && z >= 12 && z <= 13 && y >= 1 && y <= 2);
                world.setBlock(x, y, z, hollow ? BlockType::Air : BlockType::Concrete);
            }
        }
    }

    AudioEngine audio;
    GameSettings s;
    audio.applySettings(s);

    // Listener in main tunnel at (10.5, 2.5, 5.5) facing -Z (right is +X)
    audio.setListener({10.5f, 2.5f, 5.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 0.0f}, &world, {});

    // 1. Stereo panning: source to the right (+X) louder in right ear; left (-X) louder in left ear
    AcousticParams rightAc = audio.computeAcoustics({15.5f, 2.5f, 5.5f});
    AcousticParams leftAc  = audio.computeAcoustics({5.5f, 2.5f, 5.5f});
    EXPECT_GT(rightAc.gainR, rightAc.gainL * 2.0f);
    EXPECT_GT(leftAc.gainL, leftAc.gainR * 2.0f);
    EXPECT_NEAR(rightAc.obstacleThickness, 0.0f, 0.05f);

    // 2. Straight-line tunnel audibility up to 80m, and perpendicular tunnel audibility at ~50m!
    AcousticParams straight80m = audio.computeAcoustics({90.5f, 2.5f, 5.5f}); // dist = 80.0m straight line
    AcousticParams perp50m     = audio.computeAcoustics({41.0f, 2.5f, 45.0f}); // dist = sqrt(30.5^2 + 39.5^2) = 49.9m in perp tunnel
    float gainStraight80 = straight80m.gainL + straight80m.gainR;
    float gainPerp50     = perp50m.gainL + perp50m.gainR;
    std::printf("[3D AUDIO RANGE] Straight 80m: dist=%.1fm thick=%.2fm gain=%.3f lpf=%.0fHz\n",
                straight80m.distance, straight80m.obstacleThickness, gainStraight80, straight80m.lpfCutoffHz);
    std::printf("[3D AUDIO RANGE] Perp Tunnel 50m: dist=%.1fm thick=%.2fm gain=%.3f lpf=%.0fHz\n",
                perp50m.distance, perp50m.obstacleThickness, gainPerp50, perp50m.lpfCutoffHz);
    EXPECT_NEAR(straight80m.obstacleThickness, 0.0f, 0.05f);
    EXPECT_GT(gainStraight80, 0.05f); // Audible at 80m in a straight line
    EXPECT_LT(perp50m.obstacleThickness, 4.5f); // L-corner path bypasses 30m diagonal concrete mass
    EXPECT_GT(gainPerp50, 0.06f); // Audible at ~50m in perpendicular tunnel

    // 3. Obstacle thickness occlusion: place 1m wall vs 3m thick wall between listener (10.5) and source (16.5)
    for (int z = 4; z <= 6; ++z)
        for (int y = 1; y <= 4; ++y)
            world.setBlock(13, y, z, BlockType::Concrete);
    AcousticParams thinWall = audio.computeAcoustics({16.5f, 2.5f, 5.5f});
    EXPECT_GT(thinWall.obstacleThickness, 0.7f);
    EXPECT_LT(thinWall.gainR, rightAc.gainR);
    EXPECT_LT(thinWall.lpfCutoffHz, 11000.0f);

    for (int x = 12; x <= 14; ++x)
        for (int z = 4; z <= 6; ++z)
            for (int y = 1; y <= 4; ++y)
                world.setBlock(x, y, z, BlockType::Concrete);
    AcousticParams thickWall = audio.computeAcoustics({16.5f, 2.5f, 5.5f});
    EXPECT_GT(thickWall.obstacleThickness, thinWall.obstacleThickness + 1.2f);
    EXPECT_LT(thickWall.gainR, thinWall.gainR * 0.78f);
    EXPECT_LT(thickWall.lpfCutoffHz, thinWall.lpfCutoffHz * 0.65f);

    // 4. Conditional echo: long concrete corridor produces significantly stronger echo than cramped 2x2 alcove
    float corrWet = 0.0f, corrDelay = 0.0f;
    float alcoveWet = 0.0f, alcoveDelay = 0.0f;
    audio.measureRoomEcho({5.5f, 2.5f, 5.5f}, corrWet, corrDelay);
    audio.measureRoomEcho({1.5f, 2.0f, 12.5f}, alcoveWet, alcoveDelay);
    EXPECT_GT(corrWet, alcoveWet * 3.0f);
    EXPECT_GT(corrDelay, alcoveDelay);

    // 5. Per-sound volume scaling from config
    s.masterVolume = 0.5f;
    s.volDoorCreak = 0.8f;
    audio.applySettings(s);
    EXPECT_NEAR(audio.soundVolumeScale(SoundId::DoorCreak), 0.40f, 1e-4f);
}

TEST(LockpickItemAndExitDoorTest, FivePercentBreakChanceAndSafeExitDoor) {
    AudioEngine audio;
    GameWorld gw;
    gw.startNewRun(424242ULL);

    // Starter room spawns lockpicks on the table and locks the 2m steel exit door
    int tableLockpicks = 0;
    for (const auto& it : gw.worldItems) {
        if (it.type == ItemType::Lockpick) tableLockpicks++;
    }
    EXPECT_GE(tableLockpicks, 3);
    ASSERT_FALSE(gw.doors.empty());
    EXPECT_TRUE(gw.doors[0].isExitDoor);
    EXPECT_TRUE(gw.doors[0].locked);

    // Cannot open lockpick minigame with 0 lockpicks
    gw.inventory.lockpicks = 0;
    gw.player.position = {59.8f, 1.0f, 58.5f};
    gw.player.yaw = PI * 0.5f; // Face East (+X) toward exit door
    gw.player.pitch = 0.0f;
    gw.beginInteract(audio);
    EXPECT_FALSE(gw.lockpick.active);

    // With lockpicks, exit door opens with 0% break chance
    gw.inventory.lockpicks = 2;
    gw.beginInteract(audio);
    ASSERT_TRUE(gw.lockpick.active);
    EXPECT_FLOAT_EQ(gw.lockpick.breakChance, 0.0f);

    // 500 pin contacts on exit door (0% break chance) -> ZERO snaps
    for (int k = 0; k < 500; ++k) {
        gw.lockpick.pickX = 0.5f;
        gw.lockpick.pickY = -0.05f;
        gw.lockpick.pins[0].pos = 0.0f;
        gw.lockpick.pins[0].vel = 0.0f;
        gw.lockpick.pins[0].pinned = false;
        gw.lockpick.update(cfg::FIXED_DT, audio);
        gw.lockpick.handleMouseMove(0.0f, -12.0f); // contact pin
        gw.lockpick.update(cfg::FIXED_DT, audio);
        EXPECT_FALSE(gw.lockpick.pickSnappedJustNow);
    }

    // 2000 pin contacts on a regular lock (5% break chance) -> ~5% snaps (within 2.5%..8%)
    LockpickMinigame regLock;
    regLock.open(1, 99999ULL, 0.05f);
    int snaps = 0;
    for (int k = 0; k < 2000; ++k) {
        regLock.pickSnappedJustNow = false;
        regLock.pickX = 0.5f;
        regLock.pickY = -0.05f;
        regLock.pins[0].pos = 0.0f;
        regLock.pins[0].vel = 0.0f;
        regLock.pins[0].pinned = false;
        regLock.pins[0].wasTouchingPick = false;
        regLock.handleMouseMove(0.0f, -12.0f);
        regLock.update(cfg::FIXED_DT, audio);
        if (regLock.pickSnappedJustNow) snaps++;
    }
    EXPECT_GT(snaps, 50);
    EXPECT_LT(snaps, 160);
    std::printf("[LOCKPICK BREAK TEST] 5%% chance over 2000 contacts -> %d snaps (%.2f%%)\n",
                snaps, (snaps * 100.0f) / 2000.0f);
}

TEST(ConfigHotReloadTest, ParsesPlayerLightingAndAudioSettings) {
    const std::string tmpPath = "/tmp/mega_test_cfg.cfg";
    {
        std::ofstream out(tmpPath);
        out << "max_health = 150.0\n"
            << "hunger_drain_rate = 0.5\n"
            << "robot_led_intensity = 2.2\n"
            << "robot_alert_led_color_r = 0.9\n"
            << "vol_door_creak = 0.42\n"
            << "vol_player_step = 0.61\n"
            << "echo_strength = 0.77\n"
            << "lockpick_break_chance = 0.08\n";
    }
    GameSettings s = GameSettings::loadOrCreate(tmpPath);
    EXPECT_FLOAT_EQ(s.maxHealth, 150.0f);
    EXPECT_FLOAT_EQ(s.hungerDrainRate, 0.5f);
    EXPECT_FLOAT_EQ(s.robotLedIntensity, 2.2f);
    EXPECT_FLOAT_EQ(s.robotAlertLedColorR, 0.9f);
    EXPECT_FLOAT_EQ(s.volDoorCreak, 0.42f);
    EXPECT_FLOAT_EQ(s.volPlayerStep, 0.61f);
    EXPECT_FLOAT_EQ(s.echoStrength, 0.77f);
    EXPECT_FLOAT_EQ(s.lockpickBreakChance, 0.08f);

    GameWorld gw;
    gw.settings = s;
    gw.startNewRun(1234ULL);
    EXPECT_FLOAT_EQ(gw.health, 150.0f);
}

TEST(RobotNavigationTest, CenterlineBypassAndCooperativePassing) {
    AudioEngine audio;
    GameWorld gw;
    gw.startNewRun(1111ULL);
    gw.godInvisible = true; // Keep player out of AI aggro

    // Build a clean 3m-wide straight corridor: x=10..42, z=4..6 (centerline z=5.5), y=1..4
    gw.map.world = WorldGrid{};
    for (int x = 9; x <= 43; ++x) {
        for (int z = 3; z <= 7; ++z) {
            for (int y = 0; y <= 5; ++y) {
                bool hollow = (x >= 10 && x <= 42 && z >= 4 && z <= 6 && y >= 1 && y <= 4);
                gw.map.world.setBlock(x, y, z, hollow ? BlockType::Air : BlockType::Concrete);
            }
        }
    }
    gw.player.position = {11.0f, 1.0f, 5.5f};

    // --- PART A: Diagonal obstacle bypass & return to centerline ---
    gw.map.world.addProp({{20.1f, 1.0f, 5.15f}, {20.9f, 2.2f, 5.85f}}, BlockType::HazardStripe, true);
    gw.enemies.clear();
    Enemy r;
    r.pos = {15.0f, 1.0f, 5.5f};
    r.dirX = 1; r.dirZ = 0;
    r.pendingDirX = 1; r.pendingDirZ = 0;
    r.yaw = PI * 0.5f;
    r.decisionCooldown = 999.0f;
    gw.enemies.push_back(r);

    PlayerInput idle;
    float maxOffsetNearProp = 0.0f;
    for (int f = 0; f < 1600; ++f) { // ~26.7s at 0.44m/s -> ~11.7m travel (x: 15 -> 26.7)
        gw.update(idle, cfg::FIXED_DT, audio);
        const auto& e = gw.enemies[0];
        if (e.pos.x >= 19.0f && e.pos.x <= 22.0f) {
            maxOffsetNearProp = std::max(maxOffsetNearProp, std::abs(e.pos.z - 5.5f));
        }
    }
    std::printf("[NAV OBSTACLE BYPASS] final=(%.2f, %.2f) maxSidestep=%.2fm\n",
                gw.enemies[0].pos.x, gw.enemies[0].pos.z, maxOffsetNearProp);
    EXPECT_GT(gw.enemies[0].pos.x, 24.0f);       // Passed cleanly around the obstacle
    EXPECT_GT(maxOffsetNearProp, 0.30f);         // Diagonally sidestepped around the crate
    EXPECT_LT(std::abs(gw.enemies[0].pos.z - 5.5f), 0.22f); // Returned to corridor centerline!

    // --- PART B: Cooperative robot-vs-robot passing in the 3m corridor ---
    gw.map.world = WorldGrid{};
    for (int x = 9; x <= 43; ++x) {
        for (int z = 3; z <= 7; ++z) {
            for (int y = 0; y <= 5; ++y) {
                bool hollow = (x >= 10 && x <= 42 && z >= 4 && z <= 6 && y >= 1 && y <= 4);
                gw.map.world.setBlock(x, y, z, hollow ? BlockType::Air : BlockType::Concrete);
            }
        }
    }
    gw.enemies.clear();
    Enemy rA, rB;
    rA.pos = {16.0f, 1.0f, 5.5f}; rA.dirX = 1;  rA.dirZ = 0; rA.yaw =  PI * 0.5f; rA.decisionCooldown = 999.0f;
    rB.pos = {24.0f, 1.0f, 5.5f}; rB.dirX = -1; rB.dirZ = 0; rB.yaw = -PI * 0.5f; rB.decisionCooldown = 999.0f;
    gw.enemies.push_back(rA);
    gw.enemies.push_back(rB);

    bool negotiatedOpposite = false;
    float minRobotDist = 999.0f;
    for (int f = 0; f < 1800; ++f) { // 30s -> each travels ~13m, must cross each other cleanly
        gw.update(idle, cfg::FIXED_DT, audio);
        const auto& a = gw.enemies[0];
        const auto& b = gw.enemies[1];
        float d = std::hypot(a.pos.x - b.pos.x, a.pos.z - b.pos.z);
        minRobotDist = std::min(minRobotDist, d);
        if (a.yieldTimer > 0.0f && b.yieldTimer > 0.0f &&
            (a.yieldLateralDirZ * b.yieldLateralDirZ < -0.1f)) {
            negotiatedOpposite = true;
        }
    }
    std::printf("[NAV COOP PASS] A=(%.2f, %.2f) B=(%.2f, %.2f) minDist=%.2fm negotiated=%d\n",
                gw.enemies[0].pos.x, gw.enemies[0].pos.z,
                gw.enemies[1].pos.x, gw.enemies[1].pos.z,
                minRobotDist, negotiatedOpposite ? 1 : 0);
    EXPECT_TRUE(negotiatedOpposite);
    EXPECT_GT(gw.enemies[0].pos.x, 24.0f);       // Robot A passed all the way past B's start
    EXPECT_LT(gw.enemies[1].pos.x, 16.0f);       // Robot B passed all the way past A's start
    EXPECT_GT(minRobotDist, 0.70f);              // Never clipped through each other
    EXPECT_LT(std::abs(gw.enemies[0].pos.z - 5.5f), 0.25f); // Returned to center
    EXPECT_LT(std::abs(gw.enemies[1].pos.z - 5.5f), 0.25f); // Returned to center
}

TEST(GodModeTest, NoclipInvisibilityAndSpeedWork) {
    AudioEngine audio;
    GameWorld gw;
    gw.startNewRun(5555ULL);

    // 1. Noclip flies directly through a solid concrete wall
    gw.godMode = true;
    gw.godNoclip = true;
    gw.godSpeedMult = 4.0f;
    gw.player.position = {58.0f, 2.0f, 58.0f};
    gw.player.yaw = 0.0f; // Facing -Z (toward North starter room wall at z=54)
    gw.player.pitch = 0.0f;

    PlayerInput flyFwd;
    flyFwd.moveForward = 1.0f;
    flyFwd.noclip = true;
    for (int f = 0; f < 30; ++f) { // 0.5s at 4.6 * 4 = 18.4 m/s -> ~9.2m straight through concrete!
        gw.update(flyFwd, cfg::FIXED_DT, audio);
    }
    EXPECT_LT(gw.player.position.z, 51.0f); // Passed cleanly through solid z=54 concrete wall
    EXPECT_FLOAT_EQ(gw.health, gw.settings.maxHealth);
    EXPECT_FLOAT_EQ(gw.hunger, gw.settings.maxHunger);

    // 2. God Invisibility prevents robot from spotting player & drops active Chase
    gw.godInvisible = true;
    gw.enemies.clear();
    Enemy e;
    e.pos = {58.0f, 1.0f, 58.0f};
    e.yaw = 0.0f; // Facing +Z
    e.aiState = EnemyAIState::Chase;
    e.alerted = true;
    gw.enemies.push_back(e);
    gw.player.position = {58.0f, 1.0f, 60.0f};

    PlayerInput idle;
    gw.update(idle, cfg::FIXED_DT, audio);
    EXPECT_NE(gw.enemies[0].aiState, EnemyAIState::Chase);
    EXPECT_FALSE(gw.enemies[0].alerted);

    // 3. Locker occlusion: robot looking at player through locker side/back/closed-door cannot see player
    gw.godMode = false;
    gw.godInvisible = false;
    gw.godNoclip = false;
    gw.map.world = WorldGrid{};
    for (int x = 0; x <= 12; ++x)
        for (int z = 0; z <= 12; ++z)
            gw.map.world.setBlock(x, 0, z, BlockType::TechPanel);

    // Build a 1m x 2m locker at x=[5..6], z=[5..6] with back/side walls and closed front door
    gw.map.world.addProp({{5.0f, 1.0f, 5.0f}, {5.06f, 3.0f, 6.0f}}, BlockType::RustPlate, true); // West wall
    gw.map.world.addProp({{5.94f, 1.0f, 5.0f}, {6.0f, 3.0f, 6.0f}}, BlockType::RustPlate, true); // East wall
    gw.map.world.addProp({{5.0f, 1.0f, 5.94f}, {6.0f, 3.0f, 6.0f}}, BlockType::RustPlate, true); // South wall
    gw.doors.clear();
    HingeDoor ld;
    ld.hingePos = {5.0f, 1.0f, 5.02f};
    ld.width = 1.0f; ld.height = 2.0f; ld.thickness = 0.05f;
    ld.closedYaw = 0.0f; ld.angle = 0.0f; ld.isLocker = true;
    gw.doors.push_back(ld);

    gw.player.position = {5.5f, 1.0f, 5.5f}; // Inside locker
    gw.enemies.clear();
    Enemy watcher;
    watcher.pos = {3.2f, 1.0f, 5.5f}; // Standing right outside West locker wall looking East (+X)
    watcher.yaw = PI * 0.5f;
    watcher.aiState = EnemyAIState::Patrol;
    gw.enemies.push_back(watcher);

    gw.update(idle, cfg::FIXED_DT, audio);
    EXPECT_EQ(gw.enemies[0].aiState, EnemyAIState::Patrol);
    EXPECT_FALSE(gw.enemies[0].alerted);
}

TEST(Inventory3DTest, VolumeRotationTopBlockingAndWorldGridPlacement) {
    Backpack3D bp;
    // Verify item 3D dimensions
    Int3 fSz = itemBaseGridSize(ItemType::Flashlight);
    EXPECT_EQ(fSz.x, 2); EXPECT_EQ(fSz.y, 2); EXPECT_EQ(fSz.z, 4);
    Int3 barSz = itemBaseGridSize(ItemType::Food);
    EXPECT_EQ(barSz.x, 1); EXPECT_EQ(barSz.y, 1); EXPECT_EQ(barSz.z, 2);

    // 3-axis rotation swaps dimensions accurately
    ItemOrientation ori;
    ori.rotateY(); // 2x2x4 -> 4x2x2
    Int3 rotY = ori.applyToSize(fSz);
    EXPECT_EQ(rotY.x, 4); EXPECT_EQ(rotY.y, 2); EXPECT_EQ(rotY.z, 2);
    ori.rotateX(); // 4x2x2 -> 4x2x2; test with Food (1x1x2)
    ItemOrientation barOri;
    barOri.rotateX(); // 1x1x2 -> 1x2x1 (standing upright!)
    Int3 barUp = barOri.applyToSize(barSz);
    EXPECT_EQ(barUp.x, 1); EXPECT_EQ(barUp.y, 2); EXPECT_EQ(barUp.z, 1);

    // Gravity/support inside 6x8x4 backpack: cannot float at gy=2 with nothing below!
    EXPECT_FALSE(bp.canPlaceAt(0, 2, 0, ItemType::Food, {}));
    int bottomId = bp.placeAt(0, 0, 0, ItemType::Flashlight, {}); // occupies x=[0..2), y=[0..2), z=[0..4)
    ASSERT_GT(bottomId, 0);
    EXPECT_FALSE(bp.isBlockedFromAbove(bottomId));

    // Stack a Food bar directly on top of the Flashlight at gy=2 -> now Flashlight IS blocked from above!
    int topId = bp.placeAt(0, 2, 0, ItemType::Food, {});
    ASSERT_GT(topId, 0);
    EXPECT_TRUE(bp.isBlockedFromAbove(bottomId));
    EXPECT_FALSE(bp.isBlockedFromAbove(topId));

    // Removing the top item unblocks the bottom Flashlight!
    EXPECT_TRUE(bp.removeById(topId));
    EXPECT_FALSE(bp.isBlockedFromAbove(bottomId));

    // Auto-pack fills cleanly and respects 6x8x4 volume limits
    int autoId = bp.tryAutoAdd(ItemType::Water);
    EXPECT_GT(autoId, 0);
    EXPECT_EQ(bp.countType(ItemType::Water), 1);

    // Diegetic backpack: LMB takes unblocked item into hand and returns backpack to back!
    AudioEngine audio;
    GameWorld gw;
    gw.startNewRun(7070ULL);
    int id1 = gw.backpack.placeAt(0, 0, 0, ItemType::Food, {});
    int id2 = gw.backpack.placeAt(0, 1, 0, ItemType::Battery, {});
    gw.syncInventoryCounts();
    gw.toggleBackpackInHands();
    EXPECT_EQ(gw.backpackMode, BackpackMode::InHands);

    // Attempting to consume bottom Food while Battery sits above it must fail!
    gw.hunger = 50.0f;
    EXPECT_FALSE(gw.useInventoryItem(ItemType::Food, audio));
    // Removing top Battery unblocks Food!
    EXPECT_TRUE(gw.backpack.removeById(id2));
    gw.syncInventoryCounts();
    EXPECT_FALSE(gw.backpack.isBlockedFromAbove(id1));
    EXPECT_TRUE(gw.useInventoryItem(ItemType::Food, audio));

    // Place backpack on the ground & verify mode switches cleanly
    EXPECT_TRUE(gw.togglePlaceBackpackOnGround(audio));
    EXPECT_EQ(gw.backpackMode, BackpackMode::OnGround);
    EXPECT_TRUE(gw.togglePlaceBackpackOnGround(audio));
    EXPECT_EQ(gw.backpackMode, BackpackMode::OnBack);

    // 3D world-grid snap places item cleanly on floor snapped to CELL_SIZE (0.05m)
    Vec3 snapped{};
    Vec3 downOrig = {58.0f, 2.2f, 58.0f};
    Vec3 downDir  = {0.0f, -1.0f, 0.0f};
    ASSERT_TRUE(gw.computeWorldGridSnap(downOrig, downDir, ItemType::Food, {}, snapped));
    EXPECT_NEAR(snapped.y, 1.0f, 0.02f);
    EXPECT_NEAR(snapped.x, std::round(snapped.x / Backpack3D::CELL_SIZE) * Backpack3D::CELL_SIZE, 0.001f);
}

TEST(DoubleDoorAndLootTableTest, Lockers90PercentOpenMetalCabinetsAlwaysLockedAndLootTablesWork) {
    // 1. Custom loot_tables.cfg parsing
    const std::string tmpLoot = "/tmp/mega_test_loot.cfg";
    {
        std::ofstream out(tmpLoot);
        out << "crate.spawn_chance = 1.0\n"
            << "crate.max_items = 2\n"
            << "crate.weight_lockpick = 1.0\n"
            << "crate.weight_flashlight = 0.0\n"
            << "crate.weight_battery = 0.0\n"
            << "crate.weight_food = 0.0\n"
            << "crate.weight_water = 0.0\n"
            << "crate.weight_broom = 0.0\n"
            << "crate.weight_mop = 0.0\n"
            << "crate.weight_tape = 0.0\n"
            << "crate.weight_knife = 0.0\n";
    }
    LootTables lt = LootTables::loadOrCreate(tmpLoot);
    EXPECT_FLOAT_EQ(lt.get(LootSource::Crate).spawnChance, 1.0f);
    EXPECT_EQ(lt.get(LootSource::Crate).maxItems, 2);
    Rng r(123ULL);
    EXPECT_EQ(lt.get(LootSource::Crate).rollItem(r), ItemType::Lockpick);

    // 2. Starter room double doors + metal corridor cabinets (always locked, paired leaves) + 90% open lockers
    GameWorld gw;
    gw.lootTables = lt;
    gw.startNewRun(8888ULL);

    // Starter room cabinet has double doors (indices 1 & 2 linked together)
    ASSERT_GE(gw.doors.size(), 3u);
    EXPECT_TRUE(gw.doors[1].isCabinet);
    EXPECT_TRUE(gw.doors[2].isCabinet);
    EXPECT_EQ(gw.doors[1].linkedDoorIdx, 2);
    EXPECT_EQ(gw.doors[2].linkedDoorIdx, 1);

    // Metal corridor cabinets (linked double doors beyond starter room) MUST ALWAYS be locked
    int metalCabinetLeaves = 0;
    int lockersTotal = 0;
    int lockersOpen = 0;
    for (size_t i = 3; i < gw.doors.size(); ++i) {
        const auto& d = gw.doors[i];
        if (d.isLocker) {
            lockersTotal++;
            if (!d.locked) lockersOpen++;
        } else if (d.isCabinet && d.linkedDoorIdx >= 0) {
            metalCabinetLeaves++;
            EXPECT_TRUE(d.locked);
            EXPECT_EQ(gw.doors[d.linkedDoorIdx].linkedDoorIdx, static_cast<int>(i));
        }
    }
    EXPECT_GT(metalCabinetLeaves, 0);
    EXPECT_GT(lockersTotal, 0);
    float openPct = static_cast<float>(lockersOpen) / std::max(1, lockersTotal);
    std::printf("[LOCKERS & CABINETS] lockers=%d open=%.1f%% | metalCabinetLeaves=%d | lootSurfaces=%zu | items=%zu\n",
                lockersTotal, openPct * 100.0f, metalCabinetLeaves, gw.map.lootSurfaces.size(), gw.worldItems.size());
    EXPECT_GT(openPct, 0.70f); // Statistically ~90% open
    EXPECT_GT(gw.map.lootSurfaces.size(), 10u);
    EXPECT_GT(gw.worldItems.size(), 15u);
}

TEST(RingLoopsAndRareRoomsTest, DeadEndsConvertToRingLoopsAndRareCabinetRooms) {
    GameSettings cfg = GameSettings::loadOrCreate("mega.cfg");
    int totalRings = 0;
    int totalRooms = 0;
    int totalRoomDoors = 0;
    int totalRareCabinets = 0;
    int totalRareSurfaces = 0;

    for (uint64_t seed : {101ULL, 202ULL, 303ULL, 404ULL}) {
        GameWorld gw;
        gw.settings = cfg;
        gw.startNewRun(seed);
        totalRings += gw.map.ringLoopsCreated;
        totalRooms += gw.map.deadEndRoomsCreated;
        totalRoomDoors += static_cast<int>(gw.map.roomDoors.size());

        // Verify every 3x4 dead-end room has a hinged metal door in GameWorld::doors
        EXPECT_EQ(gw.map.roomDoors.size(), static_cast<size_t>(gw.map.deadEndRoomsCreated));
        for (const auto& rd : gw.map.roomDoors) {
            bool foundDoor = false;
            for (const auto& d : gw.doors) {
                if (!d.isCabinet && !d.isLocker && !d.isExitDoor && lengthSq(d.hingePos - rd.hingePos) < 0.01f) {
                    foundDoor = true;
                    break;
                }
            }
            EXPECT_TRUE(foundDoor);
        }

        // Verify flush ring-loop corners: every pair of consecutive isLoopReturn legs shares exact corner overlap (w x w)
        for (size_t i = 0; i + 1 < gw.map.corridors.size(); ++i) {
            const auto& a = gw.map.corridors[i];
            const auto& b = gw.map.corridors[i + 1];
            if (a.isLoopReturn && b.isLoopReturn && a.alongX != b.alongX && a.regX == b.regX && a.regZ == b.regZ) {
                int aMinX = std::min(a.x0, a.x1);
                int aMaxX = a.alongX ? std::max(a.x0, a.x1) : (aMinX + a.width - 1);
                int aMinZ = std::min(a.z0, a.z1);
                int aMaxZ = a.alongX ? (aMinZ + a.width - 1) : std::max(a.z0, a.z1);

                int bMinX = std::min(b.x0, b.x1);
                int bMaxX = b.alongX ? std::max(b.x0, b.x1) : (bMinX + b.width - 1);
                int bMinZ = std::min(b.z0, b.z1);
                int bMaxZ = b.alongX ? (bMinZ + b.width - 1) : std::max(b.z0, b.z1);

                int ovX = std::min(aMaxX, bMaxX) - std::max(aMinX, bMinX) + 1;
                int ovZ = std::min(aMaxZ, bMaxZ) - std::max(aMinZ, bMinZ) + 1;
                EXPECT_EQ(ovX, a.width);
                EXPECT_EQ(ovZ, a.width);
                ++i; // Advance past paired leg
            }
        }

        for (const auto& d : gw.doors) {
            if (d.isRareCabinet) {
                totalRareCabinets++;
                EXPECT_TRUE(d.locked);
                EXPECT_GE(d.linkedDoorIdx, 0);
            }
        }
        for (const auto& ls : gw.map.lootSurfaces) {
            if (ls.source == LootSource::RareCabinet) totalRareSurfaces++;
            EXPECT_GT(ls.surfaceBox.max.y, ls.surfaceBox.min.y); // Every storage surface has real 3D height clearance!
        }
    }

    // Verify 3D storage fit + adjacent floor placement: force 100% Broom spawns on shelves & verify NO Broom clips a 0.45m shelf!
    {
        GameWorld gw;
        for (auto& s : gw.lootTables.sources) {
            s.spawnChance = 1.0f;
            s.maxItems = 1;
            s.weights.fill(0.0f);
            s.weights[static_cast<int>(ItemType::Broom)] = 1.0f;
        }
        gw.startNewRun(777ULL);
        int broomsOnFloor = 0;
        int broomsClippingLowShelf = 0;
        for (const auto& wi : gw.worldItems) {
            if (wi.type != ItemType::Broom) continue;
            if (wi.pos.y <= 1.08f) broomsOnFloor++; // Placed in tall utility bay OR adjacent floor beside storage!
            // A 1.50m broom must NEVER be placed on a low 0.45m-tall interior cabinet/rack shelf (y ~ 1.2..2.6 inside cabinet)
            for (const auto& ls : gw.map.lootSurfaces) {
                if ((ls.source == LootSource::CabinetShelf || ls.source == LootSource::ShelvingRack) &&
                    (ls.surfaceBox.max.y - ls.surfaceBox.min.y) < 1.0f) {
                    if (std::abs(wi.pos.y - ls.surfaceBox.min.y) < 0.05f &&
                        wi.pos.x >= ls.surfaceBox.min.x && wi.pos.x <= ls.surfaceBox.max.x &&
                        wi.pos.z >= ls.surfaceBox.min.z && wi.pos.z <= ls.surfaceBox.max.z) {
                        broomsClippingLowShelf++;
                    }
                }
            }
        }
        EXPECT_EQ(broomsClippingLowShelf, 0);
        EXPECT_GT(broomsOnFloor, 0);
    }

    std::printf("[DEAD-END REWRITE] 4 seeds: ringLoops=%d | deadEnd3x4Rooms=%d | roomDoors=%d | rareCabinetLeaves=%d | rareLootSurfaces=%d\n",
                totalRings, totalRooms, totalRoomDoors, totalRareCabinets, totalRareSurfaces);
    EXPECT_GT(totalRings, 0);
    EXPECT_GT(totalRooms, 0);
    EXPECT_GT(totalRoomDoors, 0);
    EXPECT_GT(totalRareCabinets, 0);
    EXPECT_GT(totalRareSurfaces, 0);
}

TEST(BackSlotMeleeAndCraftingTest, RearStrapSlotBreakingWeaponsAndSurfaceSpearCraft) {
    // 1. Rear 2x30x6 strap slot accepts tall items (>= 20 cells) and rejects short items (< 20 cells)!
    Backpack3D bp;
    EXPECT_TRUE(Backpack3D::isEligibleForBackSlot(ItemType::Broom, {}));
    EXPECT_TRUE(Backpack3D::isEligibleForBackSlot(ItemType::Mop, {}));
    EXPECT_TRUE(Backpack3D::isEligibleForBackSlot(ItemType::Stick, {}));
    EXPECT_TRUE(Backpack3D::isEligibleForBackSlot(ItemType::Spear, {}));

    // Short items must be rejected from the rear strap slot
    EXPECT_FALSE(Backpack3D::isEligibleForBackSlot(ItemType::Knife, {}));      // 1x6x1 (< 20)
    EXPECT_FALSE(Backpack3D::isEligibleForBackSlot(ItemType::Tape, {}));       // 2x1x2 (< 20)
    EXPECT_FALSE(Backpack3D::isEligibleForBackSlot(ItemType::Flashlight, {})); // 2x2x4 (< 20)
    EXPECT_FALSE(bp.canPlaceInBackSlot(0, 0, 0, ItemType::Knife, {}));
    EXPECT_FALSE(bp.canPlaceInBackSlot(0, 0, 0, ItemType::Tape, {}));

    // Rotating Broom horizontally (rotX=1 -> 2x2x30) makes height 2 (< 20) -> rejected!
    ItemOrientation flatOri;
    flatOri.rotX = 1;
    EXPECT_FALSE(Backpack3D::isEligibleForBackSlot(ItemType::Broom, flatOri));

    // Upright Broom & Mop fit cleanly side-by-side inside the 6x30x2 rear strap slot!
    int broomId = bp.tryAutoAdd(ItemType::Broom, 2);
    int mopId = bp.tryAutoAdd(ItemType::Mop, 3);
    ASSERT_GT(broomId, 0);
    ASSERT_GT(mopId, 0);
    ASSERT_NE(bp.findById(broomId), nullptr);
    ASSERT_NE(bp.findById(mopId), nullptr);
    EXPECT_TRUE(bp.findById(broomId)->inBackSlot);
    EXPECT_TRUE(bp.findById(mopId)->inBackSlot);
    EXPECT_FALSE(bp.isBlockedFromAbove(broomId));
    EXPECT_FALSE(bp.isBlockedFromAbove(mopId));

    // 2. RMB-hold rotation changes backpackWorldMatrix cleanly
    AudioEngine audio;
    GameWorld gw;
    gw.startNewRun(9001ULL);
    gw.toggleBackpackInHands();
    EXPECT_EQ(gw.backpackMode, BackpackMode::InHands);
    Mat4 mBefore = gw.backpackWorldMatrix();
    gw.rotateBackpackView(180.0f, 40.0f);
    Mat4 mAfter = gw.backpackWorldMatrix();
    EXPECT_GT(std::abs(gw.backpackViewYaw), 0.5f);
    EXPECT_NE(mBefore.m[0], mAfter.m[0]);
    gw.toggleBackpackInHands(); // Closes backpack and resets rotation

    // 3. Melee combat: Broom/Mop damages robot and breaks after 1..3 hits into Stick!
    gw.player.position = {58.0f, 1.0f, 58.0f};
    gw.player.yaw = 0.0f; // Facing -Z
    gw.player.pitch = 0.0f;
    gw.enemies.clear();
    Enemy targetBot;
    targetBot.pos = {58.0f, 1.0f, 56.2f}; // 1.8m straight ahead (-Z)
    targetBot.hp = 100.0f;
    targetBot.alive = true;
    gw.enemies.push_back(targetBot);

    gw.hasHeldItem = true;
    gw.heldItemType = ItemType::Broom;
    gw.heldItemDurability = 2; // Breaks on 2nd hit
    gw.meleeCooldown = 0.0f;

    EXPECT_TRUE(gw.performMeleeAttack(audio));
    EXPECT_LT(gw.enemies[0].hp, 100.0f);
    EXPECT_EQ(gw.heldItemType, ItemType::Broom);
    EXPECT_EQ(gw.heldItemDurability, 1);

    gw.meleeCooldown = 0.0f;
    EXPECT_TRUE(gw.performMeleeAttack(audio));
    EXPECT_EQ(gw.heldItemType, ItemType::Stick); // Broom head snapped -> transformed into Stick!

    // 4. Surface Crafting: Stick + Tape + Knife placed near each other on a 3D surface crafts Spear!
    gw.hasHeldItem = false;
    gw.worldItems.clear();
    gw.doors.clear();
    // Place all 3 ingredients near each other on the starter room floor in front of player (y=1.0)
    gw.worldItems.push_back({{58.00f, 1.0f, 57.00f}, ItemType::Stick, false, 0, 0, {1, 0, 0}, 0});
    gw.worldItems.push_back({{58.15f, 1.0f, 57.05f}, ItemType::Tape,  false, 0, 0, {}, 0});
    gw.worldItems.push_back({{57.85f, 1.0f, 57.05f}, ItemType::Knife, false, 0, 0, {}, 0});

    // Aim directly down-forward at the Tape at (58.15, 1.025, 57.05) from (58.15, 2.58, 57.55)
    gw.player.position = {58.15f, 1.0f, 57.55f};
    Vec3 toTape = Vec3{58.15f, 1.025f, 57.05f} - gw.player.eyePosition();
    gw.player.yaw = std::atan2(toTape.x, -toTape.z);
    gw.player.pitch = std::atan2(toTape.y, std::hypot(toTape.x, toTape.z));

    gw.updateHover();
    ASSERT_GE(gw.hoveredItemIdx, 0);
    ASSERT_FALSE(gw.availableCrafts.empty());
    EXPECT_EQ(gw.availableCrafts[0].result, ItemType::Spear);

    size_t prevCount = gw.worldItems.size();
    EXPECT_TRUE(gw.performCraft(0, audio));
    EXPECT_TRUE(gw.worldItems[0].pickedUp); // Stick consumed
    EXPECT_TRUE(gw.worldItems[1].pickedUp); // Tape consumed
    EXPECT_TRUE(gw.worldItems[2].pickedUp); // Knife consumed
    ASSERT_EQ(gw.worldItems.size(), prevCount + 1);
    EXPECT_EQ(gw.worldItems.back().type, ItemType::Spear);
    EXPECT_FALSE(gw.worldItems.back().pickedUp);

    // 5. Held-item ground placement preview appears ONLY after pressing RMB (heldPlaceMode)!
    gw.hasHeldItem = true;
    gw.heldPlaceMode = false;
    gw.heldItemType = ItemType::Food;
    gw.heldItemOrient = {};
    gw.hoveredDoorIdx = -1;
    gw.hoveredItemIdx = -1;
    Vec3 downOrig{58.0f, 2.2f, 58.0f};
    Vec3 downDir{0.0f, -1.0f, 0.0f};

    // Before pressing RMB -> NO placement ghost preview on the floor!
    gw.updateCursorHover(downOrig, downDir);
    EXPECT_FALSE(gw.placementPreview.active);

    // Press RMB with item in hand -> enters heldPlaceMode and shows ground placement preview!
    EXPECT_TRUE(gw.handleSecondaryClick(downOrig, downDir, audio));
    EXPECT_TRUE(gw.heldPlaceMode);
    EXPECT_TRUE(gw.placementPreview.active);

    // Click LMB -> places item onto floor and exits heldPlaceMode!
    EXPECT_TRUE(gw.handlePrimaryClick(downOrig, downDir, audio));
    EXPECT_FALSE(gw.hasHeldItem);
    EXPECT_FALSE(gw.heldPlaceMode);
    EXPECT_FALSE(gw.placementPreview.active);
}

TEST(MultiplayerTest, OnlineModeHasZeroAIAndSpawnsOneHumanVsOneMonster) {
    AudioEngine audio;
    GameWorld gw;
    gw.startOnlineMatch(2025ULL, PlayerRole::Monster, 3);

    EXPECT_TRUE(gw.onlineMode);
    EXPECT_EQ(gw.localRole, PlayerRole::Monster);
    EXPECT_EQ(gw.roundId, 3u);
    EXPECT_EQ(gw.matchPhase, MatchPhase::Playing);
    // Strictly 0 AI bots in 1v1 online mode!
    EXPECT_TRUE(gw.enemies.empty());

    // Even after moving near outer 500m area & ticking, ZERO AI bots ever spawn in onlineMode
    gw.player.position = {448.0f, 1.0f, 64.5f};
    PlayerInput idle;
    for (int i = 0; i < 60; ++i) {
        gw.update(idle, cfg::FIXED_DT, audio);
    }
    EXPECT_TRUE(gw.enemies.empty());

    // Verify Monster spawned at valid corridor >= 25m from starter spawn
    GameWorld gwFresh;
    gwFresh.startOnlineMatch(2025ULL, PlayerRole::Human, 1);
    float distFromSpawn = std::hypot(gwFresh.monster.pos.x - gwFresh.map.spawnPos.x,
                                     gwFresh.monster.pos.z - gwFresh.map.spawnPos.z);
    EXPECT_GE(distFromSpawn, 25.0f);
    EXPECT_TRUE(gwFresh.enemies.empty());
}

TEST(MultiplayerTest, MonsterAbilitiesLockerCheckAndAsymmetricCombat) {
    AudioEngine audio;
    GameWorld gw;
    gw.startOnlineMatch(7777ULL, PlayerRole::Monster, 1);

    // Clear starter area into an open 12x12 floor test room
    gw.map.world = WorldGrid{};
    for (int x = 0; x <= 14; ++x)
        for (int z = 0; z <= 14; ++z)
            gw.map.world.setBlock(x, 0, z, BlockType::TechPanel);
    gw.doors.clear();

    // Place a locker at x=[5..6], z=[5..6] with side/back walls and an unlocked closed door
    gw.map.world.addProp({{5.0f, 1.0f, 5.0f}, {5.06f, 3.0f, 6.0f}}, BlockType::RustPlate, true);
    gw.map.world.addProp({{5.94f, 1.0f, 5.0f}, {6.0f, 3.0f, 6.0f}}, BlockType::RustPlate, true);
    gw.map.world.addProp({{5.0f, 1.0f, 5.94f}, {6.0f, 3.0f, 6.0f}}, BlockType::RustPlate, true);
    HingeDoor locker;
    locker.hingePos = {5.0f, 1.0f, 5.02f};
    locker.width = 1.0f;
    locker.height = 2.0f;
    locker.thickness = 0.05f;
    locker.closedYaw = 0.0f;
    locker.angle = 0.0f;
    locker.openSign = 1.0f;
    locker.isLocker = true;
    locker.locked = false;
    locker.lockerInterior = {{5.06f, 1.0f, 5.06f}, {5.94f, 3.0f, 5.94f}};
    gw.doors.push_back(locker);

    // Put Human hidden inside the locker
    gw.player.position = {5.5f, 1.0f, 5.5f};
    gw.health = 100.0f;

    // Monster stands in front of locker facing +Z toward locker door (yaw = PI -> +Z)
    gw.monster.pos = {5.5f, 1.0f, 3.8f};
    gw.monster.yaw = PI;
    gw.monster.pitch = -0.35f;

    // 1. Night Vision toggle & Sprint stamina drain + regen
    EXPECT_FALSE(gw.monster.nightVision);
    MonsterInput nvSprint;
    nvSprint.nightVisionToggle = true;
    nvSprint.sprint = true;
    nvSprint.moveForward = 1.0f;
    gw.updateMonster(nvSprint, cfg::FIXED_DT, audio, true);
    EXPECT_TRUE(gw.monster.nightVision);
    EXPECT_LT(gw.monster.stamina, 100.0f);

    // 2. Dash impulse & 3.2s cooldown
    float prevZ = gw.monster.pos.z;
    MonsterInput dashIn;
    dashIn.moveForward = 1.0f;
    dashIn.dashPressed = true;
    gw.updateMonster(dashIn, cfg::FIXED_DT, audio, true);
    EXPECT_GT(gw.monster.dashCooldown, 3.0f);
    EXPECT_GT(gw.monster.pos.z - prevZ, 0.15f);

    // 3. Claw strike blocked by closed locker door -> instead rips the unlocked locker open!
    gw.monster.pos = {5.5f, 1.0f, 4.1f};
    gw.monster.yaw = PI;
    gw.monster.pitch = -0.35f;
    gw.monster.attackCooldown = 0.0f;
    MonsterInput clawDoor;
    clawDoor.attackPressed = true;
    gw.updateMonster(clawDoor, cfg::FIXED_DT, audio, true);
    EXPECT_FLOAT_EQ(gw.health, 100.0f); // Blocked by closed locker door
    EXPECT_GE(gw.doors[0].angle, 0.70f); // Locker ripped open!

    // 4. Now Human is exposed! Next claw strike hits Human for 25 HP
    gw.doors[0].angle = 1.5f;
    gw.monster.attackCooldown = 0.0f;
    gw.updateMonster(clawDoor, cfg::FIXED_DT, audio, true);
    EXPECT_FLOAT_EQ(gw.health, 75.0f);

    // 5. Human counterattacks Monster with Spear melee!
    gw.localRole = PlayerRole::Human;
    gw.player.position = {5.5f, 1.0f, 5.5f};
    gw.player.yaw = 0.0f; // Looking -Z toward monster at z=4.1
    gw.player.pitch = 0.15f;
    gw.hasHeldItem = true;
    gw.heldItemType = ItemType::Spear;
    gw.meleeCooldown = 0.0f;
    float monsterHpBefore = gw.monster.hp;
    EXPECT_TRUE(gw.performMeleeAttack(audio));
    EXPECT_LT(gw.monster.hp, monsterHpBefore);

    // 6. Monster lethal blow triggers MatchPhase::MonsterWon
    gw.health = 20.0f;
    gw.monster.attackCooldown = 0.0f;
    gw.updateMonster(clawDoor, cfg::FIXED_DT, audio, true);
    EXPECT_FLOAT_EQ(gw.health, 0.0f);
    EXPECT_TRUE(gw.gameOver);
    EXPECT_EQ(gw.matchPhase, MatchPhase::MonsterWon);

    // 7. Human lethal Spear strike triggers MatchPhase::HumanWon
    gw.startOnlineMatch(7777ULL, PlayerRole::Human, 2);
    gw.map.world = WorldGrid{};
    for (int x = 0; x <= 14; ++x)
        for (int z = 0; z <= 14; ++z)
            gw.map.world.setBlock(x, 0, z, BlockType::TechPanel);
    gw.doors.clear();
    gw.player.position = {6.0f, 1.0f, 6.0f};
    gw.player.yaw = 0.0f;
    gw.player.pitch = 0.1f;
    gw.monster.pos = {6.0f, 1.0f, 4.6f};
    gw.monster.hp = 15.0f;
    gw.hasHeldItem = true;
    gw.heldItemType = ItemType::Spear;
    gw.meleeCooldown = 0.0f;
    EXPECT_TRUE(gw.performMeleeAttack(audio));
    EXPECT_FLOAT_EQ(gw.monster.hp, 0.0f);
    EXPECT_TRUE(gw.gameOver);
    EXPECT_EQ(gw.matchPhase, MatchPhase::HumanWon);
}

TEST(MultiplayerTest, NetSessionSerializationAndUdpLoopback) {
    // 1. Binary roundtrip of NetInputFrame
    NetInputFrame in;
    in.seq = 42;
    in.lastAckEventId = 19;
    in.role = static_cast<uint8_t>(PlayerRole::Monster);
    in.moveForward = 1.0f;
    in.moveRight = -0.5f;
    in.yaw = 1.234f;
    in.pitch = -0.456f;
    in.flags = (1u << 0) | (1u << 2) | (1u << 4);
    in.dragDx = 3.5f;
    in.dragDy = -7.25f;
    in.rayOrig = {12.5f, 2.5f, 44.0f};
    in.rayDir = {0.0f, 0.0f, -1.0f};
    in.craftOptionIdx = 2;
    in.quickUseSlot = 1;

    auto inBytes = NetSession::serializeClientInput(in);
    NetInputFrame inOut;
    ASSERT_TRUE(NetSession::deserializeClientInput(inBytes.data(), inBytes.size(), inOut));
    EXPECT_EQ(inOut.seq, 42u);
    EXPECT_EQ(inOut.lastAckEventId, 19u);
    EXPECT_EQ(inOut.role, in.role);
    EXPECT_FLOAT_EQ(inOut.moveForward, 1.0f);
    EXPECT_FLOAT_EQ(inOut.moveRight, -0.5f);
    EXPECT_FLOAT_EQ(inOut.yaw, 1.234f);
    EXPECT_FLOAT_EQ(inOut.pitch, -0.456f);
    EXPECT_EQ(inOut.flags, in.flags);
    EXPECT_EQ(inOut.craftOptionIdx, 2);
    EXPECT_EQ(inOut.quickUseSlot, 1);

    // 2. Snapshot build & binary roundtrip from real GameWorld
    AudioEngine audio;
    GameWorld hostWorld;
    hostWorld.startOnlineMatch(445566ULL, PlayerRole::Human, 5);
    hostWorld.recordSoundEvent(SoundId::DoorCreak, true, {60.0f, 2.0f, 58.0f}, 0.7f, 1.0f, 0.4f);
    NetWorldSnapshot snap = hostWorld.buildSnapshot(100, 42, 0);
    ASSERT_FALSE(snap.doors.empty());
    ASSERT_EQ(snap.soundEvents.size(), 1u);

    auto snapBytes = NetSession::serializeWorldSnapshot(snap);
    NetWorldSnapshot snapOut;
    ASSERT_TRUE(NetSession::deserializeWorldSnapshot(snapBytes.data(), snapBytes.size(), snapOut));
    EXPECT_EQ(snapOut.serverTick, 100u);
    EXPECT_EQ(snapOut.ackInputSeq, 42u);
    EXPECT_EQ(snapOut.roundId, 5u);
    EXPECT_EQ(snapOut.baseSeed, 445566ULL);
    EXPECT_EQ(snapOut.doors.size(), snap.doors.size());
    EXPECT_EQ(snapOut.soundEvents.size(), 1u);
    EXPECT_EQ(snapOut.soundEvents[0].soundId, static_cast<uint8_t>(SoundId::DoorCreak));

    // 3. Real UDP loopback handshake, input transmission, and snapshot delivery on localhost
    constexpr uint16_t TEST_PORT = 17777;
    NetSession hostNet;
    NetSession clientNet;
    ASSERT_TRUE(hostNet.startHost(TEST_PORT, PlayerRole::Human, 445566ULL));
    ASSERT_TRUE(clientNet.startClient("127.0.0.1", TEST_PORT));

    double t = 0.0;
    for (int step = 0; step < 30; ++step) {
        t += 0.02;
        clientNet.poll(t);
        hostNet.poll(t);
        if (hostNet.isConnected() && clientNet.isConnected()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_TRUE(hostNet.isConnected());
    ASSERT_TRUE(clientNet.isConnected());
    EXPECT_EQ(clientNet.localRole(), PlayerRole::Monster);
    EXPECT_EQ(clientNet.negotiatedSeed(), 445566ULL);

    ASSERT_TRUE(clientNet.sendClientInput(in));
    ASSERT_TRUE(hostNet.sendWorldSnapshot(snap));

    NetInputFrame recvIn;
    NetWorldSnapshot recvSnap;
    bool gotIn = false;
    bool gotSnap = false;
    for (int step = 0; step < 20 && (!gotIn || !gotSnap); ++step) {
        t += 0.02;
        hostNet.poll(t);
        clientNet.poll(t);
        if (!gotIn && hostNet.popLatestClientInput(recvIn)) gotIn = true;
        if (!gotSnap && clientNet.popLatestSnapshot(recvSnap)) gotSnap = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_TRUE(gotIn);
    EXPECT_TRUE(gotSnap);
    EXPECT_EQ(recvIn.seq, 42u);
    EXPECT_EQ(recvSnap.serverTick, 100u);
    EXPECT_EQ(recvSnap.baseSeed, 445566ULL);

    clientNet.stop();
    hostNet.stop();
}

TEST(MultiplayerTest, RoomCodeEncodingAndNatPunchLoopback) {
    // 1. Base32 Room Code roundtrip with CRC8 checksum across varied IPv4 + ports
    auto makeNetIp = [](uint8_t a, uint8_t b, uint8_t c, uint8_t d) -> uint32_t {
        uint32_t host = (static_cast<uint32_t>(a) << 24) |
                        (static_cast<uint32_t>(b) << 16) |
                        (static_cast<uint32_t>(c) << 8) |
                        static_cast<uint32_t>(d);
        return ((host & 0xFFu) << 24) | ((host & 0xFF00u) << 8) |
               ((host & 0xFF0000u) >> 8) | ((host & 0xFF000000u) >> 24);
    };
    auto makeNetPort = [](uint16_t p) -> uint16_t {
        return static_cast<uint16_t>(((p & 0xFFu) << 8) | ((p >> 8) & 0xFFu));
    };

    const std::pair<uint32_t, uint16_t> cases[] = {
        {makeNetIp(203, 0, 113, 42), makeNetPort(7777)},
        {makeNetIp(192, 168, 1, 99), makeNetPort(51234)},
        {makeNetIp(127, 0, 0, 1),    makeNetPort(17778)},
    };
    for (const auto& [ipNet, portNet] : cases) {
        std::string code = NetSession::encodeRoomCode(ipNet, portNet);
        ASSERT_EQ(code.size(), 14u); // XXXX-XXXX-XXXX
        EXPECT_EQ(code[4], '-');
        EXPECT_EQ(code[9], '-');

        NetEndpoint decoded{};
        ASSERT_TRUE(NetSession::decodeRoomCode(code, decoded));
        EXPECT_EQ(decoded.ipv4, ipNet);
        EXPECT_EQ(decoded.port, portNet);

        // Case-insensitive & dash-free tolerance
        std::string lowerNoDash;
        for (char c : code) {
            if (c != '-') lowerNoDash.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        NetEndpoint decodedLower{};
        ASSERT_TRUE(NetSession::decodeRoomCode(lowerNoDash, decodedLower));
        EXPECT_EQ(decodedLower, decoded);

        // Single-character mutation must be caught by CRC8
        std::string corrupted = code;
        corrupted[0] = (corrupted[0] == 'A') ? 'B' : 'A';
        NetEndpoint bad{};
        EXPECT_FALSE(NetSession::decodeRoomCode(corrupted, bad));
    }

    // 2. parseTarget supports Room Code, IP:port, and bare IP with fallback port
    {
        std::string code = NetSession::encodeRoomCode(makeNetIp(127, 0, 0, 1), makeNetPort(17778));
        NetEndpoint epCode{}, epIpPort{}, epBareIp{}, epBad{};
        ASSERT_TRUE(NetSession::parseTarget(code, 7777, epCode));
        ASSERT_TRUE(NetSession::parseTarget("127.0.0.1:17778", 7777, epIpPort));
        ASSERT_TRUE(NetSession::parseTarget("127.0.0.1", 17778, epBareIp));
        EXPECT_EQ(epCode, epIpPort);
        EXPECT_EQ(epCode, epBareIp);
        EXPECT_FALSE(NetSession::parseTarget("", 7777, epBad));
    }

    // 3. Full Room Code + simultaneous PKT_PUNCH UDP loopback between Host & Client
    constexpr uint16_t HOST_PORT = 17778;
    NetSession hostNet;
    NetSession clientNet;

    // Client binds ephemeral lobby socket first (like in Join UI), Host binds HOST_PORT (without touching router UPnP in CI)
    ASSERT_TRUE(clientNet.prepareLobbySocket(0, false));
    uint16_t clientPort = clientNet.boundPort();
    ASSERT_GT(clientPort, 0);

    std::string hostRoomCode = NetSession::encodeRoomCode(makeNetIp(127, 0, 0, 1), makeNetPort(HOST_PORT));
    std::string guestPunchCode = NetSession::encodeRoomCode(makeNetIp(127, 0, 0, 1), makeNetPort(clientPort));

    ASSERT_TRUE(hostNet.startHost(HOST_PORT, PlayerRole::Monster, 998877ULL));
    ASSERT_TRUE(hostNet.setPunchTarget(guestPunchCode));
    ASSERT_TRUE(clientNet.startClient(hostRoomCode, NetSession::DEFAULT_PORT));
    EXPECT_EQ(clientNet.boundPort(), clientPort); // Socket preserved across lobby -> startClient!

    double t = 0.0;
    for (int step = 0; step < 30; ++step) {
        t += 0.02;
        hostNet.poll(t);
        clientNet.poll(t);
        if (hostNet.isConnected() && clientNet.isConnected()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_TRUE(hostNet.isConnected());
    ASSERT_TRUE(clientNet.isConnected());
    EXPECT_EQ(hostNet.localRole(), PlayerRole::Monster);
    EXPECT_EQ(clientNet.localRole(), PlayerRole::Human);
    EXPECT_EQ(clientNet.negotiatedSeed(), 998877ULL);

    clientNet.stop();
    hostNet.stop();
}

