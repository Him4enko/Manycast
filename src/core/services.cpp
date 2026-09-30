#include "core/services.hpp"
#include "common/obs-refs.hpp"

#include <obs-frontend-api.h>

#include <atomic>
#include <cstdio>
#include <string>

namespace manycast {

namespace {

std::atomic<unsigned> g_serviceCounter{0};

ServiceRef createProbeService()
{
	char name[64] = {0};
	std::snprintf(name, sizeof(name), "ManyCast probe %u", g_serviceCounter.fetch_add(1) + 1);
	return ServiceRef{obs_service_create_private("rtmp_common", name, nullptr)};
}

std::vector<NamedValue> readListProperty(obs_properties_t *props, const char *property)
{
	std::vector<NamedValue> result;
	obs_property_t *list = obs_properties_get(props, property);
	if (!list)
		return result;

	const std::size_t count = obs_property_list_item_count(list);
	result.reserve(count);
	for (std::size_t i = 0; i < count; i++) {
		const char *value = obs_property_list_item_string(list, i);
		if (!value || !*value)
			continue;
		const char *name = obs_property_list_item_name(list, i);
		result.push_back({name && *name ? name : value, value});
	}
	return result;
}

} // namespace

std::vector<NamedValue> listPlatforms()
{
	std::vector<NamedValue> result;
	ServiceRef service = createProbeService();
	if (!service)
		return result;

	PropsRef props{obs_service_properties(service.get())};
	if (!props)
		return result;

	return readListProperty(props.get(), "service");
}

std::vector<NamedValue> listPlatformServers(const std::string &platform)
{
	std::vector<NamedValue> result;
	if (platform.empty())
		return result;

	ServiceRef service = createProbeService();
	if (!service)
		return result;

	DataRef settings{obs_data_create()};
	obs_data_set_string(settings.get(), "service", platform.c_str());
	obs_service_update(service.get(), settings.get());

	PropsRef props{obs_service_properties(service.get())};
	if (!props)
		return result;

	return readListProperty(props.get(), "server");
}

std::vector<NamedValue> listVideoEncoders()
{
	std::vector<NamedValue> result;
	const char *id = nullptr;
	for (std::size_t i = 0; obs_enum_encoder_types(i, &id); i++) {
		if (!id || obs_get_encoder_type(id) != OBS_ENCODER_VIDEO)
			continue;
		const char *display = obs_encoder_get_display_name(id);
		result.push_back({display && *display ? display : id, id});
	}
	return result;
}

std::string mainVideoEncoderId()
{
	MainEncoderInfo info = mainEncoderInfo();
	return info.videoEncoderId;
}

MainEncoderInfo mainEncoderInfo()
{
	MainEncoderInfo info;

	OutputRef output{obs_frontend_get_streaming_output()};
	if (!output)
		return info;

	obs_encoder_t *video = obs_output_get_video_encoder(output.get());
	obs_encoder_t *audio = obs_output_get_audio_encoder(output.get(), 0);

	if (video) {
		DataRef settings{obs_encoder_get_settings(video)};
		if (settings)
			info.videoBitrate = static_cast<int>(obs_data_get_int(settings.get(), "bitrate"));

		info.width = static_cast<int>(obs_encoder_get_width(video));
		info.height = static_cast<int>(obs_encoder_get_height(video));

		const char *id = obs_encoder_get_id(video);
		if (id)
			info.videoEncoderId = id;
		info.valid = true;
	}

	if (audio) {
		DataRef settings{obs_encoder_get_settings(audio)};
		if (settings)
			info.audioBitrate = static_cast<int>(obs_data_get_int(settings.get(), "bitrate"));
	}

	return info;
}

} // namespace manycast
