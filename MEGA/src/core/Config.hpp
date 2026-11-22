#pragma once

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace mega::cfg {

// Native 1280x720 resolution
inline constexpr int WINDOW_WIDTH  = 1280;
inline constexpr int WINDOW_HEIGHT = 720;
inline constexpr int RETRO_WIDTH   = 1280;
inline constexpr int RETRO_HEIGHT  = 720;

// World & Chunk dimensions (1 unit = 1 meter)
inline constexpr int CHUNK_SIZE  = 16;
inline constexpr int REGION_SIZE = 128; // 128x128m streaming region

// Physics & Player defaults
inline constexpr float FIXED_DT        = 1.0f / 60.0f;
inline constexpr float GRAVITY         = 20.0f;
inline constexpr float JUMP_SPEED      = 6.8f;
inline constexpr float MOVE_SPEED      = 4.6f;
inline constexpr float SPRINT_MULT     = 1.45f;
inline constexpr float GROUND_ACCEL    = 12.0f;
inline constexpr float AIR_ACCEL       = 3.5f;
inline constexpr float GROUND_FRICTION = 10.0f;
inline constexpr float STEP_HEIGHT     = 0.52f;

inline constexpr float PLAYER_RADIUS   = 0.30f;
inline constexpr float PLAYER_HEIGHT   = 1.75f;
inline constexpr float PLAYER_EYE_Y    = 1.56f;

inline constexpr float MOUSE_SENS      = 0.0020f;
inline constexpr float FOV_DEGREES     = 70.0f;
inline constexpr float NEAR_PLANE      = 0.08f;
inline constexpr float FAR_PLANE       = 140.0f;

} // namespace mega::cfg

namespace mega {

struct GameSettings {
    // --- Lighting & Object Glow ---
    float ambientBrightness     = 0.036f;
    float lampIntensity         = 1.45f;
    float lampRadius            = 18.0f;
    float lampColorR            = 0.72f;
    float lampColorG            = 0.88f;
    float lampColorB            = 1.00f;
    float flashlightIntensity   = 1.50f;
    float flashlightRadius      = 28.0f;
    float flashlightColorR      = 0.88f;
    float flashlightColorG      = 0.93f;
    float flashlightColorB      = 1.00f;
    float fogDensity            = 0.032f;
    float objectEmissiveScale   = 1.00f;

    // Robot LED light & visual glow settings
    float robotLedIntensity       = 0.90f;
    float robotLedRadius          = 7.0f;
    float robotLedColorR          = 0.25f;
    float robotLedColorG          = 0.80f;
    float robotLedColorB          = 1.00f;
    float robotAlertLedIntensity  = 1.35f;
    float robotAlertLedRadius     = 9.5f;
    float robotAlertLedColorR     = 1.00f;
    float robotAlertLedColorG     = 0.12f;
    float robotAlertLedColorB     = 0.10f;
    float robotLedGlowBoost       = 1.85f;

    // --- Pixelated Lighting Effect ---
    float lightTexelSnap      = 32.0f; // Snaps lighting calculation to 32x32 texture pixels
    float lightSteps          = 14.0f; // Quantized lighting bands (0 = smooth)
    float lightDitherStrength = 0.35f; // Ordered 2x2 dither between lighting bands

    // --- Player Characteristics (Max, Drain, Restore, Movement) ---
    float maxHealth             = 100.0f;
    float maxHunger             = 100.0f;
    float maxThirst             = 100.0f;
    float maxEnergy             = 100.0f;
    float maxBattery            = 100.0f;
    float hungerDrainRate       = 0.16f;
    float thirstDrainRate       = 0.24f;
    float thirstSprintDrainRate = 0.42f;
    float starvationDamageRate  = 1.50f;
    float dehydrationDamageRate = 2.00f;
    float energySprintDrainRate = 18.0f;
    float energyRegenRate       = 13.0f;
    float energyJumpCost        = 10.0f;
    float batteryDrainRate      = 1.15f;
    float foodRestore           = 35.0f;
    float waterRestore          = 40.0f;
    float batteryRestore        = 45.0f;
    float moveSpeed             = cfg::MOVE_SPEED;
    float sprintMult            = cfg::SPRINT_MULT;
    float jumpSpeed             = cfg::JUMP_SPEED;
    float lockpickBreakChance   = 0.05f;

