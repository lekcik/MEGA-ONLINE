#pragma once

#include "world/WorldGrid.hpp"
#include <cstdint>

namespace mega {

enum class BiomeType : uint8_t {
    UtilityComplex = 0,
    IndustrialFoundry,
    ReactorCore,
    HabitationHoneycomb,
    CorrosionSewers,
    BioMechanicalRift,
    MonumentalAtrium,
    COUNT
};

struct BiomePalette {
    BiomeType type;
    const char* name;
    BlockType wallBlock;
    BlockType floorBlock;
    BlockType ceilingBlock;
    BlockType trimBlock;
    BlockType bridgeBlock;
    uint8_t ambientR, ambientG, ambientB;
    uint8_t lampR, lampG, lampB;
};

inline const BiomePalette& getBiomePalette(BiomeType b) {
    static constexpr BiomePalette PALETTES[] = {
        {
            BiomeType::UtilityComplex, "UTILITY COMPLEX",
            BlockType::Concrete, BlockType::TechPanel, BlockType::DarkSteel,
            BlockType::RustPlate, BlockType::Grate,
            18, 22, 28,    // Dark cold ambient
            185, 225, 255  // Blue-white wall lamps
        },
        {
            BiomeType::IndustrialFoundry, "INDUSTRIAL FOUNDRY",
            BlockType::RustPlate, BlockType::TechPanel, BlockType::DarkSteel,
            BlockType::HazardStripe, BlockType::Grate,
            24, 20, 18,
            255, 175, 95
        },
        {
            BiomeType::ReactorCore, "REACTOR CORE",
            BlockType::TechPanel, BlockType::DarkSteel, BlockType::DarkSteel,
            BlockType::HazardStripe, BlockType::Grate,
            16, 24, 30,
            90, 235, 255
        },
        {
            BiomeType::HabitationHoneycomb, "HABITATION HONEYCOMB",
            BlockType::Concrete, BlockType::TechPanel, BlockType::DarkSteel,
            BlockType::DarkSteel, BlockType::Concrete,
            22, 22, 24,
            215, 230, 255
        },
        {
            BiomeType::CorrosionSewers, "CORROSION SEWERS",
            BlockType::RustPlate, BlockType::RustPlate, BlockType::Concrete,
            BlockType::HazardStripe, BlockType::Grate,
            16, 22, 16,
            145, 240, 85
        },
        {
            BiomeType::BioMechanicalRift, "BIO-MECHANICAL RIFT",
            BlockType::BioWall, BlockType::DarkSteel, BlockType::BioWall,
            BlockType::RustPlate, BlockType::Grate,
            24, 14, 16,
            255, 85, 75
        },
        {
            BiomeType::MonumentalAtrium, "MONUMENTAL ATRIUM",
            BlockType::DarkSteel, BlockType::TechPanel, BlockType::DarkSteel,
            BlockType::TechPanel, BlockType::Grate,
            20, 22, 28,
            200, 225, 255
        }
    };
    return PALETTES[static_cast<size_t>(b) % static_cast<size_t>(BiomeType::COUNT)];
}

} // namespace mega
