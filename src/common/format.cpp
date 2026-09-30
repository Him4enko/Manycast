#include "common/format.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace manycast {

std::string makeId()
{
	static std::atomic<std::uint64_t> counter{0};
	const auto now = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
	const std::uint64_t value = now ^ (counter.fetch_add(1) << 17);

	char buffer[32] = {0};
	std::snprintf(buffer, sizeof(buffer), "%08x%08x", static_cast<unsigned>(value >> 32),
		      static_cast<unsigned>(value & 0xffffffffu));
	return buffer;
}

std::string maskStreamKey(const std::string &key)
{
	if (key.empty())
		return {};

	if (key.size() <= 4u)
		return std::string(key.size(), '*');

	return "****" + key.substr(key.size() - 4);
}

std::string formatDuration(double seconds)
{
	const double clamped = seconds > 0.0 ? seconds : 0.0;
	const long long total = static_cast<long long>(clamped);

	char buffer[32] = {0};
	std::snprintf(buffer, sizeof(buffer), "%02lld:%02lld:%02lld", total / 3600, (total % 3600) / 60, total % 60);
	return buffer;
}

int bitrateKbps(double kbps)
{
	const double value = kbps > 0.0 ? kbps : 0.0;
	return static_cast<int>(std::lround(value));
}

double bitrateMbps(double kbps)
{
	return (kbps > 0.0 ? kbps : 0.0) / 1000.0;
}

int framesPerSecond(double fps)
{
	const double value = fps > 0.0 ? fps : 0.0;
	return static_cast<int>(std::lround(value));
}

} // namespace manycast
