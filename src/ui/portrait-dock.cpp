#include "ui/portrait-dock.hpp"

#include "common/guard.hpp"
#include "common/localization.hpp"
#include "core/portrait-canvas.hpp"
#include "ui/portrait-preview.hpp"
#include "core/output.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QComboBox>
#include <QCursor>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

namespace manycast {

namespace {

struct SourceEntry {
	std::string name;
	obs_source_t *source = nullptr;
};

QString itemLabel(const PortraitPreview::SceneItemInfo &info)
{
	QString label = QString::fromUtf8(info.name.c_str());
	if (!info.visible)
		label += QStringLiteral(" ") + text("Portrait.ItemHidden");
	if (info.locked)
		label += QStringLiteral(" ") + text("Portrait.ItemLocked");
	return label;
}

bool collectSourcesProc(void *param, obs_source_t *source)
{
	auto *entries = static_cast<std::vector<SourceEntry> *>(param);
	const char *name = obs_source_get_name(source);
	if (name)
		entries->push_back({name, source});
	return true;
}

struct CanvasPreset {
	int width;
	int height;
	const char *label;
};

const CanvasPreset kCanvasPresets[] = {
	{1080, 1920, "1080x1920 (9:16)"},
	{720, 1280, "720x1280 (9:16)"},
	{1080, 1080, "1080x1080 (1:1)"},
	{1920, 1080, "1920x1080 (16:9)"},
};

} // namespace

PortraitDock::PortraitDock(PortraitCanvas *canvas, QWidget *parent) : QWidget(parent), canvas_(canvas)
{
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(6, 6, 6, 6);
	root->setSpacing(6);

	auto *sceneGroup = new QGroupBox(text("Portrait.Scene"), this);
	auto *sceneLayout = new QVBoxLayout(sceneGroup);
	sceneLayout->setSpacing(6);
	sceneList_ = new QComboBox(sceneGroup);
	sceneList_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	sceneLayout->addWidget(sceneList_);
	auto *sceneButtons = new QHBoxLayout();
	fromCurrent_ = new QPushButton(text("Portrait.SceneFromCurrent"), sceneGroup);
	emptyScene_ = new QPushButton(text("Portrait.NewScene"), sceneGroup);
	deleteScene_ = new QPushButton(text("Portrait.DeleteScene"), sceneGroup);
	addSource_ = new QPushButton(text("Portrait.AddSource"), sceneGroup);
	sceneButtons->addWidget(fromCurrent_);
	sceneButtons->addWidget(emptyScene_);
	sceneButtons->addWidget(deleteScene_);
	sceneButtons->addWidget(addSource_);
	sceneButtons->addStretch(1);
	sceneLayout->addLayout(sceneButtons);
	root->addWidget(sceneGroup);

	auto *splitter = new QSplitter(Qt::Horizontal, this);
	splitter->setChildrenCollapsible(false);

	preview_ = new PortraitPreview(canvas_, splitter);
	splitter->addWidget(preview_);

	auto *sideWidget = new QWidget(splitter);
	sideWidget->setMinimumWidth(210);
	auto *sidePanel = new QVBoxLayout(sideWidget);
	sidePanel->setContentsMargins(0, 0, 0, 0);
	sidePanel->setSpacing(6);

	auto *sourcesGroup = new QGroupBox(text("Portrait.Group.Sources"), sideWidget);
	auto *sourcesLayout = new QVBoxLayout(sourcesGroup);
	itemList_ = new QListWidget(sourcesGroup);
	itemList_->setSelectionMode(QAbstractItemView::SingleSelection);
	itemList_->setMinimumHeight(80);
	sourcesLayout->addWidget(itemList_, 1);

	auto *orderGrid = new QGridLayout();
	up_ = new QPushButton(text("Portrait.Up"), sourcesGroup);
	down_ = new QPushButton(text("Portrait.Down"), sourcesGroup);
	visibility_ = new QPushButton(text("Portrait.Hide"), sourcesGroup);
	lock_ = new QPushButton(text("Portrait.Lock"), sourcesGroup);
	orderGrid->addWidget(up_, 0, 0);
	orderGrid->addWidget(down_, 0, 1);
	orderGrid->addWidget(visibility_, 1, 0);
	orderGrid->addWidget(lock_, 1, 1);
	sourcesLayout->addLayout(orderGrid);
	sidePanel->addWidget(sourcesGroup, 1);

	auto *itemGroup = new QGroupBox(text("Portrait.Group.Item"), sideWidget);
	auto *itemLayout = new QGridLayout(itemGroup);
	properties_ = new QPushButton(text("Portrait.Properties"), itemGroup);
	filters_ = new QPushButton(text("Portrait.Filters"), itemGroup);
	remove_ = new QPushButton(text("Portrait.Remove"), itemGroup);
	fit_ = new QPushButton(text("Portrait.Fit"), itemGroup);
	fill_ = new QPushButton(text("Portrait.Fill"), itemGroup);
	center_ = new QPushButton(text("Portrait.Center"), itemGroup);
	itemLayout->addWidget(properties_, 0, 0);
	itemLayout->addWidget(filters_, 0, 1);
	itemLayout->addWidget(fit_, 1, 0);
	itemLayout->addWidget(fill_, 1, 1);
	itemLayout->addWidget(center_, 2, 0);
	itemLayout->addWidget(remove_, 2, 1);
	sidePanel->addWidget(itemGroup);

	splitter->addWidget(sideWidget);
	splitter->setStretchFactor(0, 1);
	splitter->setStretchFactor(1, 0);
	splitter->setSizes({600, 260});
	root->addWidget(splitter, 1);

	auto *canvasGroup = new QGroupBox(text("Portrait.Group.Canvas"), this);
	auto *canvasRow = new QHBoxLayout(canvasGroup);
	canvasSize_ = new QComboBox(canvasGroup);
	canvasSize_->setToolTip(text("Portrait.Resolution"));
	for (const CanvasPreset &preset : kCanvasPresets)
		canvasSize_->addItem(QString::fromLatin1(preset.label), QVariant::fromValue<int>(preset.width));
	canvasRow->addWidget(canvasSize_, 1);
	applySize_ = new QPushButton(text("Portrait.ResolutionApply"), canvasGroup);
	canvasRow->addWidget(applySize_);
	root->addWidget(canvasGroup);

	status_ = new QLabel(this);
	status_->setWordWrap(true);
	status_->setVisible(false);
	root->addWidget(status_);

	QObject::connect(fromCurrent_, &QPushButton::clicked, this,
			 guarded("scene from current", [this]() { createSceneFromCurrent(); }));
	QObject::connect(emptyScene_, &QPushButton::clicked, this,
			 guarded("new scene", [this]() { createEmptyScene(); }));
	QObject::connect(deleteScene_, &QPushButton::clicked, this,
			 guarded("delete scene", [this]() { deleteScene(); }));
	QObject::connect(addSource_, &QPushButton::clicked, this, guarded("add source", [this]() { addSource(); }));
	QObject::connect(properties_, &QPushButton::clicked, this,
			 guarded("source properties", [this]() { openProperties(); }));
	QObject::connect(filters_, &QPushButton::clicked, this, guarded("source filters", [this]() { openFilters(); }));
	QObject::connect(remove_, &QPushButton::clicked, this,
			 guarded("remove source", [this]() { removeSelected(); }));
	QObject::connect(fit_, &QPushButton::clicked, this, guarded("fit source", [this]() { fitSelected(false); }));
	QObject::connect(fill_, &QPushButton::clicked, this, guarded("fill source", [this]() { fitSelected(true); }));
	QObject::connect(center_, &QPushButton::clicked, this,
			 guarded("center source", [this]() { centerSelected(); }));
	QObject::connect(up_, &QPushButton::clicked, this, guarded("move source up", [this]() { moveSelected(1); }));
	QObject::connect(down_, &QPushButton::clicked, this,
			 guarded("move source down", [this]() { moveSelected(-1); }));
	QObject::connect(visibility_, &QPushButton::clicked, this,
			 guarded("toggle visibility", [this]() { toggleVisibility(); }));
	QObject::connect(lock_, &QPushButton::clicked, this, guarded("toggle lock", [this]() { toggleLock(); }));
	QObject::connect(applySize_, &QPushButton::clicked, this,
			 guarded("apply canvas size", [this]() { applyCanvasSize(); }));
	QObject::connect(itemList_, &QListWidget::currentRowChanged, this, [this](int row) {
		guard("select source", [this, row]() {
			if (updating_ || row < 0)
				return;

			QListWidgetItem *listItem = itemList_->item(row);
			if (listItem)
				selectItemById(listItem->data(Qt::UserRole).toLongLong());
		});
	});

	preview_->onSelectionChanged = [this]() {
		guard("selection changed", [this]() { handleSelectionChanged(); });
	};
	preview_->onSourceActivated = [this](obs_source_t *) {
		guard("activate source", [this]() { openProperties(); });
	};

	timer_ = new QTimer(this);
	timer_->setInterval(1000);
	QObject::connect(timer_, &QTimer::timeout, this, guarded("portrait dock poll", [this]() { poll(); }));
	timer_->start();

	refresh();
}

PortraitDock::~PortraitDock() = default;

void PortraitDock::refresh()
{
	ready_ = canvas_ && canvas_->ready();

	if (ready_)
		syncSceneList(true);
	else
		clearSceneList();

	preview_->refresh();
	rebuildItemList();
	handleSelectionChanged();
}

void PortraitDock::poll()
{
	if (!canvas_)
		return;

	if (canvas_->ready() != ready_) {
		refresh();
		return;
	}
	if (!ready_)
		return;

	syncSceneList(false);
	preview_->refresh();
	syncItemList();
	updateButtons();
}

void PortraitDock::clearSceneList()
{
	sceneNames_.clear();
	sceneList_->clear();
}

void PortraitDock::syncSceneList(bool force)
{
	if (!canvas_ || !canvas_->ready()) {
		clearSceneList();
		return;
	}

	const std::vector<std::string> names = canvas_->sceneNames();
	if (!force && names == sceneNames_)
		return;

	sceneNames_ = names;
	rebuildSceneList();
}

/* rebuilds only when items were added or removed, so the selection is kept */
void PortraitDock::syncItemList()
{
	const std::vector<PortraitPreview::SceneItemInfo> items = preview_->sceneItems();

	bool structureChanged = items.size() != itemIds_.size();
	if (!structureChanged) {
		for (std::size_t i = 0; i < items.size(); i++) {
			if (obs_sceneitem_get_id(items[i].item) != itemIds_[i]) {
				structureChanged = true;
				break;
			}
		}
	}

	if (structureChanged) {
		rebuildItemList();
		return;
	}

	updating_ = true;
	for (std::size_t i = 0; i < items.size() && int(i) < itemList_->count(); i++) {
		const QString label = itemLabel(items[i]);
		if (itemList_->item(int(i))->text() != label)
			itemList_->item(int(i))->setText(label);
	}
	updating_ = false;
}

void PortraitDock::rebuildSceneList()
{
	const QString current = QString::fromStdString(canvas_ ? canvas_->sceneName() : std::string());

	QObject::disconnect(sceneList_, nullptr, this, nullptr);
	sceneList_->clear();
	for (const std::string &name : sceneNames_)
		sceneList_->addItem(QString::fromStdString(name), QString::fromStdString(name));

	const int index = sceneList_->findText(current);
	if (index >= 0)
		sceneList_->setCurrentIndex(index);
	sceneList_->setEnabled(!sceneNames_.empty());

	QObject::connect(sceneList_, &QComboBox::currentIndexChanged, this, [this](int i) {
		guard("select portrait scene", [this, i]() {
			if (i < 0 || !canvas_)
				return;

			const std::string name = sceneList_->itemData(i).toString().toStdString();
			if (canvas_->selectScene(name))
				refresh();
		});
	});
}

void PortraitDock::rebuildItemList()
{
	updating_ = true;
	itemList_->clear();
	itemIds_.clear();

	const std::vector<PortraitPreview::SceneItemInfo> items = preview_->sceneItems();
	obs_sceneitem_t *selected = preview_->selectedItem();

	for (const PortraitPreview::SceneItemInfo &info : items) {
		auto *listItem = new QListWidgetItem(itemLabel(info), itemList_);
		const int64_t id = obs_sceneitem_get_id(info.item);
		listItem->setData(Qt::UserRole, QVariant::fromValue<qlonglong>(id));
		itemIds_.push_back(id);

		if (info.item == selected)
			itemList_->setCurrentItem(listItem);
	}

	updating_ = false;

	updateButtons();
}

void PortraitDock::updateButtons()
{
	const bool ready = canvas_ && canvas_->ready();
	const bool hasScene = ready && canvas_->scene() != nullptr;
	obs_sceneitem_t *item = preview_->selectedItem();

	fromCurrent_->setEnabled(ready);
	emptyScene_->setEnabled(ready);
	deleteScene_->setEnabled(ready && sceneNames_.size() > 1u);
	addSource_->setEnabled(hasScene);
	properties_->setEnabled(item != nullptr);
	filters_->setEnabled(item != nullptr);
	remove_->setEnabled(item != nullptr);
	fit_->setEnabled(item != nullptr);
	fill_->setEnabled(item != nullptr);
	center_->setEnabled(item != nullptr);
	up_->setEnabled(item != nullptr);
	down_->setEnabled(item != nullptr);
	visibility_->setEnabled(item != nullptr);
	lock_->setEnabled(item != nullptr);
	applySize_->setEnabled(ready);

	if (item) {
		visibility_->setText(obs_sceneitem_visible(item) ? text("Portrait.Hide") : text("Portrait.Show"));
		lock_->setText(obs_sceneitem_locked(item) ? text("Portrait.Unlock") : text("Portrait.Lock"));
	} else {
		visibility_->setText(text("Portrait.Hide"));
		lock_->setText(text("Portrait.Lock"));
	}

	if (!ready) {
		status_->setText(text("Portrait.NoCanvas"));
		status_->setVisible(true);
		return;
	}

	status_->clear();
	status_->setVisible(false);
}

void PortraitDock::handleSelectionChanged()
{
	updating_ = true;
	obs_sceneitem_t *item = preview_->selectedItem();
	int row = -1;
	if (item) {
		const int64_t id = obs_sceneitem_get_id(item);
		for (int i = 0; i < itemList_->count(); i++) {
			if (itemList_->item(i)->data(Qt::UserRole).toLongLong() == id) {
				row = i;
				break;
			}
		}
	}
	itemList_->setCurrentRow(row);
	updating_ = false;

	updateButtons();
}

void PortraitDock::selectItemById(int64_t id)
{
	const std::vector<PortraitPreview::SceneItemInfo> items = preview_->sceneItems();
	for (const PortraitPreview::SceneItemInfo &info : items) {
		if (obs_sceneitem_get_id(info.item) == id) {
			preview_->select(info.item);
			return;
		}
	}
	preview_->clearSelection();
}

void PortraitDock::createSceneFromCurrent()
{
	if (!canvas_)
		return;

	std::string error;
	finishSceneOperation(canvas_->createSceneFromCurrent(error), error);
}

void PortraitDock::createEmptyScene()
{
	if (!canvas_)
		return;

	std::string error;
	finishSceneOperation(canvas_->createEmptyScene(error), error);
}

void PortraitDock::deleteScene()
{
	if (!canvas_)
		return;

	const QString name = QString::fromStdString(canvas_->sceneName());
	if (name.isEmpty())
		return;

	if (QMessageBox::question(this, text("Portrait.Scene"), text("Question.DeleteScene", name),
				  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
		return;

	std::string error;
	finishSceneOperation(canvas_->removeScene(name.toStdString(), error), error);
}

void PortraitDock::finishSceneOperation(bool ok, const std::string &error)
{
	if (!ok && !error.empty())
		QMessageBox::warning(this, text("Portrait.Dock.Title"), QString::fromUtf8(error.c_str()));

	refresh();
}

void PortraitDock::addSource()
{
	obs_scene_t *scene = canvas_ ? canvas_->scene() : nullptr;
	if (!scene)
		return;

	std::vector<SourceEntry> entries;
	obs_enum_sources(collectSourcesProc, &entries);
	if (entries.empty())
		return;

	std::sort(entries.begin(), entries.end(),
		  [](const SourceEntry &a, const SourceEntry &b) { return a.name < b.name; });

	obs_source_t *currentSceneSource = obs_scene_get_source(scene);

	QMenu menu(this);
	for (const SourceEntry &entry : entries) {
		if (entry.source == currentSceneSource)
			continue;

		QAction *action = menu.addAction(QString::fromUtf8(entry.name.c_str()));
		auto holder = std::make_shared<SourceRef>(obs_source_get_ref(entry.source));
		QObject::connect(action, &QAction::triggered, this, [this, holder, source = entry.source]() {
			guard("add source to scene", [this, source]() { addSourceToScene(source); });
		});
	}

	if (menu.isEmpty())
		return;

	menu.exec(QCursor::pos());
}

void PortraitDock::addSourceToScene(obs_source_t *source)
{
	obs_scene_t *scene = canvas_ ? canvas_->scene() : nullptr;
	if (!scene || !source)
		return;

	obs_sceneitem_t *item = obs_scene_add(scene, source);
	if (!item)
		return;

	preview_->fitItem(item, false);
	preview_->select(item);
	rebuildItemList();
}

void PortraitDock::removeSelected()
{
	obs_sceneitem_t *item = preview_->selectedItem();
	if (!item)
		return;

	preview_->clearSelection();
	obs_sceneitem_remove(item);
	rebuildItemList();
}

void PortraitDock::fitSelected(bool fill)
{
	obs_sceneitem_t *item = preview_->selectedItem();
	if (!item)
		return;

	preview_->fitItem(item, fill);
}

void PortraitDock::centerSelected()
{
	obs_sceneitem_t *item = preview_->selectedItem();
	if (!item)
		return;

	preview_->centerItem(item);
}

void PortraitDock::moveSelected(int up)
{
	obs_sceneitem_t *item = preview_->selectedItem();
	if (!item)
		return;

	obs_sceneitem_set_order(item, up > 0 ? OBS_ORDER_MOVE_UP : OBS_ORDER_MOVE_DOWN);
	rebuildItemList();
}

void PortraitDock::toggleVisibility()
{
	obs_sceneitem_t *item = preview_->selectedItem();
	if (!item)
		return;

	obs_sceneitem_set_visible(item, !obs_sceneitem_visible(item));
	rebuildItemList();
}

void PortraitDock::toggleLock()
{
	obs_sceneitem_t *item = preview_->selectedItem();
	if (!item)
		return;

	obs_sceneitem_set_locked(item, !obs_sceneitem_locked(item));
	rebuildItemList();
}

void PortraitDock::applyCanvasSize()
{
	const int index = canvasSize_->currentIndex();
	if (!canvas_ || index < 0)
		return;

	if (portraitOutputsActive() > 0) {
		QMessageBox::warning(this, text("Portrait.Dock.Title"), text("Portrait.Resolution.Busy"));
		return;
	}

	const CanvasPreset &preset = kCanvasPresets[index];
	if (!canvas_->setSize(static_cast<uint32_t>(preset.width), static_cast<uint32_t>(preset.height)))
		QMessageBox::warning(this, text("Portrait.Dock.Title"), text("Portrait.Resolution.Failed"));

	preview_->refresh();
	rebuildItemList();
}

void PortraitDock::openProperties()
{
	obs_sceneitem_t *item = preview_->selectedItem();
	obs_source_t *source = item ? obs_sceneitem_get_source(item) : nullptr;
	if (source)
		obs_frontend_open_source_properties(source);
}

void PortraitDock::openFilters()
{
	obs_sceneitem_t *item = preview_->selectedItem();
	obs_source_t *source = item ? obs_sceneitem_get_source(item) : nullptr;
	if (source)
		obs_frontend_open_source_filters(source);
}

} // namespace manycast
