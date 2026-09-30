#include "ui/portrait-preview.hpp"
#include "common/guard.hpp"
#include "common/snap.hpp"
#include "common/plugin-state.hpp"
#include "core/portrait-canvas.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <graphics/matrix4.h>

#include <QMouseEvent>
#include <QEvent>
#include <QHideEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QWheelEvent>
#include <QWindow>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace manycast {

namespace {

constexpr uint32_t kOutlineColor = 0xFF60A0FF;
constexpr uint32_t kHandleColor = 0xFFFFFFFF;
constexpr uint32_t kGuideColor = 0xFF40B0FF;

void getScaleAndCenter(int baseCX, int baseCY, int windowCX, int windowCY, int &x, int &y, float &scale)
{
	if (baseCX <= 0 || baseCY <= 0 || windowCX <= 0 || windowCY <= 0) {
		x = 0;
		y = 0;
		scale = 0.0f;
		return;
	}

	const double windowAspect = double(windowCX) / double(windowCY);
	const double baseAspect = double(baseCX) / double(baseCY);
	int newCX;
	int newCY;

	if (windowAspect > baseAspect) {
		scale = float(windowCY) / float(baseCY);
		newCX = int(double(windowCY) * baseAspect);
		newCY = windowCY;
	} else {
		scale = float(windowCX) / float(baseCX);
		newCX = windowCX;
		newCY = int(float(windowCX) / baseAspect);
	}

	x = windowCX / 2 - newCX / 2;
	y = windowCY / 2 - newCY / 2;
}

void drawSolidRect(float x, float y, float width, float height, uint32_t color)
{
	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t *param = gs_effect_get_param_by_name(solid, "color");

	struct matrix4 matrix;
	matrix4_identity(&matrix);
	matrix.x.x = width;
	matrix.y.y = height;
	matrix.t.x = x;
	matrix.t.y = y;

	gs_effect_set_color(param, color);
	gs_matrix_push();
	gs_matrix_mul(&matrix);
	while (gs_effect_loop(solid, "Solid"))
		gs_draw_sprite(nullptr, 0, 1, 1);
	gs_matrix_pop();
}

SceneItemRef retainSceneItem(obs_sceneitem_t *item)
{
	if (item)
		obs_sceneitem_addref(item);
	return SceneItemRef{item};
}

struct HitTestContext {
	const vec2 *point = nullptr;
	obs_sceneitem_t *found = nullptr;
};

bool hitTestProc(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *context = static_cast<HitTestContext *>(param);
	if (!obs_sceneitem_visible(item) || obs_sceneitem_locked(item))
		return true;

	struct matrix4 box;
	obs_sceneitem_get_box_transform(item, &box);

	struct matrix4 inverse;
	matrix4_inv(&inverse, &box);

	struct vec3 test;
	vec3_set(&test, context->point->x, context->point->y, 0.0f);
	vec3_transform(&test, &test, &inverse);

	if (test.x >= 0.0f && test.x <= 1.0f && test.y >= 0.0f && test.y <= 1.0f)
		context->found = item;

	return true;
}

struct FindContext {
	obs_sceneitem_t *needle = nullptr;
	bool found = false;
};

bool findProc(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *context = static_cast<FindContext *>(param);
	if (item == context->needle) {
		context->found = true;
		return false;
	}
	return true;
}

bool collectItemsProc(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *items = static_cast<std::vector<PortraitPreview::SceneItemInfo> *>(param);

	PortraitPreview::SceneItemInfo info;
	info.item = item;
	obs_source_t *source = obs_sceneitem_get_source(item);
	const char *name = source ? obs_source_get_name(source) : nullptr;
	info.name = name ? name : "?";
	info.visible = obs_sceneitem_visible(item);
	info.locked = obs_sceneitem_locked(item);
	items->push_back(info);
	return true;
}

} // namespace

PortraitPreview::PortraitPreview(PortraitCanvas *canvas, QWidget *parent) : QWidget(parent), canvas_(canvas)
{
	setAttribute(Qt::WA_PaintOnScreen);
	setAttribute(Qt::WA_StaticContents);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAttribute(Qt::WA_DontCreateNativeAncestors);
	setAttribute(Qt::WA_NativeWindow);
	setMinimumSize(120, 120);
	setMouseTracking(false);
}

PortraitPreview::~PortraitPreview()
{
	if (manycast::isShuttingDown())
		return;

	destroyDisplay();
}

