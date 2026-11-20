#pragma once

#include "ps5_sce.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>

namespace ps5
{
	// The native packet uses g and rad/s. Follow the existing RPCS3 DualSense
	// mapping into the DS3 axes, with only the Y (yaw) gyro exposed by cellPad.
	inline std::optional<std::array<uint16_t, 4>> sixaxis(const pad_sample& sample, float one_g)
	{
		const auto& accel = sample.acceleration_g;
		const auto& angular = sample.angular_velocity_rad;
		if (!std::isfinite(accel[0]) || !std::isfinite(accel[1]) || !std::isfinite(accel[2]) ||
		    !std::isfinite(angular[1]))
			return std::nullopt;
		if (accel[0] == 0.f && accel[1] == 0.f && accel[2] == 0.f && angular[1] == 0.f)
			return std::nullopt; // The driver has not supplied motion data yet.

		const auto quantize = [](float signed_units)
		{
			return static_cast<uint16_t>(std::clamp(512.f + signed_units, 0.f, 1023.f));
		};
		constexpr float yaw_scale = 246.f / std::numbers::pi_v<float>;
		return std::array{quantize(-accel[0] * one_g), quantize(-accel[1] * one_g),
		                  quantize(-accel[2] * one_g), quantize(-angular[1] * yaw_scale)};
	}
}
