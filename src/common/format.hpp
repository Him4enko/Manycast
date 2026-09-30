#pragma once

#include <cstdint>
#include <string>

namespace manycast {

std::string makeId();
std::string maskStreamKey(const std::string &key);
std::string formatDuration(double seconds);
int bitrateKbps(double kbps);
double bitrateMbps(double kbps);
int framesPerSecond(double fps);

} // namespace manycast
