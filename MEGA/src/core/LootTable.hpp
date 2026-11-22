#pragma once

#include "core/Rng.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <string>

namespace mega {

enum class ItemType : int {
    Flashlight = 0,
    Battery    = 1,
    Food       = 2,
    Water      = 3,
    Lockpick   = 4,
    Broom      = 5, // 2 x 30 x 2 (breaks into Stick after 1..3 hits)
    Mop        = 6, // 2 x 30 x 2 (breaks into Stick after 1..3 hits)
    Tape       = 7, // 2 x 1 x 2  (electrical tape)
    Knife      = 8, // 1 x 6 x 1
    Stick      = 9, // 1 x 26 x 1 (broken broom/mop handle)
    Spear      = 10 // 1 x 32 x 1 (crafted from Stick + Tape + Knife)
};

inline constexpr int ITEM_TYPE_COUNT = 11;

inline bool isMeleeWeapon(ItemType t) {
    return t == ItemType::Broom || t == ItemType::Mop || t == ItemType::Stick ||
           t == ItemType::Knife || t == ItemType::Spear;
}

enum class LootSource : int {
    Crate        = 0,
    CabinetShelf = 1,
    ShelvingRack = 2,
    Locker       = 3,
    RareCabinet  = 4 // Rare cabinet inside 3x4 dead-end rooms with increased loot
};

inline constexpr int LOOT_SOURCE_COUNT = 5;

struct LootSourceTable {
    float spawnChance = 0.45f; // Chance that this surface spawns at least 1 item
    int maxItems = 2;          // Max items per surface
    // Relative weights for each ItemType:
    // Flashlight, Battery, Food, Water, Lockpick, Broom, Mop, Tape, Knife, Stick, Spear
    std::array<float, ITEM_TYPE_COUNT> weights = {
        0.04f, 0.22f, 0.25f, 0.22f, 0.14f, 0.03f, 0.03f, 0.04f, 0.03f, 0.0f, 0.0f
    };

    ItemType rollItem(Rng& rng) const {
        float sum = 0.0f;
        for (float w : weights) sum += std::max(0.0f, w);
        if (sum <= 1e-5f) return ItemType::Food;
        float r = rng.nextFloat() * sum;
        float acc = 0.0f;
        for (int i = 0; i < ITEM_TYPE_COUNT; ++i) {
            acc += std::max(0.0f, weights[i]);
            if (r <= acc) return static_cast<ItemType>(i);
        }
        return ItemType::Lockpick;
    }
};

struct LootTables {
    std::array<LootSourceTable, LOOT_SOURCE_COUNT> sources;

    LootTables() {
        // 0: Wooden Crates
        sources[static_cast<int>(LootSource::Crate)] = {
            0.48f, 2, {0.02f, 0.22f, 0.30f, 0.24f, 0.11f, 0.01f, 0.01f, 0.06f, 0.03f, 0.0f, 0.0f}
        };
        // 1: Standard Locked Metal Cabinet Shelves
        sources[static_cast<int>(LootSource::CabinetShelf)] = {
            0.68f, 2, {0.08f, 0.26f, 0.18f, 0.18f, 0.15f, 0.02f, 0.02f, 0.06f, 0.05f, 0.0f, 0.0f}
        };
        // 2: Open Shelving Racks
        sources[static_cast<int>(LootSource::ShelvingRack)] = {
            0.42f, 2, {0.03f, 0.20f, 0.26f, 0.24f, 0.13f, 0.02f, 0.02f, 0.06f, 0.04f, 0.0f, 0.0f}
        };
        // 3: Hideable Corridor Lockers
        sources[static_cast<int>(LootSource::Locker)] = {
            0.28f, 1, {0.02f, 0.18f, 0.14f, 0.16f, 0.36f, 0.03f, 0.03f, 0.05f, 0.03f, 0.0f, 0.0f}
        };
        // 4: Rare Cabinet inside 3x4 Dead-End Rooms (increased loot + broom/mop/tape/knife)
        sources[static_cast<int>(LootSource::RareCabinet)] = {
            0.88f, 3, {0.06f, 0.14f, 0.10f, 0.10f, 0.12f, 0.12f, 0.12f, 0.12f, 0.12f, 0.0f, 0.0f}
        };
    }

