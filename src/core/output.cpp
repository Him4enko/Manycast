#include "core/output.hpp"

#include "common/guard.hpp"
#include "common/plugin-state.hpp"
#include "core/portrait-canvas.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <atomic>
#include <cstdio>
#include <functional>
#include <map>
#include <mutex>
#include <utility>

namespace manycast {

namespace {

constexpr const char *kOutputId = "rtmp_output";
constexpr const char *kAudioEncoderId = "ffmpeg_aac";

std::atomic<unsigned> g_outputCounter{0};

struct PooledEncoder {
	EncoderRef encoder;
	int users = 0;
};

std::map<std::string, PooledEncoder> g_encoderPool;
std::mutex g_encoderPoolMutex;
std::atomic<int> g_portraitActive{0};

obs_encoder_t *acquireSharedEncoder(const std::string &key, const std::function<EncoderRef()> &factory)
{
	std::lock_guard<std::mutex> lock(g_encoderPoolMutex);

	auto it = g_encoderPool.find(key);
	if (it == g_encoderPool.end()) {
		EncoderRef encoder = factory();
		if (!encoder)
			return nullptr;
		it = g_encoderPool.emplace(key, PooledEncoder{std::move(encoder), 0}).first;
	}

	it->second.users++;
	return it->second.encoder.get();
}

void releaseSharedEncoder(const std::string &key)
{
	if (key.empty())
		return;

	std::lock_guard<std::mutex> lock(g_encoderPoolMutex);

	auto it = g_encoderPool.find(key);
	if (it == g_encoderPool.end())
		return;

	if (--it->second.users <= 0)
		g_encoderPool.erase(it);
}

std::string stopCodeMessage(int code)
{
	switch (code) {
	case 0:
		return {};
	case -1:
		return obs_module_text("Error.WrongUrl");
	case -2:
		return obs_module_text("Error.ServerConnect");
	case -3:
		return obs_module_text("Error.ServerHandshake");
	case -4:
		return obs_module_text("Error.ServerRefuse");
	default:
		return obs_module_text("Error.Unknown");
	}
}

} // namespace

void clearEncoderPool()
{
	std::lock_guard<std::mutex> lock(g_encoderPoolMutex);
	g_encoderPool.clear();
}

int portraitOutputsActive()
{
	return g_portraitActive.load();
}

StreamOutput::StreamOutput(Destination destination) : destination_(std::move(destination)) {}

StreamOutput::~StreamOutput()
{
	if (isShuttingDown())
		return;

	releaseAll();
}

void StreamOutput::releaseAll()
{
	disconnectSignals();
	output_.reset();
	videoEncoder_.reset();
	audioEncoder_.reset();
	service_.reset();

	releaseSharedEncoder(sharedEncoderKey_);
	sharedEncoderKey_.clear();

	if (countedPortrait_) {
		g_portraitActive--;
		countedPortrait_ = false;
	}
}

bool StreamOutput::start(std::string &error)
{
	if (output_) {
		error = obs_module_text("Error.AlreadyRunning");
		return false;
	}

	startedAt_ = lastSample_ = std::chrono::steady_clock::now();
	lastCounters_ = {};
	setState(OutputState::Connecting, {});

	if (!createService(error)) {
		setState(OutputState::Failed, error);
		releaseAll();
		return false;
	}

	if (!createOutput(error)) {
		setState(OutputState::Failed, error);
		releaseAll();
		return false;
	}

	bool encodersReady = false;
	if (destination_.canvasMode == CanvasMode::Portrait)
		encodersReady = attachPortraitEncoders(error);
	else if (destination_.encoderMode == EncoderMode::SharedMain)
		encodersReady = attachSharedEncoders(error);
	else
		encodersReady = createOwnEncoders(error);

	if (!encodersReady) {
		setState(OutputState::Failed, error);
		releaseAll();
		return false;
	}

	obs_output_set_delay(output_.get(), destination_.delaySeconds,
			     destination_.delayPreserve ? OBS_OUTPUT_DELAY_PRESERVE : 0);
	obs_output_set_reconnect_settings(output_.get(), destination_.reconnectRetries,
					  destination_.reconnectDelaySeconds);

	connectSignals();

	if (!obs_output_start(output_.get())) {
		const char *lastError = obs_output_get_last_error(output_.get());
		error = lastError && *lastError ? lastError : obs_module_text("Error.StartOutput");
		setState(OutputState::Failed, error);
		releaseAll();
		return false;
	}

	return true;
}

void StreamOutput::requestStop(bool force)
{
	if (!output_)
		return;

	setState(OutputState::Stopping, {});
	if (force)
		obs_output_force_stop(output_.get());
	else
		obs_output_stop(output_.get());
}

bool StreamOutput::active() const
{
	return output_ && obs_output_active(output_.get());
}

OutputState StreamOutput::state() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return state_;
}