void PortraitPreview::createDisplay()
{
	if (display_)
		return;

	if (!isVisible())
		return;

#ifdef _WIN32
	QWindow *handle = windowHandle();
	if (!handle || !handle->isExposed())
		return;

	const QSize size = this->size() * devicePixelRatioF();
	if (size.width() <= 0 || size.height() <= 0)
		return;

	struct gs_init_data info = {};
	info.window.hwnd = (HWND)handle->winId();
	info.cx = uint32_t(size.width());
	info.cy = uint32_t(size.height());
	info.format = GS_BGRA;
	info.zsformat = GS_ZS_NONE;

	obs_video_info ovi;
	if (obs_get_video_info(&ovi))
		info.adapter = ovi.adapter;

	display_ = obs_display_create(&info, 0xFF101010);
	if (display_) {
		obs_display_add_draw_callback(display_, &PortraitPreview::drawCallback, this);
	} else {
		obs_log(LOG_ERROR, "could not create the portrait preview display");
	}
#endif
}

void PortraitPreview::destroyDisplay()
{
	if (!display_)
		return;

	obs_display_remove_draw_callback(display_, &PortraitPreview::drawCallback, this);
	obs_display_destroy(display_);
	display_ = nullptr;
}

void PortraitPreview::updateDisplaySize()
{
	if (!display_)
		return;

	const QSize size = this->size() * devicePixelRatioF();
	obs_display_resize(display_, uint32_t(size.width()), uint32_t(size.height()));
}

void PortraitPreview::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	createDisplay();
	updateDisplaySize();
	update();
}

void PortraitPreview::hideEvent(QHideEvent *event)
{
	QWidget::hideEvent(event);
	/* the native window may be recreated while hidden, so drop the display */
	destroyDisplay();
}

void PortraitPreview::changeEvent(QEvent *event)
{
	if (event->type() == QEvent::WinIdChange)
		destroyDisplay();

	QWidget::changeEvent(event);
}

QPaintEngine *PortraitPreview::paintEngine() const
{
	return nullptr;
}

void PortraitPreview::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	createDisplay();
	updateDisplaySize();
}

void PortraitPreview::paintEvent(QPaintEvent *event)
{
	Q_UNUSED(event);
	createDisplay();
	updateDisplaySize();
}

float PortraitPreview::canvasPerPixel() const
{
	obs_canvas_t *canvas = canvas_ ? canvas_->canvas() : nullptr;
	if (!canvas)
		return 0.0f;

	obs_video_info ovi;
	if (!obs_canvas_get_video_info(canvas, &ovi) || !ovi.base_width || !ovi.base_height)
		return 0.0f;

	const QSize pixelSize = size() * devicePixelRatioF();
	int x = 0;
	int y = 0;
	float scale = 0.0f;
	getScaleAndCenter(int(ovi.base_width), int(ovi.base_height), pixelSize.width(), pixelSize.height(), x, y,
			  scale);
	return scale > 0.0f ? 1.0f / scale : 0.0f;
}

bool PortraitPreview::widgetToCanvas(const QPointF &point, vec2 *out) const
{
	obs_canvas_t *canvas = canvas_ ? canvas_->canvas() : nullptr;
	if (!canvas || !out)
		return false;

	obs_video_info ovi;
	if (!obs_canvas_get_video_info(canvas, &ovi) || !ovi.base_width || !ovi.base_height)
		return false;

	const QSize pixelSize = size() * devicePixelRatioF();
	int x = 0;
	int y = 0;
	float scale = 0.0f;
	getScaleAndCenter(int(ovi.base_width), int(ovi.base_height), pixelSize.width(), pixelSize.height(), x, y,
			  scale);
	if (scale <= 0.0f)
		return false;

	const QPointF devicePoint = point * devicePixelRatioF();
	vec2_set(out, float((devicePoint.x() - x) / scale), float((devicePoint.y() - y) / scale));
	return true;
}

