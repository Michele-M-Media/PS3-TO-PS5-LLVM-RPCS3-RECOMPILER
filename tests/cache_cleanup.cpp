#include "../overlay/ps5/recompiler/recompiler_cache_cleanup.h"

#include <cassert>
#include <fstream>

namespace fs = std::filesystem;
using recompiler_cache_cleanup::status;

static int marker_error;

extern "C" int __real_open(const char*, int, ...);
extern "C" int __wrap_open(const char* path, int flags, ...)
{
	if (marker_error && std::string_view(path).ends_with("/clear-cache.enable"))
	{
		errno = marker_error;
		return -1;
	}
	return __real_open(path, flags);
}

int main(int argc, char** argv)
{
	assert(argc == 2);
	const fs::path root(argv[1]);
	const fs::path app = root / "app";
	const fs::path data = root / "data";
	const auto title = data / "cache/cache/TEST00001";
	const auto other = data / "cache/cache/TEST00002";
	const auto shared = data / "cache/spu_progs";
	const auto save = data / "dev_hdd0/home/user/savedata";
	for (const auto& path : {app, title, other, shared, save})
		fs::create_directories(path);
	std::ofstream{title / "cached"};
	std::ofstream{other / "cached"};
	std::ofstream{save / "data"};
	fs::create_directory_symlink(other, title / "outside");
	int lookups = 0;
	const auto serial = [&] { ++lookups; return "TEST00001"; };
	const auto run = [&] { return recompiler_cache_cleanup::run(app, data, serial); };
	assert(run().state == status::normal_boot && lookups == 0);
	for (const int error : {EACCES, EPERM})
	{
		marker_error = error;
		const auto result = run();
		assert(result.state == status::request_unavailable && result.error.value() == error);
	}
	marker_error = 0;
	assert(lookups == 0 && fs::exists(title / "cached"));
	const auto marker = app / "clear-cache.enable";
	fs::create_symlink(other, marker);
	assert(run().state == status::failed && fs::exists(title));
	fs::remove(marker);
	std::ofstream{marker};
	assert(recompiler_cache_cleanup::run(app, data, [] { return "../save"; }).state == status::failed);
	assert(run().state == status::complete && lookups == 1);
	assert(!fs::exists(title) && fs::exists(other / "cached") && fs::exists(shared) && fs::exists(save / "data"));
	assert(run().removed == 0);
	fs::create_directory_symlink(other, title);
	assert(run().state == status::failed && fs::exists(other / "cached"));
}