    // --- 3D Audio & Per-Sound Volumes ---
    float masterVolume       = 1.00f;
    float ambientVolume      = 0.32f;
    float echoStrength       = 0.50f;
    float occlusionStrength  = 1.00f;
    float soundMaxDist       = 85.0f;
    float soundDistanceAtten = 0.026f; // Distance rolloff coefficient (audible up to 80m straight line)
    float soundObstacleAtten = 0.22f;  // Obstacle & corner attenuation per meter (audible ~50m in perp tunnel)
    float soundObstacleLpf   = 0.45f;  // Obstacle low-pass muffling coefficient

    float volPlayerStep     = 0.75f;
    float volRobotStep      = 0.95f;
    float volRobotAlert     = 0.85f;
    float volDoorCreak      = 0.80f;
    float volDoorImpact     = 0.90f;
    float volFlashlight     = 0.80f;
    float volPickup         = 0.70f;
    float volPlayerHurt     = 0.85f;
    float volLockPinSet     = 0.80f;
    float volLockBreak      = 0.85f;
    float volLockOpen       = 0.90f;
    float volLockpickSnap   = 0.95f;
    float volWeapon         = 0.85f;

    // --- Corridor Generation & Streaming ---
    int corridorMinLen        = 30;
    int corridorMaxLen        = 80;
    int corridorMinWidth      = 2;
    int corridorMaxWidth      = 4;
    int minConnections        = 2;
    int maxConnections        = 3;
    int minParallelSpacing    = 22;
    int streamRadius          = 1;

    static void writeDefaultFile(const std::string& path) {
        std::ofstream out(path);
        if (!out) return;
        out << "# MEGA // Configuration File\n"
            << "# Press F5 in-game to hot-reload lighting, player & audio settings, or R to regenerate world.\n\n"
            << "# --- Lighting & Object Glow ---\n"
            << "ambient_brightness = 0.036\n"
            << "lamp_intensity = 1.45\n"
            << "lamp_radius = 18.0\n"
            << "lamp_color_r = 0.72\n"
            << "lamp_color_g = 0.88\n"
            << "lamp_color_b = 1.00\n"
            << "flashlight_intensity = 1.50\n"
            << "flashlight_radius = 28.0\n"
            << "flashlight_color_r = 0.88\n"
            << "flashlight_color_g = 0.93\n"
            << "flashlight_color_b = 1.00\n"
            << "fog_density = 0.032\n"
            << "object_emissive_scale = 1.00\n\n"
            << "# --- Robot LED Glow & Light ---\n"
            << "robot_led_intensity = 0.90\n"
            << "robot_led_radius = 7.0\n"
            << "robot_led_color_r = 0.25\n"
            << "robot_led_color_g = 0.80\n"
            << "robot_led_color_b = 1.00\n"
            << "robot_alert_led_intensity = 1.35\n"
            << "robot_alert_led_radius = 9.5\n"
            << "robot_alert_led_color_r = 1.00\n"
            << "robot_alert_led_color_g = 0.12\n"
            << "robot_alert_led_color_b = 0.10\n"
            << "robot_led_glow_boost = 1.85\n\n"
            << "# --- Pixelated 3D Lighting Effect ---\n"
            << "light_texel_snap = 32.0\n"
            << "light_steps = 14.0\n"
            << "light_dither_strength = 0.35\n\n"
            << "# --- Player Characteristics ---\n"
            << "max_health = 100.0\n"
            << "max_hunger = 100.0\n"
            << "max_thirst = 100.0\n"
            << "max_energy = 100.0\n"
            << "max_battery = 100.0\n"
            << "hunger_drain_rate = 0.16\n"
            << "thirst_drain_rate = 0.24\n"
            << "thirst_sprint_drain_rate = 0.42\n"
            << "starvation_damage_rate = 1.50\n"
            << "dehydration_damage_rate = 2.00\n"
            << "energy_sprint_drain_rate = 18.0\n"
            << "energy_regen_rate = 13.0\n"
            << "energy_jump_cost = 10.0\n"
            << "battery_drain_rate = 1.15\n"
            << "food_restore = 35.0\n"
            << "water_restore = 40.0\n"
            << "battery_restore = 45.0\n"
            << "move_speed = 4.6\n"
            << "sprint_mult = 1.45\n"
            << "jump_speed = 6.8\n"
            << "lockpick_break_chance = 0.05\n\n"
            << "# --- 3D Audio & Volumes ---\n"
            << "master_volume = 1.00\n"
            << "ambient_volume = 0.32\n"
            << "echo_strength = 0.50\n"
            << "occlusion_strength = 1.00\n"
            << "sound_max_dist = 85.0\n"
            << "sound_distance_atten = 0.026\n"
            << "sound_obstacle_atten = 0.22\n"
            << "sound_obstacle_lpf = 0.45\n"
            << "vol_player_step = 0.75\n"
            << "vol_robot_step = 0.95\n"
            << "vol_robot_alert = 0.85\n"
            << "vol_door_creak = 0.80\n"
            << "vol_door_impact = 0.90\n"
            << "vol_flashlight = 0.80\n"
            << "vol_pickup = 0.70\n"
            << "vol_player_hurt = 0.85\n"
            << "vol_lock_pin_set = 0.80\n"
            << "vol_lock_break = 0.85\n"
            << "vol_lock_open = 0.90\n"
            << "vol_lockpick_snap = 0.95\n"
            << "vol_weapon = 0.85\n\n"
            << "# --- Utility Complex Corridor Generator ---\n"
            << "corridor_min_len = 30\n"
            << "corridor_max_len = 80\n"
            << "corridor_min_width = 2\n"
            << "corridor_max_width = 4\n"
            << "min_connections = 2\n"
            << "max_connections = 3\n"
            << "min_parallel_spacing = 22\n"
            << "stream_radius = 1\n";
    }

