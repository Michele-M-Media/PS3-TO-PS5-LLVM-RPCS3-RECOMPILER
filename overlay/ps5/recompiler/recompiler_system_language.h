#pragma once

#include "yaml-cpp/yaml.h"

namespace recompiler_locale
{
inline constexpr int system_language_parameter = 1;

inline constexpr const char* guest_language(int result, int console_language)
{
    if (result == 0)
    {
        switch (console_language)
        {
        case 0: return "Japanese";
        case 2:
        case 22: return "French"; // France and Canada
        case 3:
        case 20: return "Spanish"; // Spain and Latin America
        case 4: return "German";
        case 5: return "Italian";
        default: break;
        }
    }
    // English on both console locales and when the title lacks the requested language.
    return "English (US)";
}

inline bool apply_system_language(YAML::Node& profile, const char* language)
{
    if (!profile.IsMap())
        return false;

    const auto system = static_cast<const YAML::Node&>(profile)["System"];
    if (system && !system.IsNull() && !system.IsMap())
        return false;

    profile["System"]["Language"] = language;
    return true;
}
}
