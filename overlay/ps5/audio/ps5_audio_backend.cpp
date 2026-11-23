#include "stdafx.h"
#include "ps5_audio_backend.h"
#include "ps5_sce.h"
#include "floating-stereo.h"
#include "native-reserve.h"
#include "elastic-stereo.h"
#include "bidirectional-gap.h"

LOG_CHANNEL(ps5_audio_log, "SceAudioOut");

ps5_audio_backend::ps5_audio_backend()
{
	// sceAudioOutInit may be called more than once; anything but a failure is fine
	if (const int res = sceAudioOutInit(); res < 0 && res != static_cast<int>(0x8026000e))
	{
		ps5_audio_log.error("sceAudioOutInit() failed: 0x%x", res);
		return;
	}

	m_initialized = true;
}

ps5_audio_backend::~ps5_audio_backend()
{
	Close();
}

bool ps5_audio_backend::Open(std::string_view /*dev_id*/, AudioFreq freq, AudioSampleSize sample_size, AudioChannelCnt ch_cnt, audio_channel_layout layout)
{
	if (!m_initialized)
		return false;

	Close();
	std::lock_guard lock{m_cb_mutex};

	if (freq != AudioFreq::FREQ_48K)
	{
		ps5_audio_log.error("Only 48 kHz output is supported (requested %u Hz)", static_cast<u32>(freq));
		return false;
	}

	m_sampling_rate = freq;
	m_sample_size = sample_size;

	// The port is stereo; RPCS3 downmixes 5.1/7.1 to it
	setup_channel_layout(static_cast<u32>(ch_cnt), 2, layout, ps5_audio_log);

	const bool s16 = get_convert_to_s16();
	const u32 format = s16 ? ps5::AUDIO_OUT_S16_STEREO : ps5::AUDIO_OUT_FLOAT_STEREO;

	m_port = sceAudioOutOpen(ps5::AUDIO_OUT_USER_SYSTEM, 0, 0, grain, 48000, format);

	if (m_port < 0)
	{
		ps5_audio_log.error("sceAudioOutOpen() failed: 0x%x", m_port);
		m_port = -1;
		return false;
	}

	m_buffer.assign(grain * get_channels() * get_sample_size(), 0);
	ps5_audio_log.notice("Opened port %d: %s stereo, grain %u", m_port, s16 ? "s16" : "float", grain);

	m_failed = false;
	m_stop = false;
	m_thread = std::make_unique<named_thread<std::function<void()>>>("SceAudioOut", [this]() { output_loop(); });
	return true;
}

void ps5_audio_backend::Close()
{
	if (m_thread)
	{
		m_stop = true;
		m_thread.reset(); // joins
	}

	std::lock_guard lock{m_cb_mutex};

	if (m_port >= 0)
	{
		sceAudioOutClose(m_port);
		m_port = -1;
	}

	m_playing = false;
}

void ps5_audio_backend::Play()
{
	std::lock_guard lock{m_cb_mutex};
	m_playing = true;
}

void ps5_audio_backend::Pause()
{
	std::lock_guard lock{m_cb_mutex};
	m_playing = false;
}

