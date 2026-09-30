#pragma once

#include <obs.h>
#include <util/bmem.h>

#include <utility>

namespace manycast {

template<typename T, void (*ReleaseFn)(T *)> class Ref {
public:
	Ref() noexcept = default;
	explicit Ref(T *ptr) noexcept : ptr_(ptr) {}

	Ref(Ref &&other) noexcept : ptr_(std::exchange(other.ptr_, nullptr)) {}

	Ref &operator=(Ref &&other) noexcept
	{
		if (this != &other) {
			reset();
			ptr_ = std::exchange(other.ptr_, nullptr);
		}
		return *this;
	}

	Ref(const Ref &) = delete;
	Ref &operator=(const Ref &) = delete;

	~Ref() { reset(); }

	void reset(T *ptr = nullptr) noexcept
	{
		if (ptr_)
			ReleaseFn(ptr_);
		ptr_ = ptr;
	}

	T *get() const noexcept { return ptr_; }
	T *release() noexcept { return std::exchange(ptr_, nullptr); }
	explicit operator bool() const noexcept { return ptr_ != nullptr; }

private:
	T *ptr_ = nullptr;
};

using OutputRef = Ref<obs_output_t, obs_output_release>;
using ServiceRef = Ref<obs_service_t, obs_service_release>;
using EncoderRef = Ref<obs_encoder_t, obs_encoder_release>;
using DataRef = Ref<obs_data_t, obs_data_release>;
using DataArrayRef = Ref<obs_data_array_t, obs_data_array_release>;
using PropsRef = Ref<obs_properties_t, obs_properties_destroy>;
using CanvasRef = Ref<obs_canvas_t, obs_canvas_release>;
using SceneRef = Ref<obs_scene_t, obs_scene_release>;
using SourceRef = Ref<obs_source_t, obs_source_release>;
using SceneItemRef = Ref<obs_sceneitem_t, obs_sceneitem_release>;

class FreeBuffer {
public:
	FreeBuffer() noexcept = default;
	explicit FreeBuffer(char *ptr) noexcept : ptr_(ptr) {}
	FreeBuffer(const FreeBuffer &) = delete;
	FreeBuffer &operator=(const FreeBuffer &) = delete;
	~FreeBuffer()
	{
		if (ptr_)
			bfree(ptr_);
	}

	char *get() const noexcept { return ptr_; }
	explicit operator bool() const noexcept { return ptr_ != nullptr; }

private:
	char *ptr_ = nullptr;
};

} // namespace manycast
