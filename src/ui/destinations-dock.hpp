#pragma once

#include "core/destination.hpp"

#include <QWidget>

#include <string>
#include <vector>

class QLabel;
class QPushButton;
class QTimer;
class QVBoxLayout;

namespace manycast {

class TargetRow;

class RestreamDock : public QWidget {
public:
	explicit RestreamDock(QWidget *parent = nullptr);
	~RestreamDock() override;

	void onFrontendEvent(int event);
	void startAll();
	void stopAll();
	void shutdown();

	void addDestination(Destination destination);
	void editDestination(const std::string &id);
	void removeDestination(const std::string &id);
	void save();

private:
	TargetRow *createRow(const Destination &destination);
	void reload();
	void tick();
	void updateSummary();
	Destination *findDestination(const std::string &id);

	std::vector<Destination> destinations_;
	std::vector<TargetRow *> rows_;
	QVBoxLayout *rowLayout_ = nullptr;
	QLabel *emptyHint_ = nullptr;
	QLabel *summary_ = nullptr;
	QPushButton *startAllButton_ = nullptr;
	QPushButton *stopAllButton_ = nullptr;
	QTimer *timer_ = nullptr;
};

} // namespace manycast
