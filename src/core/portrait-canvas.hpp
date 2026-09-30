#pragma once

#include "common/obs-refs.hpp"
#include "core/config.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace manycast {

class PortraitCanvas {
public:
	static constexpr const char *kCanvasName = "ManyCast Portrait";
	/* the canvas was called differently before the plugin was renamed */
	static constexpr const char *kLegacyCanvasName = "LocalRestream Portrait";
	static constexpr const char *kDefaultSceneName = "Portrait";
	// obs_canvas_flags: ACTIVATE | SCENE_REF (no MIX_AUDIO: the audio of this
	// canvas must not be mixed into the main program audio again)
	static constexpr uint32_t kFlags = (1u << 1) | (1u << 3);

	PortraitCanvas() = default;
	~PortraitCanvas();

	PortraitCanvas(const PortraitCanvas &) = delete;
	PortraitCanvas &operator=(const PortraitCanvas &) = delete;

	void ensure();
	void release();

	bool ready() const { return canvas_ && scene_; }
	bool hasVideo() const;
	obs_canvas_t *canvas() const { return canvas_.get(); }
	obs_scene_t *scene() const { return scene_.get(); }
	video_t *video() const;

	uint32_t width() const;
	uint32_t height() const;
	bool setSize(uint32_t width, uint32_t height);

	std::string sceneName() const;
	std::vector<std::string> sceneNames() const;
	bool selectScene(const std::string &name);
	bool createSceneFromCurrent(std::string &error);
	bool createEmptyScene(std::string &error);
	bool removeScene(const std::string &name, std::string &error);

private:
	obs_canvas_t *findCanvasByName(const char *name) const;
	std::vector<SceneRef> collectScenes(obs_canvas_t *canvas) const;
	void rebuild();
	void restoreScenes();
	void bindScene();
	void adoptScene(obs_scene_t *scene);
	void adoptNewScene(obs_scene_t *scene);
	void rememberScene(const std::string &name);
	void save();

	CanvasRef canvas_;
	SceneRef scene_;
	PortraitConfig config_;
};

PortraitCanvas &portraitCanvas();

} // namespace manycast
