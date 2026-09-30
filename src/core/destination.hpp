#pragma once

#include <cstdint>
#include <string>

namespace manycast {

enum class ServiceMode {
	Platform,
	Custom,
};

enum class EncoderMode {
	SharedMain,
	Own,
};

enum class CanvasMode {
	Main,
	Portrait,
};

struct Destination {
	std::string id;
	std::string name;

	ServiceMode serviceMode = ServiceMode::Platform;
	std::string platform;
	std::string serverName;
	std::string serverUrl;
	std::string streamKey;

	bool syncStart = true;
	bool syncStop = true;
	std::uint32_t delaySeconds = 0;
	bool delayPreserve = false;
	int reconnectRetries = 20;
	int reconnectDelaySeconds = 2;

	EncoderMode encoderMode = EncoderMode::SharedMain;
	CanvasMode canvasMode = CanvasMode::Main;
	std::string videoEncoderId;
	int videoBitrate = 0;
	int scaleWidth = 0;
	int scaleHeight = 0;
	int fpsDivisor = 1;
	int audioBitrate = 0;

	std::string serviceType() const { return serviceMode == ServiceMode::Platform ? "rtmp_common" : "rtmp_custom"; }

	std::string targetLabel() const
	{
		if (serviceMode == ServiceMode::Platform)
			return platform.empty() ? std::string("RTMP") : platform;
		return serverUrl;
	}
};

} // namespace manycast
