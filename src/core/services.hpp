#pragma once

#include <string>
#include <vector>

namespace manycast {

struct NamedValue {
	std::string name;
	std::string value;
};

struct MainEncoderInfo {
	bool valid = false;
	int videoBitrate = 0;
	int audioBitrate = 0;
	int width = 0;
	int height = 0;
	std::string videoEncoderId;
};

std::vector<NamedValue> listPlatforms();
std::vector<NamedValue> listPlatformServers(const std::string &platform);
std::vector<NamedValue> listVideoEncoders();
std::string mainVideoEncoderId();
MainEncoderInfo mainEncoderInfo();

} // namespace manycast
