#pragma once

#include <cstdint>

namespace manycast {

struct RateSample {
	std::uint64_t bytes = 0;
	int frames = 0;
};

struct Rates {
	double bitrateKbps = 0.0;
	double fps = 0.0;
};

/* counters that went backwards (the output was restarted) and non positive
 * intervals produce zeroes instead of negative or infinite values */
Rates computeRates(const RateSample &previous, const RateSample &current, double intervalSeconds);

} // namespace manycast
