#pragma once

#include "common/obs-refs.hpp"

#include <QPointF>
#include <QWidget>

#include <functional>
#include <string>
#include <vector>

#include <graphics/vec2.h>
#include <obs.h>

namespace manycast {

class PortraitCanvas;

class PortraitPreview : public QWidget {
public:
	struct Box {
		float x = 0.0f;
		float y = 0.0f;
		float width = 0.0f;
		float height = 0.0f;
		bool valid = false;
	};

	struct SceneItemInfo {
		obs_sceneitem_t *item = nullptr;
		std::string name;
		bool visible = true;
		bool locked = false;
	};

	explicit PortraitPreview(PortraitCanvas *canvas, QWidget *parent = nullptr);
	~PortraitPreview() override;

	void refresh();
	void clearSelection();
	void select(obs_sceneitem_t *item);
	obs_sceneitem_t *selectedItem() const { return selection_.get(); }

	bool centerItem(obs_sceneitem_t *item);
	bool fitItem(obs_sceneitem_t *item, bool fill);

	std::vector<SceneItemInfo> sceneItems() const;

	std::function<void()> onSelectionChanged;
	std::function<void(obs_source_t *)> onSourceActivated;

protected:
	void showEvent(QShowEvent *event) override;
	void hideEvent(QHideEvent *event) override;
	void changeEvent(QEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void paintEvent(QPaintEvent *event) override;
	QPaintEngine *paintEngine() const override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void mouseDoubleClickEvent(QMouseEvent *event) override;
	void wheelEvent(QWheelEvent *event) override;

private:
	enum class DragMode {
		None,
		Move,
		Scale,
	};

	static void drawCallback(void *param, uint32_t cx, uint32_t cy);
	static void drawFrame(void *param, uint32_t cx, uint32_t cy);

	void createDisplay();
	void destroyDisplay();
	void updateDisplaySize();
	void bindScene();
	bool widgetToCanvas(const QPointF &point, vec2 *out) const;
	Box boxOf(obs_sceneitem_t *item) const;
	obs_sceneitem_t *itemAt(const vec2 &point) const;
	void updateSelectionBox();
	void selectItem(obs_sceneitem_t *item);
	bool isInScene(obs_sceneitem_t *item) const;
	bool isInHandle(const vec2 &point) const;
	float canvasPerPixel() const;
	void snapPosition(const Box &box, vec2 *position);
	void clearGuides();

	PortraitCanvas *canvas_ = nullptr;
	obs_display_t *display_ = nullptr;
	obs_scene_t *boundScene_ = nullptr;

	SceneItemRef selection_;
	Box selectionBox_;

	DragMode dragMode_ = DragMode::None;
	vec2 dragStart_{};
	vec2 dragStartPos_{};
	vec2 dragStartScale_{};
	Box dragStartBox_;

	bool guideVertical_ = false;
	bool guideHorizontal_ = false;
	float guideVerticalAt_ = 0.0f;
	float guideHorizontalAt_ = 0.0f;
};

} // namespace manycast
