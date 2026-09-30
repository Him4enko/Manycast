#include "core/config.hpp"
#include "common/format.hpp"
#include "common/obs-refs.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/platform.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>

namespace manycast {

namespace {

constexpr const char *kConfigFile = "manycast.json";
/* legacy plugin name, its settings are still imported */
constexpr const char *kLegacyModuleDir = "local-restream";
constexpr const char *kLegacyConfigFile = "local-restream.json";
constexpr const char *kTargetsKey = "targets";

std::string profileKey()
{
	char *profile = obs_frontend_get_current_profile();
	std::string key = profile ? profile : "default";
	bfree(profile);
	if (key.empty())
		key = "default";
	return key;
}

std::string directoryOf(const std::string &path)
{
	const std::size_t pos = path.find_last_of("/\\");
	return pos == std::string::npos ? std::string() : path.substr(0, pos);
}

Destination destinationFromData(obs_data_t *data)
{
	Destination dest;
	dest.id = obs_data_get_string(data, "id");
	if (dest.id.empty())
		dest.id = makeId();
	dest.name = obs_data_get_string(data, "name");
	dest.serviceMode = obs_data_get_string(data, "service_mode") == std::string("custom") ? ServiceMode::Custom
											      : ServiceMode::Platform;
	dest.platform = obs_data_get_string(data, "platform");
	dest.serverName = obs_data_get_string(data, "server_name");
	dest.serverUrl = obs_data_get_string(data, "server_url");
	dest.streamKey = obs_data_get_string(data, "stream_key");

	dest.syncStart = obs_data_get_bool(data, "sync_start");
	dest.syncStop = obs_data_get_bool(data, "sync_stop");
	dest.delaySeconds = static_cast<std::uint32_t>(obs_data_get_int(data, "delay_seconds"));
	dest.delayPreserve = obs_data_get_bool(data, "delay_preserve");
	dest.reconnectRetries = static_cast<int>(obs_data_get_int(data, "reconnect_retries"));
	dest.reconnectDelaySeconds = static_cast<int>(obs_data_get_int(data, "reconnect_delay"));

	dest.encoderMode = obs_data_get_string(data, "encoder_mode") == std::string("own") ? EncoderMode::Own
											   : EncoderMode::SharedMain;
	dest.canvasMode = obs_data_get_string(data, "canvas") == std::string("portrait") ? CanvasMode::Portrait
											 : CanvasMode::Main;
	dest.videoEncoderId = obs_data_get_string(data, "video_encoder");
	dest.videoBitrate = static_cast<int>(obs_data_get_int(data, "video_bitrate"));
	dest.scaleWidth = static_cast<int>(obs_data_get_int(data, "scale_width"));
	dest.scaleHeight = static_cast<int>(obs_data_get_int(data, "scale_height"));
	dest.fpsDivisor = static_cast<int>(obs_data_get_int(data, "fps_divisor"));
	dest.audioBitrate = static_cast<int>(obs_data_get_int(data, "audio_bitrate"));

	if (dest.reconnectRetries < 0)
		dest.reconnectRetries = 0;
	if (dest.reconnectDelaySeconds < 1)
		dest.reconnectDelaySeconds = 1;
	if (dest.fpsDivisor < 1)
		dest.fpsDivisor = 1;

	return dest;
}

DataRef destinationToData(const Destination &dest)
{
	DataRef data{obs_data_create()};
	obs_data_set_string(data.get(), "id", dest.id.c_str());
	obs_data_set_string(data.get(), "name", dest.name.c_str());
	obs_data_set_string(data.get(), "service_mode",
			    dest.serviceMode == ServiceMode::Custom ? "custom" : "platform");
	obs_data_set_string(data.get(), "platform", dest.platform.c_str());
	obs_data_set_string(data.get(), "server_name", dest.serverName.c_str());
	obs_data_set_string(data.get(), "server_url", dest.serverUrl.c_str());
	obs_data_set_string(data.get(), "stream_key", dest.streamKey.c_str());

	obs_data_set_bool(data.get(), "sync_start", dest.syncStart);
	obs_data_set_bool(data.get(), "sync_stop", dest.syncStop);
	obs_data_set_int(data.get(), "delay_seconds", dest.delaySeconds);
	obs_data_set_bool(data.get(), "delay_preserve", dest.delayPreserve);
	obs_data_set_int(data.get(), "reconnect_retries", dest.reconnectRetries);
	obs_data_set_int(data.get(), "reconnect_delay", dest.reconnectDelaySeconds);

	obs_data_set_string(data.get(), "encoder_mode", dest.encoderMode == EncoderMode::Own ? "own" : "shared");
	obs_data_set_string(data.get(), "canvas", dest.canvasMode == CanvasMode::Portrait ? "portrait" : "main");
	obs_data_set_string(data.get(), "video_encoder", dest.videoEncoderId.c_str());
	obs_data_set_int(data.get(), "video_bitrate", dest.videoBitrate);
	obs_data_set_int(data.get(), "scale_width", dest.scaleWidth);
	obs_data_set_int(data.get(), "scale_height", dest.scaleHeight);
	obs_data_set_int(data.get(), "fps_divisor", dest.fpsDivisor);
	obs_data_set_int(data.get(), "audio_bitrate", dest.audioBitrate);

	return data;
}

/* the config path of the legacy plugin name */
std::string legacyConfigPath()
{
	FreeBuffer path{obs_module_config_path(kConfigFile)};
	if (!path)
		return {};

	const std::string current{path.get()};
	const std::size_t filePos = current.rfind(kConfigFile);
	if (filePos == std::string::npos)
		return {};

	const std::size_t dirEnd = current.rfind('/', filePos);
	if (dirEnd == std::string::npos)
		return {};

	const std::size_t moduleStart = current.rfind('/', dirEnd - 1);
	if (moduleStart == std::string::npos || moduleStart >= dirEnd)
		return {};

	return current.substr(0, moduleStart + 1) + kLegacyModuleDir + "/" + kLegacyConfigFile;
}

DataRef loadRoot()
{
	FreeBuffer path{obs_module_config_path(kConfigFile)};
	if (!path) {
		blog(LOG_WARNING, "no config path available, settings will not be loaded or saved");
		return {};
	}

	DataRef root{obs_data_create_from_json_file_safe(path.get(), "bak")};

	/* first start after the rename */
	if (!root) {
		const std::string legacy = legacyConfigPath();
		if (!legacy.empty()) {
			DataRef old_root{obs_data_create_from_json_file_safe(legacy.c_str(), "bak")};
			if (old_root) {
				blog(LOG_INFO, "imported the settings of the previous plugin name (%s)",
				     legacy.c_str());
				return old_root;
			}
		}
	}

	if (!root && os_file_exists(path.get()))
		blog(LOG_WARNING, "could not read %s, falling back to defaults", kConfigFile);

	return root;
}

bool saveRoot(obs_data_t *root)
{
	FreeBuffer path{obs_module_config_path(kConfigFile)};
	if (!path) {
		blog(LOG_WARNING, "no config path available, settings are not saved");
		return false;
	}

	const std::string dir = directoryOf(path.get());
	if (!dir.empty())
		os_mkdirs(dir.c_str());

	if (!obs_data_save_json_safe(root, path.get(), "tmp", "bak")) {
		blog(LOG_WARNING, "could not save %s", kConfigFile);
		return false;
	}

	return true;
}

std::vector<DataRef> readObjectArray(obs_data_t *root, const char *key)
{
	std::vector<DataRef> items;

	DataArrayRef array{obs_data_get_array(root, key)};
	if (!array)
		return items;

	const std::size_t count = obs_data_array_count(array.get());
	items.reserve(count);
	for (std::size_t i = 0; i < count; i++) {
		DataRef item{obs_data_array_item(array.get(), i)};
		if (item)
			items.push_back(std::move(item));
	}

	return items;
}

void writeObjectArray(obs_data_t *root, const char *key, const std::vector<DataRef> &items)
{
	DataArrayRef array{obs_data_array_create()};
	for (const DataRef &item : items)
		obs_data_array_push_back(array.get(), item.get());

	obs_data_set_array(root, key, array.get());
}

} // namespace

std::vector<Destination> loadDestinations()
{
	std::vector<Destination> result;

	DataRef root = loadRoot();
	if (!root)
		return result;

	for (const DataRef &item : readObjectArray(root.get(), profileKey().c_str()))
		result.push_back(destinationFromData(item.get()));

	return result;
}

void saveDestinations(const std::vector<Destination> &destinations)
{
	DataRef root = loadRoot();
	if (!root)
		root.reset(obs_data_create());

	std::vector<DataRef> items;
	items.reserve(destinations.size());
	for (const Destination &destination : destinations)
		items.push_back(destinationToData(destination));

	writeObjectArray(root.get(), profileKey().c_str(), items);
	saveRoot(root.get());
}

PortraitConfig loadPortraitConfig()
{
	PortraitConfig config;

	DataRef root = loadRoot();
	if (!root)
		return config;

	const int width = static_cast<int>(obs_data_get_int(root.get(), "portrait_width"));
	const int height = static_cast<int>(obs_data_get_int(root.get(), "portrait_height"));
	if (width > 0 && height > 0) {
		config.width = static_cast<std::uint32_t>(width);
		config.height = static_cast<std::uint32_t>(height);
	}

	config.currentScene = obs_data_get_string(root.get(), "portrait_current_scene");

	for (const DataRef &item : readObjectArray(root.get(), "portrait_scenes")) {
		const char *name = obs_data_get_string(item.get(), "name");
		if (name && *name)
			config.scenes.push_back(name);
	}

	return config;
}

void savePortraitConfig(const PortraitConfig &config)
{
	DataRef root = loadRoot();
	if (!root)
		root.reset(obs_data_create());

	obs_data_set_int(root.get(), "portrait_width", config.width);
	obs_data_set_int(root.get(), "portrait_height", config.height);
	obs_data_set_string(root.get(), "portrait_current_scene", config.currentScene.c_str());

	std::vector<DataRef> scenes;
	scenes.reserve(config.scenes.size());
	for (const std::string &name : config.scenes) {
		DataRef item{obs_data_create()};
		obs_data_set_string(item.get(), "name", name.c_str());
		scenes.push_back(std::move(item));
	}

	writeObjectArray(root.get(), "portrait_scenes", scenes);
	saveRoot(root.get());
}

} // namespace manycast