PortraitPreview::Box PortraitPreview::boxOf(obs_sceneitem_t *item) const
{
	Box box;
	if (!item)
		return box;

	struct matrix4 transform;
	obs_sceneitem_get_box_transform(item, &transform);

	float minX = FLT_MAX;
	float minY = FLT_MAX;
	float maxX = -FLT_MAX;
	float maxY = -FLT_MAX;

	for (int i = 0; i < 4; i++) {
		const float u = (i == 1 || i == 2) ? 1.0f : 0.0f;
		const float v = (i >= 2) ? 1.0f : 0.0f;

		struct vec3 corner;
		vec3_set(&corner, u, v, 0.0f);
		vec3_transform(&corner, &corner, &transform);

		minX = (std::min)(minX, corner.x);
		minY = (std::min)(minY, corner.y);
		maxX = (std::max)(maxX, corner.x);
		maxY = (std::max)(maxY, corner.y);
	}

	box.x = minX;
	box.y = minY;
	box.width = maxX - minX;
	box.height = maxY - minY;
	box.valid = box.width > 0.5f && box.height > 0.5f;
	return box;
}

bool PortraitPreview::isInScene(obs_sceneitem_t *item) const
{
	if (!item || !boundScene_)
		return false;

	FindContext context;
	context.needle = item;
	obs_scene_enum_items(boundScene_, findProc, &context);
	return context.found;
}

obs_sceneitem_t *PortraitPreview::itemAt(const vec2 &point) const
{
	if (!boundScene_)
		return nullptr;

	HitTestContext context;
	context.point = &point;
	obs_scene_enum_items(boundScene_, hitTestProc, &context);
	return context.found;
}

void PortraitPreview::updateSelectionBox()
{
	if (selection_ && !isInScene(selection_.get())) {
		selection_.reset();
		selectionBox_ = {};
		if (onSelectionChanged)
			onSelectionChanged();
	}

	selectionBox_ = selection_ ? boxOf(selection_.get()) : Box{};
}

bool PortraitPreview::isInHandle(const vec2 &point) const
{
	if (!selectionBox_.valid)
		return false;

	const float size = 14.0f * canvasPerPixel();
	if (size <= 0.0f)
		return false;

	const float right = selectionBox_.x + selectionBox_.width;
	const float bottom = selectionBox_.y + selectionBox_.height;

	return point.x >= right - size && point.x <= right + size && point.y >= bottom - size &&
	       point.y <= bottom + size;
}

void PortraitPreview::selectItem(obs_sceneitem_t *item)
{
	const bool changed = selection_.get() != item;
	selection_ = retainSceneItem(item);
	selectionBox_ = item ? boxOf(item) : Box{};

	if (changed && onSelectionChanged)
		onSelectionChanged();
}

void PortraitPreview::clearSelection()
{
	selectItem(nullptr);
}

void PortraitPreview::bindScene()
{
	obs_scene_t *scene = canvas_ ? canvas_->scene() : nullptr;
	if (scene == boundScene_)
		return;

	boundScene_ = scene;
	selection_.reset();
	selectionBox_ = {};
	if (onSelectionChanged)
		onSelectionChanged();
}

void PortraitPreview::refresh()
{
	bindScene();
	updateSelectionBox();
}

void PortraitPreview::select(obs_sceneitem_t *item)
{
	selectItem(item);
}

bool PortraitPreview::centerItem(obs_sceneitem_t *item)
{
	if (!item || !canvas_)
		return false;

	const Box box = boxOf(item);
	if (!box.valid)
		return false;

	struct vec2 position;
	obs_sceneitem_get_pos(item, &position);
	position.x += (float(canvas_->width()) - box.width) / 2.0f - box.x;
	position.y += (float(canvas_->height()) - box.height) / 2.0f - box.y;
	obs_sceneitem_set_pos(item, &position);

	updateSelectionBox();
	return true;
}

bool PortraitPreview::fitItem(obs_sceneitem_t *item, bool fill)
{
	obs_source_t *source = item ? obs_sceneitem_get_source(item) : nullptr;
	if (!source || !canvas_)
		return false;

	const uint32_t sourceWidth = obs_source_get_width(source);
	const uint32_t sourceHeight = obs_source_get_height(source);
	const uint32_t canvasWidth = canvas_->width();
	const uint32_t canvasHeight = canvas_->height();
	if (!sourceWidth || !sourceHeight || !canvasWidth || !canvasHeight)
		return false;

	const float horizontal = float(canvasWidth) / float(sourceWidth);
	const float vertical = float(canvasHeight) / float(sourceHeight);
	const float factor = fill ? (std::max)(horizontal, vertical) : (std::min)(horizontal, vertical);

	struct vec2 scale;
	scale.x = factor;
	scale.y = factor;
	obs_sceneitem_set_scale(item, &scale);

	return centerItem(item);
}

