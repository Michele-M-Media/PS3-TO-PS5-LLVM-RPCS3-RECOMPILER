#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <string>
#include <unistd.h>

namespace rpcs3::ps5 {
void initialize_qt_platform();
void initialize_core_paths();
}

int recompiler_rpcs3_prepare_vfs(const char* data, const char* app) {
    if (!data || !app || data[0] != '/' || app[0] != '/' ||
        std::strpbrk(data, "\"\n\r\\") || std::strpbrk(app, "\"\n\r\\")) return 68;
    constexpr const char marker[] = "# RPCS3 Recompiler VFS v1\n";
    const std::string path = std::string(data) + "/vfs.yml";
    if (auto* old = std::fopen(path.c_str(), "rb")) {
        char prefix[sizeof marker]{};
        const auto n = std::fread(prefix, 1, sizeof marker - 1, old);
        std::fclose(old);
        if (n != sizeof marker - 1 || std::memcmp(prefix, marker, n)) return 69;
    } else if (errno != ENOENT) return 69;
    const std::string pending = path + ".recompiler-pending";
    auto* out = std::fopen(pending.c_str(), "wbx");
    if (!out) return 69;
    const int written = std::fprintf(out,
        "%s\"$(EmulatorDir)\": \"%s/\"\n\"/dev_hdd0/\": \"%s/dev_hdd0/\"\n"
        "\"/dev_hdd1/\": \"%s/dev_hdd1/\"\n\"/dev_flash/\": \"%s/dev_flash/\"\n",
        marker, data, data, data, app);
    const bool failed = written <= 0 || std::fflush(out) || ::fsync(::fileno(out));
    const bool closed = std::fclose(out) == 0;
    if (failed || !closed || std::rename(pending.c_str(), path.c_str())) {
        std::remove(pending.c_str());
        return 69;
    }
    return 0;
}
