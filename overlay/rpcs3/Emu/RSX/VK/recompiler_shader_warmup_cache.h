#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

namespace recompiler_shader_warmup
{
	constexpr std::uint32_t capacity = 8;
	constexpr std::int64_t learn_threshold_us = 50'000;
	using profile = std::array<std::uint32_t, 8>;

	struct file_header
	{
		std::uint64_t magic;
		profile device;
		std::uint32_t record_size;
		std::uint32_t count;
		std::uint64_t checksum;
	};

	constexpr std::uint64_t magic = 0x31574d4952534141ULL;
	static_assert(sizeof(file_header) == 56);

	inline std::uint64_t checksum(std::span<const std::byte> data)
	{
		std::uint64_t hash = 14695981039346656037ULL;
		for (const auto value : data)
		{
			hash ^= std::to_integer<std::uint8_t>(value);
			hash *= 1099511628211ULL;
		}
		return hash;
	}

	template <typename Record>
	bool remember(std::vector<Record>& records, const Record& item, std::int64_t elapsed_us)
	{
		if (elapsed_us < learn_threshold_us || std::find(records.begin(), records.end(), item) != records.end())
			return false;
		if (records.size() == capacity)
			records.erase(records.begin());
		records.push_back(item);
		return true;
	}

	template <typename Record>
	std::vector<std::byte> encode(const profile& device, std::span<const Record> records)
	{
		static_assert(std::is_trivially_copyable_v<Record>);
		if (records.size() > capacity)
			return {};
		const auto payload = std::as_bytes(records);
		std::vector<std::byte> bytes(sizeof(file_header) + payload.size());
		const file_header header{magic, device, sizeof(Record), static_cast<std::uint32_t>(records.size()), checksum(payload)};
		std::memcpy(bytes.data(), &header, sizeof(header));
		if (!payload.empty())
			std::memcpy(bytes.data() + sizeof(header), payload.data(), payload.size());
		return bytes;
	}

	template <typename Record>
	std::optional<std::vector<Record>> decode(const profile& device, std::span<const std::byte> bytes)
	{
		static_assert(std::is_trivially_copyable_v<Record>);
		if (bytes.size() < sizeof(file_header))
			return std::nullopt;
		file_header header{};
		std::memcpy(&header, bytes.data(), sizeof(header));
		const auto payload = bytes.subspan(sizeof(header));
		if (header.magic != magic || header.device != device || header.record_size != sizeof(Record) ||
			header.count > capacity || payload.size() != header.count * sizeof(Record) || checksum(payload) != header.checksum)
		{
			return std::nullopt;
		}
		std::vector<Record> records(header.count);
		if (!payload.empty())
			std::memcpy(records.data(), payload.data(), payload.size());
		return records;
	}
}