std::string StreamOutput::message() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return message_;
}

OutputStats StreamOutput::takeStats()
{
	OutputStats stats;
	if (!output_)
		return stats;

	const auto now = std::chrono::steady_clock::now();
	const RateSample current{obs_output_get_total_bytes(output_.get()),
				 static_cast<int>(obs_output_get_total_frames(output_.get()))};
	const double interval = std::chrono::duration<double>(now - lastSample_).count();
	const Rates rates = computeRates(lastCounters_, current, interval);

	stats.seconds = std::chrono::duration<double>(now - startedAt_).count();
	stats.totalBytes = current.bytes;
	stats.droppedFrames = obs_output_get_frames_dropped(output_.get());
	stats.bitrateKbps = rates.bitrateKbps;
	stats.fps = rates.fps;

	lastCounters_ = current;
	lastSample_ = now;
	return stats;
}

bool StreamOutput::createService(std::string &error)
{
	DataRef settings{obs_data_create()};

	if (destination_.serviceMode == ServiceMode::Platform) {
		if (destination_.platform.empty()) {
			error = obs_module_text("Error.NoPlatform");
			return false;
		}
		obs_data_set_string(settings.get(), "service", destination_.platform.c_str());
		if (!destination_.serverName.empty())
			obs_data_set_string(settings.get(), "server", destination_.serverName.c_str());
		obs_data_set_string(settings.get(), "key", destination_.streamKey.c_str());
	} else {
		if (destination_.serverUrl.empty()) {
			error = obs_module_text("Error.NoServer");
			return false;
		}
		obs_data_set_string(settings.get(), "server", destination_.serverUrl.c_str());
		obs_data_set_string(settings.get(), "key", destination_.streamKey.c_str());
	}

	const std::string label = destination_.name.empty() ? destination_.targetLabel() : destination_.name;
	const std::string name = "ManyCast service - " + label;

	service_.reset(obs_service_create(destination_.serviceType().c_str(), name.c_str(), settings.get(), nullptr));
	if (!service_) {
		error = obs_module_text("Error.CreateService");
		return false;
	}
	return true;
}

bool StreamOutput::createOutput(std::string &error)
{
	const char *outputId = kOutputId;
	const char *preferred = obs_service_get_preferred_output_type(service_.get());
	if (preferred && *preferred)
		outputId = preferred;

	DataRef settings{obs_output_defaults(outputId)};
	if (!settings)
		settings.reset(obs_data_create());

	const std::string label = destination_.name.empty() ? destination_.targetLabel() : destination_.name;
	char name[256] = {0};
	std::snprintf(name, sizeof(name), "ManyCast %s (%u)", label.c_str(), g_outputCounter.fetch_add(1) + 1);

	output_.reset(obs_output_create(outputId, name, settings.get(), nullptr));
	if (!output_) {
		error = obs_module_text("Error.CreateOutput");
		return false;
	}

	obs_output_set_service(output_.get(), service_.get());
	return true;
}

bool StreamOutput::buildVideoSettings(std::string &videoId, DataRef &settings, bool &cloneMain) const
{
	OutputRef mainOutput{obs_frontend_get_streaming_output()};
	obs_encoder_t *mainVideo = mainOutput ? obs_output_get_video_encoder(mainOutput.get()) : nullptr;
	const char *mainVideoId = mainVideo ? obs_encoder_get_id(mainVideo) : nullptr;

	videoId = destination_.videoEncoderId;
	cloneMain = mainVideo && (videoId.empty() || (mainVideoId && videoId == mainVideoId));
	if (cloneMain)
		videoId = mainVideoId ? mainVideoId : "obs_x264";
	if (videoId.empty())
		videoId = "obs_x264";

	settings = cloneMain && mainVideo ? DataRef{obs_encoder_get_settings(mainVideo)}
					  : DataRef{obs_encoder_defaults(videoId.c_str())};
	if (!settings)
		settings.reset(obs_data_create());

	if (destination_.videoBitrate > 0)
		obs_data_set_int(settings.get(), "bitrate", destination_.videoBitrate);

	return true;
}

bool StreamOutput::buildAudioSettings(DataRef &settings) const
{
	OutputRef mainOutput{obs_frontend_get_streaming_output()};
	obs_encoder_t *mainAudio = mainOutput ? obs_output_get_audio_encoder(mainOutput.get(), 0) : nullptr;

	settings = mainAudio ? DataRef{obs_encoder_get_settings(mainAudio)}
			     : DataRef{obs_encoder_defaults(kAudioEncoderId)};
	if (!settings)
		settings.reset(obs_data_create());

	if (destination_.audioBitrate > 0)
		obs_data_set_int(settings.get(), "bitrate", destination_.audioBitrate);

	return true;
}

