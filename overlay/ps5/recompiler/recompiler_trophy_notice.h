#pragma once

#include <cstddef>
#include <cstring>
#include <string_view>

namespace recompiler_trophy
{
// Text-only notification packet used by the native PS5 SDK examples.
struct request
{
    char header[45]{};
    char message[1024]{};
    char tail[2051]{};
};

static_assert(offsetof(request, message) == 45);
static_assert(sizeof(request) == 3120);

using sender = int (*)(int, request*, std::size_t, int);

inline std::string_view heading(int language)
{
    switch (language)
    {
    case 0: return "PS3 TO PS5 LLVM RPCS3 RECOMPILER\nトロフィー獲得 ";
    case 2: return "PS3 TO PS5 LLVM RPCS3 RECOMPILER\nTrophée obtenu ";
    case 3: return "PS3 TO PS5 LLVM RPCS3 RECOMPILER\nTrofeo conseguido ";
    case 4: return "PS3 TO PS5 LLVM RPCS3 RECOMPILER\nTrophäe erhalten ";
    case 5: return "PS3 TO PS5 LLVM RPCS3 RECOMPILER\nTrofeo sbloccato ";
    default: return "PS3 TO PS5 LLVM RPCS3 RECOMPILER\nTrophy earned ";
    }
}

inline bool submit(sender send, std::string_view name, int language)
{
    if (!send || name.empty() || name.size() > 128)
        return false;

    request packet;
    const std::string_view prefix = heading(language);
    std::memcpy(packet.message, prefix.data(), prefix.size());
    std::size_t length = prefix.size();
    for (char raw : name)
    {
        const auto byte = static_cast<unsigned char>(raw);
        if (byte >= 0x20 && byte != 0x7f)
            packet.message[length++] = raw;
    }
    return length > prefix.size() && send(0, &packet, sizeof(packet), 0) == 0;
}
}
