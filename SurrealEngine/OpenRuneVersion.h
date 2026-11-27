#pragma once
#include <cstdint>
// Release identity is independent of the network wire protocol. Increment Build
// for each distributed APK; bump GameplayRulesRevision for simulation changes.
namespace OpenRuneBuild {
inline constexpr const char* Version = "1.0.2";
inline constexpr int Build = 38;
inline constexpr const char* Repository = "Mokomis/open-rune";
inline constexpr uint32_t GameplayRulesRevision = 42;
}