    static GameSettings loadOrCreate(const std::string& path = "mega.cfg") {
        GameSettings s;
        std::ifstream in(path);
        if (!in.good()) {
            writeDefaultFile(path);
            return s;
        }

        std::string line;
        while (std::getline(in, line)) {
            auto hashPos = line.find('#');
            if (hashPos != std::string::npos) line = line.substr(0, hashPos);
            auto eqPos = line.find('=');
            if (eqPos == std::string::npos) continue;

            auto trim = [](std::string str) {
                size_t a = str.find_first_not_of(" \t\r\n");
                if (a == std::string::npos) return std::string{};
                size_t b = str.find_last_not_of(" \t\r\n");
                return str.substr(a, b - a + 1);
            };

            std::string key = trim(line.substr(0, eqPos));
            std::string val = trim(line.substr(eqPos + 1));
            if (key.empty() || val.empty()) continue;

            try {
                // Lighting & Object Glow
                if (key == "ambient_brightness") s.ambientBrightness = std::max(0.0f, std::stof(val));
                else if (key == "lamp_intensity") s.lampIntensity = std::max(0.0f, std::stof(val));
                else if (key == "lamp_radius") s.lampRadius = std::max(0.5f, std::stof(val));
                else if (key == "lamp_color_r") s.lampColorR = std::max(0.0f, std::stof(val));
                else if (key == "lamp_color_g") s.lampColorG = std::max(0.0f, std::stof(val));
                else if (key == "lamp_color_b") s.lampColorB = std::max(0.0f, std::stof(val));
                else if (key == "flashlight_intensity") s.flashlightIntensity = std::max(0.0f, std::stof(val));
                else if (key == "flashlight_radius") s.flashlightRadius = std::max(0.5f, std::stof(val));
                else if (key == "flashlight_color_r") s.flashlightColorR = std::max(0.0f, std::stof(val));
                else if (key == "flashlight_color_g") s.flashlightColorG = std::max(0.0f, std::stof(val));
                else if (key == "flashlight_color_b") s.flashlightColorB = std::max(0.0f, std::stof(val));
                else if (key == "fog_density") s.fogDensity = std::max(0.0f, std::stof(val));
                else if (key == "object_emissive_scale") s.objectEmissiveScale = std::max(0.0f, std::stof(val));

                // Robot LED
                else if (key == "robot_led_intensity") s.robotLedIntensity = std::max(0.0f, std::stof(val));
                else if (key == "robot_led_radius") s.robotLedRadius = std::max(0.5f, std::stof(val));
                else if (key == "robot_led_color_r") s.robotLedColorR = std::max(0.0f, std::stof(val));
                else if (key == "robot_led_color_g") s.robotLedColorG = std::max(0.0f, std::stof(val));
                else if (key == "robot_led_color_b") s.robotLedColorB = std::max(0.0f, std::stof(val));
                else if (key == "robot_alert_led_intensity") s.robotAlertLedIntensity = std::max(0.0f, std::stof(val));
                else if (key == "robot_alert_led_radius") s.robotAlertLedRadius = std::max(0.5f, std::stof(val));
                else if (key == "robot_alert_led_color_r") s.robotAlertLedColorR = std::max(0.0f, std::stof(val));
                else if (key == "robot_alert_led_color_g") s.robotAlertLedColorG = std::max(0.0f, std::stof(val));
                else if (key == "robot_alert_led_color_b") s.robotAlertLedColorB = std::max(0.0f, std::stof(val));
                else if (key == "robot_led_glow_boost") s.robotLedGlowBoost = std::max(0.1f, std::stof(val));

                // Pixelated lighting
                else if (key == "light_texel_snap") s.lightTexelSnap = std::stof(val);
                else if (key == "light_steps") s.lightSteps = std::stof(val);
                else if (key == "light_dither_strength") s.lightDitherStrength = std::stof(val);

                // Player characteristics
                else if (key == "max_health") s.maxHealth = std::max(1.0f, std::stof(val));
                else if (key == "max_hunger") s.maxHunger = std::max(1.0f, std::stof(val));
                else if (key == "max_thirst") s.maxThirst = std::max(1.0f, std::stof(val));
                else if (key == "max_energy") s.maxEnergy = std::max(1.0f, std::stof(val));
                else if (key == "max_battery") s.maxBattery = std::max(1.0f, std::stof(val));
                else if (key == "hunger_drain_rate") s.hungerDrainRate = std::max(0.0f, std::stof(val));
                else if (key == "thirst_drain_rate") s.thirstDrainRate = std::max(0.0f, std::stof(val));
                else if (key == "thirst_sprint_drain_rate") s.thirstSprintDrainRate = std::max(0.0f, std::stof(val));
                else if (key == "starvation_damage_rate") s.starvationDamageRate = std::max(0.0f, std::stof(val));
                else if (key == "dehydration_damage_rate") s.dehydrationDamageRate = std::max(0.0f, std::stof(val));
                else if (key == "energy_sprint_drain_rate") s.energySprintDrainRate = std::max(0.0f, std::stof(val));
                else if (key == "energy_regen_rate") s.energyRegenRate = std::max(0.0f, std::stof(val));
                else if (key == "energy_jump_cost") s.energyJumpCost = std::max(0.0f, std::stof(val));
                else if (key == "battery_drain_rate") s.batteryDrainRate = std::max(0.0f, std::stof(val));
                else if (key == "food_restore") s.foodRestore = std::max(0.0f, std::stof(val));
                else if (key == "water_restore") s.waterRestore = std::max(0.0f, std::stof(val));
                else if (key == "battery_restore") s.batteryRestore = std::max(0.0f, std::stof(val));
                else if (key == "move_speed") s.moveSpeed = std::clamp(std::stof(val), 1.0f, 16.0f);
                else if (key == "sprint_mult") s.sprintMult = std::clamp(std::stof(val), 1.0f, 3.0f);
                else if (key == "jump_speed") s.jumpSpeed = std::clamp(std::stof(val), 1.0f, 20.0f);
                else if (key == "lockpick_break_chance") s.lockpickBreakChance = std::clamp(std::stof(val), 0.0f, 1.0f);

                // Audio
                else if (key == "master_volume") s.masterVolume = std::clamp(std::stof(val), 0.0f, 2.0f);
                else if (key == "ambient_volume") s.ambientVolume = std::clamp(std::stof(val), 0.0f, 2.0f);
                else if (key == "echo_strength") s.echoStrength = std::clamp(std::stof(val), 0.0f, 2.0f);
                else if (key == "occlusion_strength") s.occlusionStrength = std::clamp(std::stof(val), 0.0f, 3.0f);
                else if (key == "sound_max_dist") s.soundMaxDist = std::clamp(std::stof(val), 6.0f, 150.0f);
                else if (key == "sound_distance_atten") s.soundDistanceAtten = std::clamp(std::stof(val), 0.001f, 1.0f);
                else if (key == "sound_obstacle_atten") s.soundObstacleAtten = std::clamp(std::stof(val), 0.0f, 3.0f);
                else if (key == "sound_obstacle_lpf") s.soundObstacleLpf = std::clamp(std::stof(val), 0.0f, 3.0f);
                else if (key == "vol_player_step") s.volPlayerStep = std::clamp(std::stof(val), 0.0f, 2.0f);
                else if (key == "vol_robot_step") s.volRobotStep = std::clamp(std::stof(val), 0.0f, 2.0f);
                else if (key == "vol_robot_alert") s.volRobotAlert = std::clamp(std::stof(val), 0.0f, 2.0f);
                else if (key == "vol_door_creak") s.volDoorCreak = std::clamp(std::stof(val), 0.0f, 2.0f);
                else if (key == "vol_door_impact") s.volDoorImpact = std::clamp(std::stof(val), 0.0f, 2.0f);
                else if (key == "vol_flashlight") s.volFlashlight = std::clamp(std::stof(val), 0.0f, 2.0f);
                else if (key == "vol_pickup") s.volPickup = std::clamp(std::stof(val), 0.0f, 2.0f);
                else if (key == "vol_player_hurt") s.volPlayerHurt = std::clamp(std::stof(val), 0.0f, 2.0f);
                else if (key == "vol_lock_pin_set") s.volLockPinSet = std::clamp(std::stof(val), 0.0f, 2.0f);
                else if (key == "vol_lock_break") s.volLockBreak = std::clamp(std::stof(val), 0.0f, 2.0f);
                else if (key == "vol_lock_open") s.volLockOpen = std::clamp(std::stof(val), 0.0f, 2.0f);
                else if (key == "vol_lockpick_snap") s.volLockpickSnap = std::clamp(std::stof(val), 0.0f, 2.0f);
                else if (key == "vol_weapon") s.volWeapon = std::clamp(std::stof(val), 0.0f, 2.0f);

                // World generation
                else if (key == "corridor_min_len") s.corridorMinLen = std::clamp(std::stoi(val), 16, 120);
                else if (key == "corridor_max_len") s.corridorMaxLen = std::clamp(std::stoi(val), 20, 120);
                else if (key == "corridor_min_width") s.corridorMinWidth = std::clamp(std::stoi(val), 2, 6);
                else if (key == "corridor_max_width") s.corridorMaxWidth = std::clamp(std::stoi(val), 2, 6);
                else if (key == "min_connections") s.minConnections = std::clamp(std::stoi(val), 1, 5);
                else if (key == "max_connections") s.maxConnections = std::clamp(std::stoi(val), 2, 6);
                else if (key == "min_parallel_spacing") s.minParallelSpacing = std::clamp(std::stoi(val), 10, 50);
                else if (key == "stream_radius") s.streamRadius = std::clamp(std::stoi(val), 1, 3);
            } catch (...) {
            }
        }
        return s;
    }
};

} // namespace mega
