#pragma once

#include <vector>

// Decode the title's local WAV/ATRAC3+ boot track to 48 kHz stereo S16.
// Returns a negative FFmpeg error and leaves pcm untouched on failure.
int recompiler_decode_boot_pcm(const char* path, std::vector<char>& pcm);
