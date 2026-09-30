#pragma once

#include <obs.h>

#include <QWidget>

#include <cstdint>
#include <string>
#include <vector>

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QTimer;

namespace manycast {

class PortraitCanvas;
class PortraitPreview;

class PortraitDock : public QWidget {
public:
	explicit PortraitDock(PortraitCanvas *canvas, QWidget *parent = nullptr);
	~PortraitDock() override;

	void refresh();

private:
	void poll();
	void clearSceneList();
	void syncSceneList(bool force);
	void rebuildSceneList();
	void rebuildItemList();
	void syncItemList();
	void updateButtons();
	void createSceneFromCurrent();
	void createEmptyScene();
	void deleteScene();
	void finishSceneOperation(bool ok, const std::string &error);
	void addSource();
	void addSourceToScene(obs_source_t *source);
	void removeSelected();
	void fitSelected(bool fill);
	void centerSelected();
	void moveSelected(int up);
	void toggleVisibility();
	void toggleLock();
	void applyCanvasSize();
	void openProperties();
	void openFilters();
	void selectItemById(int64_t id);
	void handleSelectionChanged();

	PortraitCanvas *canvas_ = nullptr;
	PortraitPreview *preview_ = nullptr;

	QComboBox *sceneList_ = nullptr;
	QPushButton *fromCurrent_ = nullptr;
	QPushButton *emptyScene_ = nullptr;
	QPushButton *deleteScene_ = nullptr;

	QPushButton *addSource_ = nullptr;
	QPushButton *properties_ = nullptr;
	QPushButton *filters_ = nullptr;
	QPushButton *remove_ = nullptr;
	QPushButton *fit_ = nullptr;
	QPushButton *fill_ = nullptr;
	QPushButton *center_ = nullptr;

	QListWidget *itemList_ = nullptr;
	QPushButton *up_ = nullptr;
	QPushButton *down_ = nullptr;
	QPushButton *visibility_ = nullptr;
	QPushButton *lock_ = nullptr;

	QComboBox *canvasSize_ = nullptr;
	QPushButton *applySize_ = nullptr;

	QLabel *status_ = nullptr;
	QTimer *timer_ = nullptr;

	std::vector<std::string> sceneNames_;
	std::vector<int64_t> itemIds_;
	bool ready_ = false;
	bool updating_ = false;
};

} // namespace manycast