bool StreamOutput::createAudioEncoder(DataRef &settings, std::string &error)
{
	char name[128] = {0};
	std::snprintf(name, sizeof(name), "ManyCast audio %s", destination_.id.c_str());

	audioEncoder_.reset(obs_audio_encoder_create(kAudioEncoderId, name, settings.get(), 0, nullptr));
	if (!audioEncoder_) {
		error = obs_module_text("Error.CreateEncoder");
		return false;
	}

	obs_encoder_set_audio(audioEncoder_.get(), obs_get_audio());
	obs_output_set_audio_encoder(output_.get(), audioEncoder_.get(), 0);
	return true;
}

bool StreamOutput::attachSharedEncoders(std::string &error)
{
	OutputRef mainOutput{obs_frontend_get_streaming_output()};
	obs_encoder_t *videoEncoder = mainOutput ? obs_output_get_video_encoder(mainOutput.get()) : nullptr;
	obs_encoder_t *audioEncoder = mainOutput ? obs_output_get_audio_encoder(mainOutput.get(), 0) : nullptr;

	if (!videoEncoder || !audioEncoder) {
		error = obs_module_text("Error.NoMainEncoder");
		return false;
	}

	obs_output_set_video_encoder(output_.get(), videoEncoder);
	obs_output_set_audio_encoder(output_.get(), audioEncoder, 0);
	return true;
}

/* settings of the video and audio encoder of this destination; a service may
 * cap them, unless they are a copy of the main stream anyway */
void StreamOutput::prepareEncoderSettings(std::string &videoId, DataRef &videoSettings, DataRef &audioSettings,
					  bool &cloneMain) const
{
	buildVideoSettings(videoId, videoSettings, cloneMain);
	buildAudioSettings(audioSettings);

	if (!cloneMain)
		obs_service_apply_encoder_settings(service_.get(), videoSettings.get(), audioSettings.get());
}

void StreamOutput::applyVideoEncoderOverrides(obs_encoder_t *encoder) const
{
	if (!encoder)
		return;

	if (destination_.scaleWidth > 0 && destination_.scaleHeight > 0) {
		obs_encoder_set_scaled_size(encoder, static_cast<std::uint32_t>(destination_.scaleWidth),
					    static_cast<std::uint32_t>(destination_.scaleHeight));
		obs_encoder_set_gpu_scale_type(encoder, OBS_SCALE_BICUBIC);
	}

	if (destination_.fpsDivisor > 1)
		obs_encoder_set_frame_rate_divisor(encoder, static_cast<std::uint32_t>(destination_.fpsDivisor));
}

bool StreamOutput::createOwnEncoders(std::string &error)
{
	std::string videoId;
	DataRef videoSettings;
	DataRef audioSettings;
	bool cloneMain = false;
	prepareEncoderSettings(videoId, videoSettings, audioSettings, cloneMain);

	char videoName[128] = {0};
	std::snprintf(videoName, sizeof(videoName), "ManyCast video %s", destination_.id.c_str());
	videoEncoder_.reset(obs_video_encoder_create(videoId.c_str(), videoName, videoSettings.get(), nullptr));
	if (!videoEncoder_) {
		error = obs_module_text("Error.CreateEncoder");
		return false;
	}
	obs_encoder_set_video(videoEncoder_.get(), obs_get_video());
	applyVideoEncoderOverrides(videoEncoder_.get());

	obs_output_set_video_encoder(output_.get(), videoEncoder_.get());
	return createAudioEncoder(audioSettings, error);
}

