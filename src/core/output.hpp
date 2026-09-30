#pragma once

#include "common/stats.hpp"
#include "common/obs-refs.hpp"
#include "core/destination.hpp"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

namespace manycast {

enum class OutputState {
	Idle,
	Connecting,
	Live,
	Reconnecting,
	Stopping,
	Failed,
};

struct OutputStats {
	double seconds = 0.0;
	double bitrateKbps = 0.0;
	double fps = 0.0;
	int droppedFrames = 0;
	std::uint64_t totalBytes = 0;
};

class StreamOutput {
public:
	explicit StreamOutput(Destination destination);
	~StreamOutput();

	StreamOutput(const StreamOutput &) = delete;
	StreamOutput &operator=(const StreamOutput &) = delete;

	bool start(std::string &error);
	void requestStop(bool force = false);
	bool active() const;

	OutputState state() const;
	std::string message() const;
	OutputStats takeStats();

	const Destination &destination() const { return destination_; }

private:
	static void onStarting(void *data, calldata_t *params);
	static void onStarted(void *data, calldata_t *params);
	static void onStopping(void *data, calldata_t *params);
	static void onStopped(void *data, calldata_t *params);
	static void onReconnect(void *data, calldata_t *params);
	static void onReconnected(void *data, calldata_t *params);

	bool createService(std::string &error);
	bool createOutput(std::string &error);
	void applyVideoEncoderOverrides(obs_encoder_t *encoder) const;
	void prepareEncoderSettings(std::string &videoId, DataRef &videoSettings, DataRef &audioSettings,
				    bool &cloneMain) const;
	bool buildVideoSettings(std::string &videoId, DataRef &settings, bool &cloneMain) const;
	bool buildAudioSettings(DataRef &settings) const;
	bool createAudioEncoder(DataRef &settings, std::string &error);
	bool attachSharedEncoders(std::string &error);
	bool createOwnEncoders(std::string &error);
	bool attachPortraitEncoders(std::string &error);
	void connectSignals();
	void disconnectSignals();
	void releaseAll();
	void setState(OutputState state, std::string message);

	Destination destination_;

	ServiceRef service_;
	OutputRef output_;
	EncoderRef videoEncoder_;
	EncoderRef audioEncoder_;
	std::string sharedEncoderKey_;
	bool countedPortrait_ = false;
	bool signalsConnected_ = false;

	mutable std::mutex mutex_;
	OutputState state_ = OutputState::Idle;
	std::string message_;

	std::chrono::steady_clock::time_point startedAt_{};
	std::chrono::steady_clock::time_point lastSample_{};
	RateSample lastCounters_{};
};

void clearEncoderPool();
int portraitOutputsActive();

} // namespace manycast