std::vector<PortraitPreview::SceneItemInfo> PortraitPreview::sceneItems() const
{
	std::vector<SceneItemInfo> items;
	if (!boundScene_)
		return items;

	obs_scene_enum_items(boundScene_, collectItemsProc, &items);
	std::reverse(items.begin(), items.end());
	return items;
}

void PortraitPreview::clearGuides()
{
	guideVertical_ = false;
	guideHorizontal_ = false;
}

void PortraitPreview::snapPosition(const Box &box, vec2 *position)
{
	clearGuides();

	if (!canvas_ || !position || !box.valid)
		return;

	const float threshold = 10.0f * canvasPerPixel();
	if (threshold <= 0.0f)
		return;

	const SnapBox snapBox{box.valid, box.x, box.y, box.width, box.height};
	const SnapResult snap = computeSnap(snapBox, float(canvas_->width()), float(canvas_->height()), threshold);

	if (snap.snappedX) {
		position->x += snap.offsetX;
		guideVertical_ = true;
		guideVerticalAt_ = snap.guideX;
	}

	if (snap.snappedY) {
		position->y += snap.offsetY;
		guideHorizontal_ = true;
		guideHorizontalAt_ = snap.guideY;
	}
}

void PortraitPreview::mousePressEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton) {
		QWidget::mousePressEvent(event);
		return;
	}

	vec2 point;
	if (!widgetToCanvas(event->position(), &point)) {
		QWidget::mousePressEvent(event);
		return;
	}

	updateSelectionBox();

	const bool handleHit = isInHandle(point);
	obs_sceneitem_t *item = handleHit ? selection_.get() : itemAt(point);
	selectItem(item);
	clearGuides();

	if (!selection_ || !selectionBox_.valid) {
		dragMode_ = DragMode::None;
		return;
	}

	/* a locked item can be selected but must not be moved */
	if (obs_sceneitem_locked(selection_.get())) {
		dragMode_ = DragMode::None;
		return;
	}

	dragMode_ = handleHit ? DragMode::Scale : DragMode::Move;
	dragStart_ = point;
	obs_sceneitem_get_pos(selection_.get(), &dragStartPos_);
	obs_sceneitem_get_scale(selection_.get(), &dragStartScale_);
	dragStartBox_ = selectionBox_;
	event->accept();
}

void PortraitPreview::mouseMoveEvent(QMouseEvent *event)
{
	if (dragMode_ == DragMode::None || !selection_ || !isInScene(selection_.get())) {
		QWidget::mouseMoveEvent(event);
		return;
	}

	vec2 point;
	if (!widgetToCanvas(event->position(), &point)) {
		QWidget::mouseMoveEvent(event);
		return;
	}

	if (dragMode_ == DragMode::Move) {
		const float deltaX = point.x - dragStart_.x;
		const float deltaY = point.y - dragStart_.y;

		struct vec2 position;
		position.x = dragStartPos_.x + deltaX;
		position.y = dragStartPos_.y + deltaY;

		Box box = dragStartBox_;
		box.x += deltaX;
		box.y += deltaY;
		snapPosition(box, &position);

		obs_sceneitem_set_pos(selection_.get(), &position);
	} else if (dragMode_ == DragMode::Scale && dragStartBox_.width > 1.0f) {
		float factor = (point.x - dragStartBox_.x) / dragStartBox_.width;
		factor = (std::max)(0.02f, (std::min)(factor, 40.0f));

		struct vec2 scale;
		scale.x = dragStartScale_.x * factor;
		scale.y = dragStartScale_.y * factor;
		obs_sceneitem_set_scale(selection_.get(), &scale);

		const Box now = boxOf(selection_.get());
		struct vec2 position;
		obs_sceneitem_get_pos(selection_.get(), &position);
		position.x += dragStartBox_.x - now.x;
		position.y += dragStartBox_.y - now.y;
		obs_sceneitem_set_pos(selection_.get(), &position);
	}

	selectionBox_ = boxOf(selection_.get());
	event->accept();
}

void PortraitPreview::mouseReleaseEvent(QMouseEvent *event)
{
	dragMode_ = DragMode::None;
	clearGuides();
	QWidget::mouseReleaseEvent(event);
}

void PortraitPreview::mouseDoubleClickEvent(QMouseEvent *event)
{
	vec2 point;
	if (widgetToCanvas(event->position(), &point)) {
		obs_sceneitem_t *item = itemAt(point);
		if (item) {
			selectItem(item);
			if (onSourceActivated)
				onSourceActivated(obs_sceneitem_get_source(item));
			return;
		}
	}
	QWidget::mouseDoubleClickEvent(event);
}