    const LootSourceTable& get(LootSource src) const {
        return sources[std::clamp(static_cast<int>(src), 0, LOOT_SOURCE_COUNT - 1)];
    }

    static void writeDefaultFile(const std::string& path) {
        std::ofstream out(path);
        if (!out) return;
        LootTables def;
        const char* names[LOOT_SOURCE_COUNT] = {
            "crate", "cabinet_shelf", "shelving_rack", "locker", "rare_cabinet"
        };
        const char* itemNames[ITEM_TYPE_COUNT] = {
            "flashlight", "battery", "food", "water", "lockpick",
            "broom", "mop", "tape", "knife", "stick", "spear"
        };

        out << "# MEGA // Loot Tables Configuration\n"
            << "# Press F5 to reload loot tables, or R to regenerate world with new loot.\n"
            << "# Format: <source>.<field> = <value>\n"
            << "# Sources: crate, cabinet_shelf, shelving_rack, locker, rare_cabinet\n\n";

        for (int s = 0; s < LOOT_SOURCE_COUNT; ++s) {
            const auto& t = def.sources[s];
            out << "# --- " << names[s] << " ---\n";
            out << names[s] << ".spawn_chance = " << t.spawnChance << "\n";
            out << names[s] << ".max_items = " << t.maxItems << "\n";
            for (int i = 0; i < ITEM_TYPE_COUNT; ++i) {
                out << names[s] << ".weight_" << itemNames[i] << " = " << t.weights[i] << "\n";
            }
            out << "\n";
        }
    }

    static LootTables loadOrCreate(const std::string& path = "loot_tables.cfg") {
        LootTables lt;
        std::ifstream in(path);
        if (!in.good()) {
            writeDefaultFile(path);
            return lt;
        }

        auto trim = [](std::string str) {
            size_t a = str.find_first_not_of(" \t\r\n");
            if (a == std::string::npos) return std::string{};
            size_t b = str.find_last_not_of(" \t\r\n");
            return str.substr(a, b - a + 1);
        };

        std::string line;
        while (std::getline(in, line)) {
            auto hash = line.find('#');
            if (hash != std::string::npos) line = line.substr(0, hash);
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;

            std::string key = trim(line.substr(0, eq));
            std::string val = trim(line.substr(eq + 1));
            auto dot = key.find('.');
            if (dot == std::string::npos || val.empty()) continue;

            std::string srcName = key.substr(0, dot);
            std::string field = key.substr(dot + 1);

            int srcIdx = -1;
            if (srcName == "crate") srcIdx = 0;
            else if (srcName == "cabinet_shelf") srcIdx = 1;
            else if (srcName == "shelving_rack") srcIdx = 2;
            else if (srcName == "locker") srcIdx = 3;
            else if (srcName == "rare_cabinet") srcIdx = 4;
            if (srcIdx < 0) continue;

            auto& tbl = lt.sources[srcIdx];
            try {
                if (field == "spawn_chance") tbl.spawnChance = std::clamp(std::stof(val), 0.0f, 1.0f);
                else if (field == "max_items") tbl.maxItems = std::clamp(std::stoi(val), 0, 8);
                else if (field == "weight_flashlight") tbl.weights[0] = std::max(0.0f, std::stof(val));
                else if (field == "weight_battery") tbl.weights[1] = std::max(0.0f, std::stof(val));
                else if (field == "weight_food") tbl.weights[2] = std::max(0.0f, std::stof(val));
                else if (field == "weight_water") tbl.weights[3] = std::max(0.0f, std::stof(val));
                else if (field == "weight_lockpick") tbl.weights[4] = std::max(0.0f, std::stof(val));
                else if (field == "weight_broom") tbl.weights[5] = std::max(0.0f, std::stof(val));
                else if (field == "weight_mop") tbl.weights[6] = std::max(0.0f, std::stof(val));
                else if (field == "weight_tape") tbl.weights[7] = std::max(0.0f, std::stof(val));
                else if (field == "weight_knife") tbl.weights[8] = std::max(0.0f, std::stof(val));
                else if (field == "weight_stick") tbl.weights[9] = std::max(0.0f, std::stof(val));
                else if (field == "weight_spear") tbl.weights[10] = std::max(0.0f, std::stof(val));
            } catch (...) {
            }
        }
        return lt;
    }
};

} // namespace mega
