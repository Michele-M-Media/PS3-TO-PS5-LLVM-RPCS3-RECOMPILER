#include "../overlay/rpcs3/Emu/RSX/VK/recompiler_shader_warmup_cache.h"

#include <cassert>
#include <fstream>

int main(int argc, char** argv)
{
	using namespace recompiler_shader_warmup;
	static_assert(sample_count_matches(0x81002c, 1));
	static_assert(sample_count_matches(0x682822c, 2));
	static_assert(!sample_count_matches(0x682822c, 1));
	static_assert(!sample_count_matches(0x683822c, 3));
	static_assert(!sample_count_matches(0x690822c, 16));

	constexpr profile device{0x1002, 0, 1, 1};
	const std::vector<std::uint64_t> old(8, 0x81002c);
	assert(decode<std::uint64_t>(device, encode<std::uint64_t>(device, old)) == old);

	std::vector<std::uint64_t> learned;
	for (std::uint64_t key = 0; key < 20; ++key)
		assert(remember(learned, key, 700000));
	assert(learned.size() == 20 && learned.front() == 0);
	assert(!remember(learned, std::uint64_t{0}, 700000));
	auto roundtrip = encode<std::uint64_t>(device, learned);
	assert(decode<std::uint64_t>(device, roundtrip) == learned);
	roundtrip.back() ^= std::byte{1};
	assert(!decode<std::uint64_t>(device, roundtrip));

	if (argc > 1)
	{
		using wire_record = std::array<std::byte, 456>;
		std::vector<std::byte> bytes(sizeof(file_header) + sizeof(wire_record) * 8);
		std::ifstream captured(argv[1], std::ios::binary);
		captured.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
		assert(captured && captured.peek() == std::ifstream::traits_type::eof());
		file_header header{};
		std::memcpy(&header, bytes.data(), sizeof(header));
		const auto records = decode<wire_record>(header.device, bytes);
		assert(records && records->size() == 8);
		assert(encode<wire_record>(header.device, *records) == bytes);
	}
}
