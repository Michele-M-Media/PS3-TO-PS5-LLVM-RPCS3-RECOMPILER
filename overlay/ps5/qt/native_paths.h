#pragma once
// Constructor-safe native paths: no Qt dependency and no frontend-owned global
// std::string has to have run its dynamic initializer before these functions.
#include <cstdio>
#include <cerrno>
#include <cstdlib>
#include <string>
#include <sys/stat.h>

extern "C" const char *sceKernelGetFsSandboxRandomWord();

namespace rpcs3::ps5::native_paths {
inline void ensure_directory(const std::string &path)
{
    struct stat info{};
    if ((::mkdir(path.c_str(), 0777) != 0 && errno != EEXIST)
        || ::stat(path.c_str(), &info) != 0 || !S_ISDIR(info.st_mode)) {
        std::fprintf(stderr, "RPCS3 PS5: data directory unavailable: %s (errno %d)\n", path.c_str(), errno);
        std::abort();
    }
}
inline std::string directory(std::string path)
{
    if (path.empty() || path.front() != '/') {
        std::fprintf(stderr, "RPCS3 PS5: expected an absolute native directory: %s\n", path.c_str());
        std::abort();
    }
    while (path.size() > 1 && path.back() == '/') path.pop_back();
    if (path.back() != '/') path += '/';
    return path;
}
inline const std::string &data_directory()
{
    static const std::string path = [] {
        const char *value = std::getenv("RPCS3_PS5_DATA_ROOT");
        auto root = directory(value && *value ? value : "/data/rpcs3-recompiler");
        ensure_directory(root);
        return root;
    }();
    return path;
}
inline const std::string &cache_directory()
{
    static const std::string path = [] {
        auto root = data_directory() + "cache/";
        ensure_directory(root);
        return root;
    }();
    return path;
}
inline const std::string &app_directory()
{
    static const std::string path = [] {
        auto exists = [](const std::string &candidate) {
            struct stat info{};
            return ::stat(candidate.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
        };
        const char *value = std::getenv("RPCS3_PS5_APP_ROOT");
        std::string root = value && *value ? value : "/app0";
        if (!exists(root)) {
            const char *word = sceKernelGetFsSandboxRandomWord();
            if (word && *word) root = std::string("/mnt/sandbox/") + word + "/app0";
            if (!exists(root)) {
                std::fprintf(stderr, "RPCS3 PS5: native application mount unavailable: %s\n", root.c_str());
                std::abort();
            }
        }
        return directory(root);
    }();
    return path;
}
}
