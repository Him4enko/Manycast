#include "common/guard.hpp"
#include "common/plugin-state.hpp"
#include "core/portrait-canvas.hpp"
#include "ui/portrait-dock.hpp"
#include "ui/destinations-dock.hpp"
#include "core/output.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QMainWindow>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")
OBS_MODULE_AUTHOR("ManyCast")

namespace {

manycast::RestreamDock *g_dock = nullptr;
manycast::PortraitDock *g_portraitDock = nullptr;

void onFrontendEvent(enum obs_frontend_event event, void *private_data)
{
	manycast::guard("frontend event", [&]() {
		auto *dock = static_cast<manycast::RestreamDock *>(private_data);
		if (dock)
			dock->onFrontendEvent(static_cast<int>(event));

		switch (event) {
		case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
			manycast::portraitCanvas().ensure();
			if (g_portraitDock)
				g_portraitDock->refresh();
			break;
		case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
			/* stop everything before OBS tears the canvases down */
			if (dock)
				dock->shutdown();
			break;
		default:
			break;
		}
	});
}

bool loadPlugin()
{
	auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!mainWindow) {
		obs_log(LOG_ERROR, "main window is not available, plugin disabled");
		return false;
	}

	g_dock = new manycast::RestreamDock();
	if (!obs_frontend_add_dock_by_id("manycast-dock", obs_module_text("Dock.Title"), g_dock)) {
		delete g_dock;
		g_dock = nullptr;
		obs_log(LOG_ERROR, "could not register the dock widget");
		return false;
	}

	g_portraitDock = new manycast::PortraitDock(&manycast::portraitCanvas());
	if (!obs_frontend_add_dock_by_id("manycast-portrait-dock", obs_module_text("Portrait.Dock.Title"),
					 g_portraitDock)) {
		delete g_portraitDock;
		g_portraitDock = nullptr;
		obs_log(LOG_WARNING, "could not register the portrait dock widget");
	}

	obs_frontend_add_event_callback(onFrontendEvent, g_dock);

	obs_log(LOG_INFO, "ManyCast loaded (version %s)", PLUGIN_VERSION);
	return true;
}

void unloadPlugin()
{
	/* late widget destructors and pending timers must not touch libobs anymore */
	manycast::setShuttingDown(true);

	if (g_dock) {
		obs_frontend_remove_event_callback(onFrontendEvent, g_dock);
		manycast::guard("stop streams on unload", []() { g_dock->shutdown(); });
	}

	manycast::guard("clear encoder pool", []() { manycast::clearEncoderPool(); });
	manycast::guard("release portrait canvas", []() { manycast::portraitCanvas().release(); });

	g_portraitDock = nullptr;
	g_dock = nullptr;
}

} // namespace

bool obs_module_load(void)
{
	bool loaded = false;
	manycast::guard("obs_module_load", [&loaded]() { loaded = loadPlugin(); });
	return loaded;
}

void obs_module_unload(void)
{
	manycast::guard("obs_module_unload", []() { unloadPlugin(); });
	obs_log(LOG_INFO, "ManyCast unloaded");
}

const char *obs_module_description(void)
{
	return "Stream to several RTMP destinations at once without a restreaming service";
}
