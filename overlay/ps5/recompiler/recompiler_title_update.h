#pragma once
#include <algorithm>

#include "Crypto/sha256.h"
#include "Loader/PSF.h"
#include "Utilities/File.h"

#include "recompiler_title_update_manifest.h"

namespace recompiler_title_update
{
	inline bool checked_transfer(fs::file& input, u64 offset, const asset& expected, fs::file* output = nullptr)
	{
		if (input.seek(offset) != offset)
			return false;
		mbedtls_sha256_context hash;
		mbedtls_sha256_init(&hash);
		mbedtls_sha256_starts_ret(&hash, 0);
		std::array<unsigned char, 65536> buffer;
		u64 remaining = expected.size;
		while (remaining)
		{
			const u64 count = std::min<u64>(remaining, buffer.size());
			if (input.read(buffer.data(), count) != count || (output && output->write(buffer.data(), count) != count))
			{
				mbedtls_sha256_free(&hash);
				return false;
			}
			mbedtls_sha256_update_ret(&hash, buffer.data(), count);
			remaining -= count;
		}
		std::array<unsigned char, 32> actual;
		mbedtls_sha256_finish_ret(&hash, actual.data());
		mbedtls_sha256_free(&hash);
		return actual == expected.hash;
	}

	inline bool verify_file(const std::string& path, const asset& expected)
	{
		fs::stat_t info{};
		if (!fs::get_stat(path, info) || info.is_directory || info.is_symlink || info.size != expected.size)
			return false;
		fs::file input(path);
		return input && checked_transfer(input, 0, expected);
	}

	inline bool install_asset(fs::file& source, const std::string& destination,
	                          const std::string& previous, const asset& expected)
	{
		if (verify_file(destination, expected))
			return true;
		fs::stat_t old{};
		if (fs::get_stat(destination, old))
		{
			if (old.is_symlink || old.is_directory)
				return false;
			if (!fs::is_file(previous))
			{
				if (!fs::create_path(fs::get_parent_dir(previous)))
					return false;
				fs::file input(destination);
				fs::pending_file backup(previous);
				if (!input || !backup.file)
					return false;
				std::array<unsigned char, 65536> buffer;
				u64 remaining = old.size;
				while (remaining)
				{
					const u64 count = std::min<u64>(remaining, buffer.size());
					if (input.read(buffer.data(), count) != count || backup.file.write(buffer.data(), count) != count)
						return false;
					remaining -= count;
				}
				if (!backup.commit(false))
					return false;
			}
		}
		else if (fs::g_tls_error != fs::error::noent)
			return false;

		if (!fs::create_path(fs::get_parent_dir(destination)))
			return false;
		fs::pending_file pending(destination);
		return pending.file && checked_transfer(source, expected.offset, expected, &pending.file) && pending.commit();
	}

	// Install the two boot files after the rest of the update data.
	// A stopped copy can resume by checking hashes and retaining prior backups.
	inline bool apply(const std::string& app, const std::string& data, const logs::channel& log)
	{
		const auto path = app + "/" + archive.path;
		fs::stat_t container{};
		if (!fs::get_stat(path, container))
		{
			log.error("RECOMPILER_TITLE_UPDATE: cannot read %s (%s)", archive.path, fs::g_tls_error);
			return false;
		}
		if (container.is_symlink || container.is_directory || container.size != archive.size)
		{
			log.error("RECOMPILER_TITLE_UPDATE: %s has an invalid size or type", archive.path);
			return false;
		}
		const auto disc = psf::load_object(app + "/PS3_GAME/PARAM.SFO");
		if (psf::get_string(disc, "TITLE_ID", "") != title_id)
		{
			log.error("RECOMPILER_TITLE_UPDATE: expected disc title %s", title_id);
			return false;
		}
		const auto destination = data + "/dev_hdd0/game/" + title_id + "/";
		const auto recovery = data + "/updates/" + title_id + "-" + update_version + "/";
		const auto stamp = recovery + "installed";
		const auto& sfo = assets.back();
		const std::string revision = std::string(title_id) + " " + update_version + "\n";
		bool stamped = false;
		fs::stat_t marker_info{};
		if (fs::get_stat(stamp, marker_info))
		{
			if (marker_info.is_directory || marker_info.is_symlink)
				return false;
			if (marker_info.size == revision.size())
			{
				fs::file input(stamp);
				std::string saved(revision.size(), '\0');
				stamped = input && input.read(saved.data(), saved.size()) == saved.size() && saved == revision;
			}
		}
		else if (fs::g_tls_error != fs::error::noent)
			return false;

		bool same_version = false;
		fs::stat_t metadata{};
		if (fs::get_stat(destination + sfo.path, metadata))
		{
			if (metadata.is_directory || metadata.is_symlink)
				return false;
			const auto installed = psf::load_object(destination + sfo.path);
			const auto version = psf::get_string(installed, "APP_VER", "");
			const auto serial = psf::get_string(installed, "TITLE_ID", "");
			if (psf::get_string(installed, "CATEGORY", "") != "GD" || serial != title_id || version > update_version)
			{
				log.error("RECOMPILER_TITLE_UPDATE: incompatible installed title or version");
				return false;
			}
			same_version = version == update_version;
		}
		else if (fs::g_tls_error != fs::error::noent)
			return false;

		// The guest rewrites localized fields and installation metadata in PARAM.SFO.
		// Keep those fields on an established installation, including during payload repair.
		const auto& eboot = assets[assets.size() - 2];
		const bool keep_sfo = same_version && (stamped || verify_file(destination + eboot.path, eboot));
		if (stamped && keep_sfo && std::all_of(assets.begin(), assets.end() - 1,
			[&](const asset& item) { return verify_file(destination + item.path, item); }))
		{
			log.notice("RECOMPILER_TITLE_UPDATE: %s %s ready", title_id, update_version);
			return true;
		}
		fs::file source(path);
		if (!source || !checked_transfer(source, 0, archive))
		{
			log.error("RECOMPILER_TITLE_UPDATE: %s failed SHA-256 verification", archive.path);
			return false;
		}
		log.notice("RECOMPILER_TITLE_UPDATE: %s verified, installing %s %s", archive.path, title_id, update_version);
		for (const auto& expected : assets)
		{
			if (&expected == &sfo && keep_sfo)
				continue;
			if (!install_asset(source, destination + expected.path,
			                   recovery + "previous/" + expected.path, expected))
			{
				log.error("RECOMPILER_TITLE_UPDATE: could not install %s", expected.path);
				return false;
			}
		}
		if (!fs::create_path(recovery))
			return false;
		fs::pending_file marker(stamp);
		if (!marker.file || marker.file.write(revision.data(), revision.size()) != revision.size() || !marker.commit())
			return false;
		log.notice("RECOMPILER_TITLE_UPDATE: %s %s staged and verified", title_id, update_version);
		return true;
	}
}
