#include "common/stats.hpp"

namespace manycast {

Rates computeRates(const RateSample &previous, const RateSample &current, double intervalSeconds)
{
	Rates rates;

	if (intervalSeconds <= 0.0)
		return rates;

	if (current.bytes > previous.bytes)
		rates.bitrateKbps =
			static_cast<double>(current.bytes - previous.bytes) * 8.0 / intervalSeconds / 1000.0;

	if (current.frames > previous.frames)
		rates.fps = static_cast<double>(current.frames - previous.frames) / intervalSeconds;

	return rates;
}

} // namespace manycast
