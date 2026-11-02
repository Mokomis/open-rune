#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace openrune {
// Stable wire IDs from the owned Rune Classic .int registrations. Never reorder.
// Player subclasses retain their original animation, sound and limb mappings.
struct Character { const char* className; const char* name; uint8_t skins; };
inline constexpr std::array<Character,36> Characters{{
    {"RuneI.Ragnar", "Leather Ragnar", 1},
    {"RuneI.PlayerTownRagnar", "Town Ragnar", 1},
    {"RuneI.RagnarSnow", "Snow Ragnar", 1},
    {"RuneI.TrialPitRagnar", "Trial Pit Ragnar", 1},
    {"RuneI.PlayerShipWreckRagnar", "Ship Wreck Ragnar", 1},
    {"RuneI.PlayerZombie", "Zombie", 1},
    {"RuneI.PlayerZombie2", "Zombie II", 1},
    {"RuneI.PlayerAlric", "Alric", 1},
    {"RuneI.PlayerBerserker", "Berserker", 1},
    {"RuneI.PlayerConrack", "Conrack", 1},
    {"RuneI.PlayerDarkViking", "Dark Viking", 1},
    {"RuneI.PlayerKarl", "Karl", 2},
    {"RuneI.PlayerLokiGuard", "Loki Guard", 1},
    {"RuneI.PlayerSigurd", "Sigurd", 1},
    {"RuneI.PlayerSven", "Sven", 1},
    {"RuneI.PlayerUlf", "Ulf", 1},
    {"RuneI.PlayerWolfgar", "Wolfgar", 1},
    {"RuneI.PlayerElder", "Elder", 1},
    {"RuneI.PlayerDarkWarrior", "Dark Warrior", 1},
    {"RuneI.PlayerValkyrie", "Valkyrie", 1},
    {"RuneI.PlayerSarkSpawn", "Sark Spawn", 1},
    {"RuneI.PlayerSarkSword", "Sark Sword", 1},
    {"RuneI.PlayerSarkAxe", "Sark Axe", 1},
    {"RuneI.PlayerSarkHammer", "Sark Hammer", 1},
    {"RuneI.PlayerSarkConrack", "Sark Conrack", 1},
    {"RuneI.PlayerSarkRagnar", "Sark Ragnar", 1},
    {"HallsOfValhalla.PlayerValkyrie", "Gold Valkyrie", 1},
    {"HallsOfValhalla.PlayerSnake", "Snake Berserker", 1},
    {"HallsOfValhalla.PlayerDwarf", "Dwarf", 1},
    {"HallsOfValhalla.PlayerPirate", "Swashbuckler", 1},
    {"HallsOfValhalla.PlayerMongol", "Mongol", 1},
    {"HallsOfValhalla.PlayerSyrian", "Syrian", 1},
    {"HallsOfValhalla.PlayerValkyrieWren", "Wren", 1},
    {"HallsOfValhalla.PlayerGladiator", "Remus", 1},
    {"HallsOfValhalla.PlayerGladiator2", "Romulus", 1},
    {"JunModel.PlayerJun", "Jun", 1},
}};
struct CharacterChoice {
    uint8_t character=0, skin=0;
    bool operator==(const CharacterChoice&) const = default;
};
constexpr bool validCharacter(CharacterChoice c) {
    return c.character<Characters.size() && c.skin<Characters[c.character].skins;
}
constexpr const char* skinName(CharacterChoice c) { return c.character==11 && c.skin==1?"Jarl":"Default"; }
}