void PortraitPreview::wheelEvent(QWheelEvent *event)
{
	if (!(event->modifiers() & Qt::ControlModifier) || !selection_ || !isInScene(selection_.get()) ||
	    obs_sceneitem_locked(selection_.get())) {
		QWidget::wheelEvent(event);
		return;
	}

	const int steps = event->angleDelta().y() / 120;
	if (steps == 0)
		return;

	const float factor = std::pow(1.1f, float(steps));
	const Box before = boxOf(selection_.get());
	if (!before.valid)
		return;

	const float centerX = before.x + before.width / 2.0f;
	const float centerY = before.y + before.height / 2.0f;

	struct vec2 scale;
	obs_sceneitem_get_scale(selection_.get(), &scale);
	scale.x *= factor;
	scale.y *= factor;
	obs_sceneitem_set_scale(selection_.get(), &scale);

	const Box after = boxOf(selection_.get());
	struct vec2 position;
	obs_sceneitem_get_pos(selection_.get(), &position);
	position.x += centerX - (after.x + after.width / 2.0f);
	position.y += centerY - (after.y + after.height / 2.0f);
	obs_sceneitem_set_pos(selection_.get(), &position);

	selectionBox_ = boxOf(selection_.get());
	event->accept();
}

void PortraitPreview::drawCallback(void *param, uint32_t cx, uint32_t cy)
{
	/* this runs on the graphics thread: never let anything escape into libobs */
	guard("portrait preview draw", [param, cx, cy]() { drawFrame(param, cx, cy); });
}

void PortraitPreview::drawFrame(void *param, uint32_t cx, uint32_t cy)
{
	auto *self = static_cast<PortraitPreview *>(param);
	obs_canvas_t *canvas = self->canvas_ ? self->canvas_->canvas() : nullptr;
	if (!canvas)
		return;

	obs_video_info ovi;
	if (!obs_canvas_get_video_info(canvas, &ovi) || !ovi.base_width || !ovi.base_height)
		return;

	int vx = 0;
	int vy = 0;
	float scale = 0.0f;
	getScaleAndCenter(int(ovi.base_width), int(ovi.base_height), int(cx), int(cy), vx, vy, scale);
	if (scale <= 0.0f)
		return;

	const uint32_t viewWidth = uint32_t(float(ovi.base_width) * scale);
	const uint32_t viewHeight = uint32_t(float(ovi.base_height) * scale);

	gs_viewport_push();
	gs_projection_push();
	gs_set_viewport(uint32_t(vx), uint32_t(vy), viewWidth, viewHeight);
	gs_ortho(0.0f, float(ovi.base_width), 0.0f, float(ovi.base_height), -100.0f, 100.0f);

	obs_render_canvas_texture(canvas);

	if (self->guideVertical_ || self->guideHorizontal_) {
		const float thickness = (std::max)(1.0f, 1.5f / scale);
		if (self->guideVertical_)
			drawSolidRect(self->guideVerticalAt_ - thickness / 2.0f, 0.0f, thickness,
				      float(ovi.base_height), kGuideColor);
		if (self->guideHorizontal_)
			drawSolidRect(0.0f, self->guideHorizontalAt_ - thickness / 2.0f, float(ovi.base_width),
				      thickness, kGuideColor);
	}

	if (self->selectionBox_.valid) {
		const float thickness = (std::max)(1.0f, 2.0f / scale);
		const Box &box = self->selectionBox_;

		drawSolidRect(box.x, box.y, box.width, thickness, kOutlineColor);
		drawSolidRect(box.x, box.y + box.height - thickness, box.width, thickness, kOutlineColor);
		drawSolidRect(box.x, box.y, thickness, box.height, kOutlineColor);
		drawSolidRect(box.x + box.width - thickness, box.y, thickness, box.height, kOutlineColor);

		const float handle = (std::max)(1.0f, 10.0f / scale);
		drawSolidRect(box.x + box.width - handle, box.y + box.height - handle, handle, handle, kHandleColor);
		drawSolidRect(box.x, box.y, handle, handle, kHandleColor);
		drawSolidRect(box.x + box.width - handle, box.y, handle, handle, kHandleColor);
		drawSolidRect(box.x, box.y + box.height - handle, handle, handle, kHandleColor);
	}

	gs_projection_pop();
	gs_viewport_pop();
}

} // namespace manycast
