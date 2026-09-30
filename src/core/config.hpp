#pragma once

#include "core/destination.hpp"

#include <obs.h>

#include <cstdint>
#include <string>
#include <vector>

namespace manycast {

std::vector<Destination> loadDestinations();
void saveDestinations(const std::vector<Destination> &destinations);

struct PortraitConfig {
	std::uint32_t width = 1080;
	std::uint32_t height = 1920;
	std::vector<std::string> scenes;
	std::string currentScene;
};

PortraitConfig loadPortraitConfig();
void savePortraitConfig(const PortraitConfig &config);

} // namespace manycast
