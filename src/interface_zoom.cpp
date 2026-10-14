#include "interface_zoom.h"

#include <algorithm>
#include <cmath>

namespace imterm {

	float NormalizeInterfaceZoom(float aFactor)
	{
		if (!std::isfinite(aFactor) || aFactor <= 0.0f) {
			return DefaultInterfaceZoom;
		}

		return *std::min_element(
			InterfaceZoomLevels.begin(), InterfaceZoomLevels.end(),
			[aFactor](float aLeft, float aRight) {
				return std::abs(aLeft - aFactor) < std::abs(aRight - aFactor);
			});
	}

	float StepInterfaceZoom(float aFactor, int aDirection)
	{
		const float normalized = NormalizeInterfaceZoom(aFactor);
		const auto current = std::find(
			InterfaceZoomLevels.begin(), InterfaceZoomLevels.end(), normalized);
		if (aDirection > 0 && current + 1 != InterfaceZoomLevels.end()) {
			return *(current + 1);
		}
		if (aDirection < 0 && current != InterfaceZoomLevels.begin()) {
			return *(current - 1);
		}
		return normalized;
	}

}
