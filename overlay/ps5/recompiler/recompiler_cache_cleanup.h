#pragma once

#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace recompiler_cache_cleanup
{
	namespace fs = std::filesystem;

	enum class status { normal_boot, request_unavailable, complete, failed };
	struct result
	{
		status state;
		std::uintmax_t removed = 0;
		std::error_code error{};
	};

	inline bool valid_title_id(std::string_view title)
	{
		return title.size() == 9 &&
			std::all_of(title.begin(), title.begin() + 4, [](char c) { return c >= 'A' && c <= 'Z'; }) &&
			std::all_of(title.begin() + 4, title.end(), [](char c) { return c >= '0' && c <= '9'; });
	}

	template <typename ReadTitleID>
	result run(const fs::path& app, const fs::path& data, ReadTitleID read_title_id)
	{
		// Probe the optional request using the filesystem's normal access rules.
		const auto marker = app / "clear-cache.enable";
		const int fd = ::open(marker.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
		if (fd < 0)
		{
			const int reason = errno;
			if (reason == ENOENT)
				return {status::normal_boot};
			return {reason == ELOOP ? status::failed : status::request_unavailable, 0, {reason, std::generic_category()}};
		}
		struct stat request{};
		const int checked = ::fstat(fd, &request);
		const int reason = errno;
		::close(fd);
		if (checked != 0)
			return {status::request_unavailable, 0, {reason, std::generic_category()}};
		if (!S_ISREG(request.st_mode))
			return {status::failed, 0, std::make_error_code(std::errc::invalid_argument)};

		const auto title = read_title_id();
		if (!valid_title_id(title) || !data.is_absolute() || std::find(data.begin(), data.end(), "..") != data.end())
			return {status::failed, 0, std::make_error_code(std::errc::invalid_argument)};
		auto root = data.lexically_normal();
		if (root != root.root_path() && root.filename().empty())
			root = root.parent_path();
		if (root == root.root_path())
			return {status::failed, 0, std::make_error_code(std::errc::invalid_argument)};
		const auto cache = root / "cache";
		const auto titles = cache / "cache";
		const auto target = titles / title;
		std::error_code error;
		for (const auto& path : {root, cache, titles, target})
		{
			const auto type = fs::symlink_status(path, error);
			if (type.type() == fs::file_type::not_found && (!error || error == std::errc::no_such_file_or_directory))
				return {status::complete};
			if (error || !fs::is_directory(type))
				return {status::failed, 0, error ? error : std::make_error_code(std::errc::not_a_directory)};
		}
		// The caller holds the runtime lock. remove_all removes nested symlinks as links.
		const auto count = fs::remove_all(target, error);
		return error ? result{status::failed, 0, error} : result{status::complete, count};
	}
}
