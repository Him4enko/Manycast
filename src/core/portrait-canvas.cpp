#include "core/portrait-canvas.hpp"

#include "common/plugin-state.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <algorithm>
#include <cstring>
#include <utility>

namespace manycast {

namespace {

bool collectSceneNames(void *param, obs_source_t *source)
{
	auto *names = static_cast<std::vector<std::string> *>(param);
	const char *name = obs_source_get_name(source);
	if (name)
		names->push_back(name);
	return true;
}

bool collectSceneRefs(void *param, obs_source_t *source)
{
	auto *scenes = static_cast<std::vector<SceneRef> *>(param);
	obs_scene_t *scene = obs_scene_from_source(source);
	if (scene)
		scenes->push_back(SceneRef{obs_scene_get_ref(scene)});
	return true;
}

} // namespace

PortraitCanvas::~PortraitCanvas()
{
	if (isShuttingDown())
		return;

	release();
}

PortraitCanvas &portraitCanvas()
{
	static PortraitCanvas instance;
	return instance;
}

void PortraitCanvas::ensure()
{
	config_ = loadPortraitConfig();

	if (canvas_ && obs_canvas_removed(canvas_.get()))
		release();

	if (!canvas_) {
		CanvasRef existing{findCanvasByName(kCanvasName)};

		/* a canvas created before the plugin was renamed keeps its scenes */
		if (!existing) {
			existing.reset(findCanvasByName(kLegacyCanvasName));
			if (existing) {
				obs_canvas_set_name(existing.get(), kCanvasName);
				obs_log(LOG_INFO, "renamed the portrait canvas to \"%s\"", kCanvasName);
			}
		}

		if (existing)
			canvas_ = std::move(existing);
	}

	if (canvas_ && obs_canvas_has_video(canvas_.get())) {
		bindScene();
		return;
	}

	/* giving video to an existing canvas keeps its uuid, which the Twitch
	 * "Additional Canvas" setting stores; only possible while video is stopped */
	if (canvas_) {
		obs_video_info ovi;
		if (obs_get_video_info(&ovi)) {
			ovi.base_width = config_.width;
			ovi.base_height = config_.height;
			ovi.output_width = config_.width;
			ovi.output_height = config_.height;

			if (obs_canvas_reset_video(canvas_.get(), &ovi)) {
				obs_log(LOG_INFO, "portrait canvas: video attached to the existing canvas");
				bindScene();
				return;
			}
		}
		obs_log(LOG_INFO, "portrait canvas: rebuilding the canvas (the existing one has no video)");
	}

	/* a canvas restored from a scene collection has no video: OBS saves only
	 * name/uuid/flags, and obs_canvas_reset_video() refuses to run while the OBS
	 * video is already active - so rebuild the canvas and carry the scenes over */
	rebuild();
}

void PortraitCanvas::release()
{
	scene_.reset();
	canvas_.reset();
}

obs_canvas_t *PortraitCanvas::findCanvasByName(const char *name) const
{
	obs_canvas_t *found = nullptr;

	struct obs_frontend_canvas_list list = {};
	obs_frontend_get_canvases(&list);
	for (size_t i = 0; i < list.canvases.num; i++) {
		obs_canvas_t *canvas = list.canvases.array[i];
		const char *canvasName = obs_canvas_get_name(canvas);
		if (canvasName && std::strcmp(canvasName, name) == 0 && !obs_canvas_removed(canvas)) {
			found = obs_canvas_get_ref(canvas);
			break;
		}
	}
	obs_frontend_canvas_list_free(&list);

	return found;
}

std::vector<SceneRef> PortraitCanvas::collectScenes(obs_canvas_t *canvas) const
{
	std::vector<SceneRef> scenes;
	if (canvas)
		obs_canvas_enum_scenes(canvas, collectSceneRefs, &scenes);
	return scenes;
}

void PortraitCanvas::rebuild()
{
	std::vector<SceneRef> carried;
	if (canvas_ && !obs_canvas_removed(canvas_.get()))
		carried = collectScenes(canvas_.get());

	scene_.reset();
	if (canvas_) {
		obs_frontend_remove_canvas(canvas_.get());
		canvas_.reset();
	}

	obs_video_info ovi;
	if (!obs_get_video_info(&ovi))
		return;

	ovi.base_width = config_.width;
	ovi.base_height = config_.height;
	ovi.output_width = config_.width;
	ovi.output_height = config_.height;

	canvas_.reset(obs_frontend_add_canvas(kCanvasName, &ovi, kFlags));
	if (!canvas_)
		return;

	for (SceneRef &scene : carried)
		obs_canvas_move_scene(scene.get(), canvas_.get());

	restoreScenes();
	bindScene();
}

void PortraitCanvas::restoreScenes()
{
	if (!canvas_)
		return;

	std::vector<std::string> names = config_.scenes;
	std::reverse(names.begin(), names.end());

	const std::vector<std::string> current = sceneNames();

	for (const std::string &name : names) {
		if (name.empty())
			continue;

		if (std::find(current.begin(), current.end(), name) != current.end())
			continue;

		SourceRef source{obs_get_source_by_name(name.c_str())};
		if (!source)
			continue;

		obs_scene_t *scene = obs_scene_from_source(source.get());
		if (scene)
			obs_canvas_move_scene(scene, canvas_.get());
	}
}

void PortraitCanvas::bindScene()
{
	if (!canvas_)
		return;

	SourceRef channel{obs_canvas_get_channel(canvas_.get(), 0)};
	obs_scene_t *scene = channel ? obs_scene_from_source(channel.get()) : nullptr;
	if (scene) {
		adoptScene(scene);
		return;
	}

	const std::vector<std::string> names = sceneNames();
	if (!names.empty()) {
		std::string wanted = config_.currentScene;
		if (std::find(names.begin(), names.end(), wanted) == names.end())
			wanted = names.front();

		obs_scene_t *found = obs_canvas_get_scene_by_name(canvas_.get(), wanted.c_str());
		if (found) {
			obs_canvas_set_channel(canvas_.get(), 0, obs_scene_get_source(found));
			adoptScene(found);
			obs_scene_release(found);
			return;
		}
	}

	obs_scene_t *created = obs_canvas_scene_create(canvas_.get(), kDefaultSceneName);
	if (!created)
		return;

	obs_canvas_set_channel(canvas_.get(), 0, obs_scene_get_source(created));
	adoptScene(created);
	rememberScene(obs_source_get_name(obs_scene_get_source(created)));
	obs_scene_release(created);
}

void PortraitCanvas::adoptScene(obs_scene_t *scene)
{
	SceneRef ref{obs_scene_get_ref(scene)};
	scene_ = std::move(ref);
}

void PortraitCanvas::adoptNewScene(obs_scene_t *scene)
{
	obs_canvas_set_channel(canvas_.get(), 0, obs_scene_get_source(scene));
	adoptScene(scene);

	const char *name = obs_source_get_name(obs_scene_get_source(scene));
	rememberScene(name ? name : std::string());
	obs_scene_release(scene);

	save();
}

void PortraitCanvas::rememberScene(const std::string &name)
{
	if (name.empty())
		return;

	if (std::find(config_.scenes.begin(), config_.scenes.end(), name) == config_.scenes.end())
		config_.scenes.push_back(name);

	config_.currentScene = name;
}

void PortraitCanvas::save()
{
	savePortraitConfig(config_);
}

bool PortraitCanvas::hasVideo() const
{
	return canvas_ && obs_canvas_has_video(canvas_.get());
}

video_t *PortraitCanvas::video() const
{
	return canvas_ ? obs_canvas_get_video(canvas_.get()) : nullptr;
}

uint32_t PortraitCanvas::width() const
{
	obs_video_info ovi;
	if (canvas_ && obs_canvas_get_video_info(canvas_.get(), &ovi) && ovi.base_width)
		return ovi.base_width;
	return config_.width;
}

uint32_t PortraitCanvas::height() const
{
	obs_video_info ovi;
	if (canvas_ && obs_canvas_get_video_info(canvas_.get(), &ovi) && ovi.base_height)
		return ovi.base_height;
	return config_.height;
}

bool PortraitCanvas::setSize(uint32_t width, uint32_t height)
{
	if (!width || !height)
		return false;

	config_ = loadPortraitConfig();
	config_.width = width;
	config_.height = height;
	save();

	if (!canvas_)
		ensure();
	else
		rebuild();

	return canvas_ && obs_canvas_has_video(canvas_.get());
}

std::string PortraitCanvas::sceneName() const
{
	if (!scene_)
		return {};
	const char *name = obs_source_get_name(obs_scene_get_source(scene_.get()));
	return name ? std::string(name) : std::string();
}

std::vector<std::string> PortraitCanvas::sceneNames() const
{
	std::vector<std::string> names;
	if (canvas_)
		obs_canvas_enum_scenes(canvas_.get(), collectSceneNames, &names);
	return names;
}

bool PortraitCanvas::selectScene(const std::string &name)
{
	if (!canvas_ || name.empty())
		return false;

	obs_scene_t *scene = obs_canvas_get_scene_by_name(canvas_.get(), name.c_str());
	if (!scene)
		return false;

	obs_canvas_set_channel(canvas_.get(), 0, obs_scene_get_source(scene));
	adoptScene(scene);
	obs_scene_release(scene);

	rememberScene(name);
	save();
	return true;
}

bool PortraitCanvas::createSceneFromCurrent(std::string &error)
{
	if (!canvas_) {
		error = obs_module_text("Portrait.NoCanvas");
		return false;
	}

	SourceRef current{obs_frontend_get_current_scene()};
	if (!current) {
		error = obs_module_text("Portrait.NoCurrentScene");
		return false;
	}

	obs_scene_t *source = obs_scene_from_source(current.get());
	if (!source) {
		error = obs_module_text("Portrait.NoCurrentScene");
		return false;
	}

	const char *name = obs_source_get_name(current.get());
	obs_scene_t *duplicate = obs_scene_duplicate(source, name ? name : kDefaultSceneName, OBS_SCENE_DUP_REFS);
	if (!duplicate) {
		error = obs_module_text("Portrait.SceneFailed");
		return false;
	}

	obs_canvas_move_scene(duplicate, canvas_.get());
	adoptNewScene(duplicate);
	return true;
}

bool PortraitCanvas::removeScene(const std::string &name, std::string &error)
{
	if (!canvas_ || name.empty()) {
		error = obs_module_text("Portrait.NoCanvas");
		return false;
	}

	/* like OBS itself, never remove the last scene: the canvas would render nothing */
	if (sceneNames().size() <= 1) {
		error = obs_module_text("Portrait.LastScene");
		return false;
	}

	obs_scene_t *scene = obs_canvas_get_scene_by_name(canvas_.get(), name.c_str());
	if (!scene) {
		error = obs_module_text("Portrait.SceneFailed");
		return false;
	}

	const bool isCurrent = scene_ && obs_scene_get_source(scene_.get()) == obs_scene_get_source(scene);
	if (isCurrent) {
		obs_canvas_set_channel(canvas_.get(), 0, nullptr);
		scene_.reset();
	}

	obs_canvas_scene_remove(scene);
	obs_scene_release(scene);

	config_.scenes.erase(std::remove(config_.scenes.begin(), config_.scenes.end(), name), config_.scenes.end());
	if (config_.currentScene == name)
		config_.currentScene.clear();

	if (isCurrent)
		bindScene();

	save();
	return true;
}

bool PortraitCanvas::createEmptyScene(std::string &error)
{
	if (!canvas_) {
		error = obs_module_text("Portrait.NoCanvas");
		return false;
	}

	obs_scene_t *scene = obs_canvas_scene_create(canvas_.get(), kDefaultSceneName);
	if (!scene) {
		error = obs_module_text("Portrait.SceneFailed");
		return false;
	}

	adoptNewScene(scene);
	return true;
}

} // namespace manycast