void ps5_audio_backend::output_loop()
{
	using clock = std::chrono::steady_clock;
	const u32 frame_bytes = get_sample_size() * get_channels();
	std::array<u8, sizeof(float) * 2> last_frame{};
	auto previous_callback = clock::time_point{};
	auto window_start = clock::now();
	u64 callbacks = 0, short_callbacks = 0, padded_frames = 0, busy_locks = 0, max_gap_us = 0;

	const bool float_output = get_sample_size() == sizeof(float);
	recompiler_audio::floating_stereo float_frames;
	recompiler_audio::output_levels levels;
	recompiler_audio::native_reserve reserve;
	recompiler_audio::elastic_stereo elastic;
	recompiler_audio::delayed_stereo_grain repair;
	u64 reconstructed_frames = 0;
	u64 stretched_callbacks = 0;
	float minimum_tempo = 1.0f;
	u64 priming_frames = 0;
	u64 source_frames = 0;
	u64 reserve_underruns = 0;

	while (!m_stop && thread_ctrl::state() != thread_state::aborting)
	{
		u32 written = 0;
		const u32 bytes = ::size32(m_buffer);
		bool active = false;
		bool reset_stream = false;
		bool initial_wait = false;
		const auto now = clock::now();

		{
			std::unique_lock lock(m_cb_mutex, std::defer_lock);

			if (!lock.try_lock_for(std::chrono::microseconds{50}))
			{
				++busy_locks;
			}
			else if (m_write_callback && m_playing)
			{
				active = true;
				if (float_output && bytes == recompiler_audio::native_reserve::grain_frames * frame_bytes)
				{
					elastic.update(reserve.buffered_frames(), reserve.reserve_frames());
					const float tempo = elastic.tempo();
					stretched_callbacks += tempo < 0.999f;
					minimum_tempo = std::min(minimum_tempo, tempo);
					const auto output = reserve.process(m_buffer.data(), [&](u8* dest, std::size_t requested)
					{
						return elastic.read(dest, requested, [&](u8* raw, std::size_t bytes)
						{
							const auto count = std::min(m_write_callback(static_cast<u32>(bytes), raw), static_cast<u32>(bytes));
							source_frames += count / frame_bytes;
							return count;
						});
					});
					written = static_cast<u32>(output.valid_bytes);
					initial_wait = output.initial_wait;
					priming_frames += output.initial_wait ? bytes / frame_bytes : 0;
					reserve_underruns += output.starved;
				}
				else
					written = std::min(m_write_callback(bytes, m_buffer.data()), bytes);
			}
			else
			{
				reset_stream = true;
			}
		}

		if (active)
		{
			++callbacks;
			if (previous_callback != clock::time_point{})
			{
				const auto gap = std::chrono::duration_cast<std::chrono::microseconds>(now - previous_callback).count();
				max_gap_us = std::max(max_gap_us, static_cast<u64>(gap));
			}
			previous_callback = now;
			written -= written % frame_bytes;
			if (float_output)
			{
				u32 repaired = 0;
				if (!initial_wait && bytes == recompiler_audio::delayed_stereo_grain::bytes)
				{
					const auto previous = repair.push(m_buffer.data(), written / frame_bytes);
					written = previous.valid * frame_bytes;
					repaired = previous.repaired;
					initial_wait = previous.priming;
				}
				reconstructed_frames += repaired;
				// Preserve the number of missing source samples for telemetry.
				float_frames.process(m_buffer.data(), written + repaired * frame_bytes, bytes, levels);
			}
			else if (written >= frame_bytes)
				std::memcpy(last_frame.data(), m_buffer.data() + written - frame_bytes, frame_bytes);
			if (written != bytes && !initial_wait)
			{
				++short_callbacks;
				padded_frames += (bytes - written) / frame_bytes;
				if (!float_output)
					for (u32 pos = written; pos < bytes; pos += frame_bytes)
						std::memcpy(m_buffer.data() + pos, last_frame.data(), frame_bytes);
			}
		}
		else
		{
			// Never carry a sample across pauses or unavailable callbacks.
			previous_callback = {};
			last_frame.fill(0);
			if (reset_stream)
			{
				float_frames = {};
				reserve.reset();
				elastic.reset();
				repair.reset();
			}
			std::memset(m_buffer.data(), 0, bytes);
		}

		// Blocks until the port takes the grain
		if (const int result = sceAudioOutOutput(m_port, m_buffer.data()); result < 0)
        {
            ps5_audio_log.error("sceAudioOutOutput failed: 0x%x", result);
            m_failed = true;
            std::lock_guard lock{m_state_cb_mutex};
            if (m_state_callback) m_state_callback(AudioStateEvent::UNSPECIFIED_ERROR);
            break;
        }

		if (now - window_start >= std::chrono::seconds{5})
		{
			if (callbacks || busy_locks)
			{
				ps5_audio_log.notice("RECOMPILER_AUDIO_WINDOW: callbacks=%llu short=%llu padded_frames=%llu lock_misses=%llu max_active_gap_us=%llu",
					callbacks, short_callbacks, padded_frames, busy_locks, max_gap_us);
			}
			if (float_output && callbacks)
			{
				ps5_audio_log.notice("RECOMPILER_AUDIO_LEVEL: peak_ratio=%.5f over_full_scale=%llu non_finite=%llu output_peak=%.5f limited_frames=%llu",
					levels.input_peak, levels.over_full_scale, levels.non_finite, levels.output_peak, levels.limited_frames);
			}
			if (float_output && callbacks)
			{
				ps5_audio_log.notice("RECOMPILER_AUDIO_RESERVE: target_frames=%u queued_frames=%llu source_frames=%llu prime_frames=%llu underruns=%llu continuous=1",
					reserve.reserve_frames(), static_cast<u64>(reserve.buffered_frames()), source_frames,
					priming_frames, reserve_underruns);
			}
			if (float_output && callbacks)
			{
				ps5_audio_log.notice("RECOMPILER_AUDIO_ELASTIC: tempo_milli=%u min_tempo_milli=%u stretched_callbacks=%llu",
					static_cast<u32>(elastic.tempo() * 1000), static_cast<u32>(minimum_tempo * 1000), stretched_callbacks);
			}
			if (float_output && padded_frames)
			{
				ps5_audio_log.notice("RECOMPILER_AUDIO_REPAIR: reconstructed_frames=%llu missing_frames=%llu",
					reconstructed_frames, padded_frames);
			}
			reconstructed_frames = 0;
			minimum_tempo = 1.0f;
			stretched_callbacks = 0;
			priming_frames = source_frames = reserve_underruns = 0;
			levels = {};
			window_start = now;
			callbacks = short_callbacks = padded_frames = busy_locks = max_gap_us = 0;
		}
	}

	// Flush: an output of null waits for the port to drain
	sceAudioOutOutput(m_port, nullptr);
}
