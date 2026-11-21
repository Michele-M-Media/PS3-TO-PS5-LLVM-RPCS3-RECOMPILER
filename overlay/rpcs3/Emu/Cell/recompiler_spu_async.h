#pragma once

#include <cstdint>
#include <limits>

#if defined(RPCS3_PS5) && defined(ARCH_X64)
#include "Utilities/File.h"
#include <cstdlib>
#include <string>
#endif

namespace recompiler::spu_async
{
	enum class mode { standard, hybrid, background };

	constexpr mode select(bool async, bool hybrid)
	{
		if (async)
			return mode::background;
		return hybrid ? mode::hybrid : mode::standard;
	}

	constexpr unsigned workers(mode current, unsigned usual)
	{
		return current == mode::background ? 1 : usual;
	}

	constexpr bool valid_redirect(std::int64_t rel)
	{
		return rel >= std::numeric_limits<std::int32_t>::min() && rel <= std::numeric_limits<std::int32_t>::max();
	}

#if defined(RPCS3_PS5) && defined(ARCH_X64)
	inline mode ps5_mode()
	{
		static const mode selected = []
		{
			const char* app_root = std::getenv("RPCS3_PS5_APP_ROOT");
			if (!app_root || !*app_root)
				return mode::standard;
			const std::string root(app_root);
			return select(fs::is_file(root + "/spu-async.enable"), fs::is_file(root + "/spu-hybrid.enable"));
		}();
		return selected;
	}
#endif
}