bool StreamOutput::attachPortraitEncoders(std::string &error)
{
	PortraitCanvas &portrait = portraitCanvas();
	if (!portrait.ready() || !portrait.hasVideo() || !portrait.video()) {
		error = obs_module_text("Error.NoPortraitCanvas");
		return false;
	}

	std::string videoId;
	DataRef videoSettings;
	DataRef audioSettings;
	bool cloneMain = false;
	prepareEncoderSettings(videoId, videoSettings, audioSettings, cloneMain);

	const char *uuid = obs_canvas_get_uuid(portrait.canvas());
	const char *json = obs_data_get_json(videoSettings.get());
	sharedEncoderKey_ = std::string("portrait|") + (uuid ? uuid : "?") + "|" + videoId + "|" +
			    std::to_string(destination_.scaleWidth) + "x" + std::to_string(destination_.scaleHeight) +
			    "|" + std::to_string(destination_.fpsDivisor) + "|" + (json ? json : "");

	obs_encoder_t *shared = acquireSharedEncoder(sharedEncoderKey_, [&]() -> EncoderRef {
		char name[160] = {0};
		std::snprintf(name, sizeof(name), "ManyCast portrait %s", destination_.id.c_str());

		EncoderRef encoder{obs_video_encoder_create(videoId.c_str(), name, videoSettings.get(), nullptr)};
		if (!encoder)
			return {};

		obs_encoder_set_video(encoder.get(), portrait.video());
		applyVideoEncoderOverrides(encoder.get());

		return encoder;
	});

	if (!shared) {
		releaseSharedEncoder(sharedEncoderKey_);
		sharedEncoderKey_.clear();
		error = obs_module_text("Error.CreateEncoder");
		return false;
	}

	obs_output_set_video_encoder(output_.get(), shared);
	g_portraitActive++;
	countedPortrait_ = true;
	return createAudioEncoder(audioSettings, error);
}

void StreamOutput::connectSignals()
{
	if (!output_ || signalsConnected_)
		return;

	signal_handler_t *handler = obs_output_get_signal_handler(output_.get());
	if (!handler)
		return;

	signal_handler_connect(handler, "starting", &StreamOutput::onStarting, this);
	signal_handler_connect(handler, "start", &StreamOutput::onStarted, this);
	signal_handler_connect(handler, "stopping", &StreamOutput::onStopping, this);
	signal_handler_connect(handler, "stop", &StreamOutput::onStopped, this);
	signal_handler_connect(handler, "reconnect", &StreamOutput::onReconnect, this);
	signal_handler_connect(handler, "reconnect_success", &StreamOutput::onReconnected, this);
	signalsConnected_ = true;
}

void StreamOutput::disconnectSignals()
{
	if (!output_ || !signalsConnected_)
		return;

	signal_handler_t *handler = obs_output_get_signal_handler(output_.get());
	if (handler) {
		signal_handler_disconnect(handler, "starting", &StreamOutput::onStarting, this);
		signal_handler_disconnect(handler, "start", &StreamOutput::onStarted, this);
		signal_handler_disconnect(handler, "stopping", &StreamOutput::onStopping, this);
		signal_handler_disconnect(handler, "stop", &StreamOutput::onStopped, this);
		signal_handler_disconnect(handler, "reconnect", &StreamOutput::onReconnect, this);
		signal_handler_disconnect(handler, "reconnect_success", &StreamOutput::onReconnected, this);
	}
	signalsConnected_ = false;
}

void StreamOutput::setState(OutputState state, std::string message)
{
	std::lock_guard<std::mutex> lock(mutex_);
	state_ = state;
	message_ = std::move(message);
}

void StreamOutput::onStarting(void *data, calldata_t *)
{
	guard("output starting",
	      [data]() { static_cast<StreamOutput *>(data)->setState(OutputState::Connecting, {}); });
}

void StreamOutput::onStarted(void *data, calldata_t *)
{
	guard("output started", [data]() {
		StreamOutput *self = static_cast<StreamOutput *>(data);
		self->lastSample_ = std::chrono::steady_clock::now();
		self->lastCounters_ = {};
		self->setState(OutputState::Live, {});
	});
}

void StreamOutput::onStopping(void *data, calldata_t *)
{
	guard("output stopping", [data]() { static_cast<StreamOutput *>(data)->setState(OutputState::Stopping, {}); });
}

void StreamOutput::onStopped(void *data, calldata_t *params)
{
	guard("output stopped", [data, params]() {
		StreamOutput *self = static_cast<StreamOutput *>(data);
		const int code = static_cast<int>(calldata_int(params, "code"));
		const std::string message = stopCodeMessage(code);

		if (code != 0)
			blog(LOG_WARNING, "[manycast] destination \"%s\" stopped with code %d (%s)",
			     self->destination_.name.c_str(), code, message.c_str());

		self->setState(code == 0 ? OutputState::Idle : OutputState::Failed, message);
	});
}

void StreamOutput::onReconnect(void *data, calldata_t *)
{
	guard("output reconnect",
	      [data]() { static_cast<StreamOutput *>(data)->setState(OutputState::Reconnecting, {}); });
}

void StreamOutput::onReconnected(void *data, calldata_t *)
{
	guard("output reconnected", [data]() {
		StreamOutput *self = static_cast<StreamOutput *>(data);
		self->lastSample_ = std::chrono::steady_clock::now();
		self->lastCounters_ = {};
		self->setState(OutputState::Live, {});
	});
}

} // namespace manycast
