#pragma once

#include <array>

namespace imterm {

	inline constexpr float DefaultInterfaceZoom = 1.0f;

	inline constexpr std::array<float, 11> InterfaceZoomLevels = {
		0.50f, 0.67f, 0.75f, 0.80f, 0.90f, 1.00f,
		1.10f, 1.25f, 1.50f, 1.75f, 2.00f};

	float NormalizeInterfaceZoom(float aFactor);
	float StepInterfaceZoom(float aFactor, int aDirection);

}
