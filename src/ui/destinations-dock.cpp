#include "ui/destinations-dock.hpp"

#include "common/guard.hpp"
#include "common/localization.hpp"
#include "common/format.hpp"
#include "common/plugin-state.hpp"
#include "core/config.hpp"
#include "ui/destination-dialog.hpp"
#include "core/output.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdio>
#include <memory>

namespace manycast {

namespace {

QString separator()
{
	return text("Label.Separator");
}

void applyDimColor(QWidget *widget)
{
	QPalette palette = widget->palette();
	palette.setColor(QPalette::WindowText, palette.color(QPalette::Disabled, QPalette::WindowText));
	widget->setPalette(palette);
}

QString stateText(OutputState state, const std::string &message)
{
	switch (state) {
	case OutputState::Connecting:
		return text("Status.Connecting");
	case OutputState::Live:
		return text("Status.Live");
	case OutputState::Reconnecting:
		return text("Status.Reconnecting");
	case OutputState::Stopping:
		return text("Status.Stopping");
	case OutputState::Failed:
		return message.empty() ? text("Status.Error") : QString::fromUtf8(message.c_str());
	case OutputState::Idle:
		break;
	}
	return text("Status.Idle");
}

const char *stateColor(OutputState state)
{
	switch (state) {
	case OutputState::Live:
		return "#3cb371";
	case OutputState::Connecting:
	case OutputState::Reconnecting:
	case OutputState::Stopping:
		return "#e0a030";
	case OutputState::Failed:
		return "#e05252";
	default:
		return nullptr;
	}
}

QString encoderText(EncoderMode mode)
{
	return mode == EncoderMode::SharedMain ? text("Label.EncoderShared") : text("Label.EncoderOwn");
}

QString destinationTooltip(const Destination &destination)
{
	QStringList lines;
	lines << QString::fromUtf8(destination.name.c_str());

	if (destination.serviceMode == ServiceMode::Platform && !destination.platform.empty()) {
		lines << text("Tooltip.Target", QString::fromUtf8(destination.platform.c_str()));
		if (!destination.serverName.empty())
			lines << text("Tooltip.Server", QString::fromUtf8(destination.serverName.c_str()));
	} else if (!destination.serverUrl.empty()) {
		lines << text("Tooltip.Server", QString::fromUtf8(destination.serverUrl.c_str()));
	}

	if (!destination.streamKey.empty())
		lines << text("Tooltip.Key", QString::fromStdString(maskStreamKey(destination.streamKey)));

	lines << text("Tooltip.Encoder", encoderText(destination.encoderMode));
	return lines.join(QStringLiteral("\n"));
}

Destination defaultDestination()
{
	Destination destination;
	destination.id = makeId();
	destination.encoderMode = EncoderMode::SharedMain;
	destination.syncStart = true;
	destination.syncStop = true;
	return destination;
}

} // namespace

class TargetRow : public QFrame {
public:
	explicit TargetRow(const Destination &destination) : destination_(destination)
	{
		setFrameShape(QFrame::StyledPanel);

		auto *layout = new QGridLayout(this);
		layout->setContentsMargins(10, 8, 10, 8);
		layout->setHorizontalSpacing(6);
		layout->setVerticalSpacing(2);
		layout->setColumnStretch(0, 1);

		title_ = new QLabel(this);
		QFont titleFont = title_->font();
		titleFont.setBold(true);
		title_->setFont(titleFont);
		title_->setWordWrap(true);
		layout->addWidget(title_, 0, 0);

		start_ = new QPushButton(this);
		edit_ = new QPushButton(this);
		remove_ = new QPushButton(this);
		start_->setMinimumWidth(110);
		edit_->setMinimumWidth(80);
		remove_->setMinimumWidth(80);
		layout->addWidget(start_, 0, 1, Qt::AlignTop);
		layout->addWidget(edit_, 0, 2, Qt::AlignTop);
		layout->addWidget(remove_, 0, 3, Qt::AlignTop);

		subtitle_ = new QLabel(this);
		subtitle_->setWordWrap(true);
		applyDimColor(subtitle_);
		layout->addWidget(subtitle_, 1, 0, 1, 4);

		status_ = new QLabel(this);
		status_->setWordWrap(true);
		layout->addWidget(status_, 2, 0, 1, 4);

		setToolTip(destinationTooltip(destination_));
		updateStaticLabels();
		updateDynamicLabels();
	}

	const Destination &destination() const { return destination_; }
	const std::string &id() const { return destination_.id; }

	void setDestination(const Destination &destination)
	{
		destination_ = destination;
		setToolTip(destinationTooltip(destination_));
		updateStaticLabels();
	}

	QPushButton *startButton() const { return start_; }
	QPushButton *editButton() const { return edit_; }
	QPushButton *removeButton() const { return remove_; }

	bool hasOutput() const { return static_cast<bool>(output_); }
	bool active() const { return output_ && output_->active(); }
	bool isLive() const { return output_ && output_->state() == OutputState::Live; }
	double lastBitrateKbps() const { return lastBitrateKbps_; }

	void start()
	{
		if (output_)
			return;

		lastMessage_.clear();
		lastState_ = OutputState::Connecting;
		lastBitrateKbps_ = 0.0;
		output_ = std::make_unique<StreamOutput>(destination_);

		std::string error;
		if (!output_->start(error)) {
			lastState_ = OutputState::Failed;
			lastMessage_ = error;
			output_.reset();
		}
		updateDynamicLabels();
	}

	void stop()
	{
		if (!output_)
			return;
		output_->requestStop(false);
		updateDynamicLabels();
	}

	void forceStop()
	{
		if (!output_)
			return;
		output_->requestStop(true);
		destroyOutput();
	}

	void destroyOutput()
	{
		if (!output_)
			return;
		lastState_ = output_->state();
		lastMessage_ = output_->message();
		output_.reset();
		lastBitrateKbps_ = 0.0;
		updateDynamicLabels();
	}

	void tick()
	{
		if (!output_)
			return;

		const OutputState state = output_->state();
		if (state == OutputState::Live) {
			const OutputStats stats = output_->takeStats();
			lastBitrateKbps_ = stats.bitrateKbps;

			QStringList parts;
			parts << text("Status.Live");
			parts << QString::fromStdString(formatDuration(stats.seconds));
			parts << text("Metrics.Bitrate", QString::number(bitrateKbps(stats.bitrateKbps)));
			parts << text("Metrics.Fps", QString::number(framesPerSecond(stats.fps)));
			if (stats.droppedFrames > 0)
				parts << text("Metrics.Dropped", QString::number(stats.droppedFrames));

			status_->setText(parts.join(separator()));
			applyStatusStyle(state);
		} else {
			lastBitrateKbps_ = 0.0;
			updateDynamicLabels();
		}

		if (!output_->active() && (state == OutputState::Idle || state == OutputState::Failed)) {
			lastState_ = state;
			lastMessage_ = output_->message();
			output_.reset();
			lastBitrateKbps_ = 0.0;
			updateDynamicLabels();
		}
	}

private:
	void applyStatusStyle(OutputState state)
	{
		const char *color = stateColor(state);
		status_->setStyleSheet(
			color ? QStringLiteral("color: %1; font-weight: bold;").arg(QString::fromLatin1(color))
			      : QStringLiteral("font-weight: bold;"));
	}

	void updateStaticLabels()
	{
		title_->setText(QString::fromUtf8(destination_.name.c_str()));

		QStringList parts;
		parts << QString::fromUtf8(destination_.targetLabel().c_str());
		if (!destination_.streamKey.empty())
			parts << text("Label.Key", QString::fromStdString(maskStreamKey(destination_.streamKey)));
		parts << text("Label.Encoder", encoderText(destination_.encoderMode));
		parts << text("Label.Canvas", destination_.canvasMode == CanvasMode::Portrait
						      ? text("Dialog.Canvas.Portrait")
						      : text("Dialog.Canvas.Main"));
		if (destination_.delaySeconds > 0)
			parts << text("Label.Delay", QString::number(destination_.delaySeconds));
		subtitle_->setText(parts.join(separator()));

		start_->setText(text("Button.Start"));
		edit_->setText(text("Button.Edit"));
		remove_->setText(text("Button.Remove"));
	}

	void updateDynamicLabels()
	{
		const OutputState state = output_ ? output_->state() : lastState_;
		const std::string message = output_ ? output_->message() : lastMessage_;

		status_->setText(stateText(state, message));
		applyStatusStyle(state);

		const bool isActive = active();
		start_->setText(isActive ? text("Button.Stop") : text("Button.Start"));
		edit_->setEnabled(!isActive);
		remove_->setEnabled(!isActive);
	}

	Destination destination_;
	QLabel *title_ = nullptr;
	QLabel *subtitle_ = nullptr;
	QLabel *status_ = nullptr;
	QPushButton *start_ = nullptr;
	QPushButton *edit_ = nullptr;
	QPushButton *remove_ = nullptr;

	std::unique_ptr<StreamOutput> output_;
	OutputState lastState_ = OutputState::Idle;
	std::string lastMessage_;
	double lastBitrateKbps_ = 0.0;
};

namespace {

TargetRow *findRow(const std::vector<TargetRow *> &rows, const std::string &id)
{
	for (TargetRow *row : rows) {
		if (row->id() == id)
			return row;
	}
	return nullptr;
}

} // namespace

RestreamDock::RestreamDock(QWidget *parent) : QWidget(parent)
{
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(6, 6, 6, 6);
	root->setSpacing(6);

	auto *toolbar = new QHBoxLayout();
	auto *addButton = new QPushButton(text("Button.Add"), this);
	startAllButton_ = new QPushButton(text("Button.StartAll"), this);
	stopAllButton_ = new QPushButton(text("Button.StopAll"), this);
	toolbar->addWidget(addButton);
	toolbar->addWidget(startAllButton_);
	toolbar->addWidget(stopAllButton_);
	toolbar->addStretch(1);

	summary_ = new QLabel(this);
	summary_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	applyDimColor(summary_);
	toolbar->addWidget(summary_);
	root->addLayout(toolbar);

	auto *line = new QFrame(this);
	line->setFrameShape(QFrame::HLine);
	line->setFrameShadow(QFrame::Sunken);
	root->addWidget(line);

	auto *scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);

	auto *container = new QWidget(scroll);
	rowLayout_ = new QVBoxLayout(container);
	rowLayout_->setContentsMargins(0, 0, 0, 0);
	rowLayout_->setSpacing(6);

	emptyHint_ = new QLabel(text("Label.Empty"), container);
	emptyHint_->setWordWrap(true);
	emptyHint_->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
	applyDimColor(emptyHint_);
	rowLayout_->addWidget(emptyHint_);
	rowLayout_->addStretch(1);

	scroll->setWidget(container);
	root->addWidget(scroll, 1);

	QObject::connect(addButton, &QPushButton::clicked, this,
			 guarded("add destination", [this]() { addDestination(defaultDestination()); }));
	QObject::connect(startAllButton_, &QPushButton::clicked, this, guarded("start all", [this]() { startAll(); }));
	QObject::connect(stopAllButton_, &QPushButton::clicked, this, guarded("stop all", [this]() { stopAll(); }));

	destinations_ = loadDestinations();
	for (const Destination &destination : destinations_)
		createRow(destination);
	emptyHint_->setVisible(destinations_.empty());
	updateSummary();

	timer_ = new QTimer(this);
	timer_->setInterval(1000);
	QObject::connect(timer_, &QTimer::timeout, this, guarded("dock tick", [this]() { tick(); }));
	timer_->start();
}

RestreamDock::~RestreamDock()
{
	if (isShuttingDown())
		return;

	shutdown();
}

void RestreamDock::tick()
{
	for (TargetRow *row : rows_)
		row->tick();

	updateSummary();
}

void RestreamDock::updateSummary()
{
	int live = 0;
	int active = 0;
	double kbps = 0.0;

	for (TargetRow *row : rows_) {
		if (row->active())
			active++;
		if (row->isLive()) {
			live++;
			kbps += row->lastBitrateKbps();
		}
	}

	QString summary;
	if (rows_.empty())
		summary = text("Status.Summary.Empty");
	else if (live == 0)
		summary = text("Status.Summary.Idle");
	else
		summary = text("Status.Summary", QString::number(live), QString::number(rows_.size()))
				  .arg(text("Metrics.Mbps", QString::number(bitrateMbps(kbps), 'f', 1)));
	summary_->setText(summary);

	startAllButton_->setEnabled(active < static_cast<int>(rows_.size()));
	stopAllButton_->setEnabled(active > 0);
}

TargetRow *RestreamDock::createRow(const Destination &destination)
{
	auto *row = new TargetRow(destination);
	rowLayout_->insertWidget(rowLayout_->count() - 1, row);
	rows_.push_back(row);

	QObject::connect(row->startButton(), &QPushButton::clicked, row, guarded("start/stop destination", [row]() {
				 if (row->active())
					 row->stop();
				 else
					 row->start();
			 }));
	QObject::connect(row->editButton(), &QPushButton::clicked, this,
			 guarded("edit destination", [this, row]() { editDestination(row->id()); }));
	QObject::connect(row->removeButton(), &QPushButton::clicked, this,
			 guarded("remove destination", [this, row]() { removeDestination(row->id()); }));
	return row;
}

void RestreamDock::reload()
{
	for (TargetRow *row : rows_) {
		if (row->hasOutput())
			row->forceStop();
		delete row;
	}
	rows_.clear();
	destinations_.clear();

	destinations_ = loadDestinations();
	for (const Destination &destination : destinations_)
		createRow(destination);
	emptyHint_->setVisible(destinations_.empty());
	updateSummary();
}

void RestreamDock::addDestination(Destination destination)
{
	EditDialog dialog(destination, this);
	if (dialog.exec() != QDialog::Accepted)
		return;

	Destination updated = dialog.result();
	if (updated.id.empty())
		updated.id = destination.id;
	destinations_.push_back(updated);

	createRow(updated);
	emptyHint_->setVisible(false);
	updateSummary();
	save();
}

void RestreamDock::editDestination(const std::string &id)
{
	Destination *existing = findDestination(id);
	TargetRow *row = findRow(rows_, id);
	if (!existing || !row)
		return;

	EditDialog dialog(*existing, this);
	if (dialog.exec() != QDialog::Accepted)
		return;

	*existing = dialog.result();
	row->setDestination(*existing);
	updateSummary();
	save();
}

void RestreamDock::removeDestination(const std::string &id)
{
	TargetRow *row = findRow(rows_, id);
	if (!row)
		return;

	const QString name = QString::fromUtf8(row->destination().name.c_str());
	if (QMessageBox::question(this, text("Title"), text("Question.Remove", name),
				  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
		return;

	if (row->hasOutput())
		row->forceStop();

	rows_.erase(std::remove(rows_.begin(), rows_.end(), row), rows_.end());
	destinations_.erase(std::remove_if(destinations_.begin(), destinations_.end(),
					   [&id](const Destination &destination) { return destination.id == id; }),
			    destinations_.end());

	row->deleteLater();
	emptyHint_->setVisible(destinations_.empty());
	updateSummary();
	save();
}

void RestreamDock::startAll()
{
	for (TargetRow *row : rows_) {
		if (!row->hasOutput())
			row->start();
	}
	updateSummary();
}

void RestreamDock::stopAll()
{
	for (TargetRow *row : rows_)
		row->stop();
	updateSummary();
}

void RestreamDock::shutdown()
{
	if (rows_.empty() && destinations_.empty())
		return;

	save();
	for (TargetRow *row : rows_) {
		if (row->hasOutput())
			row->forceStop();
	}
}

void RestreamDock::save()
{
	saveDestinations(destinations_);
}

void RestreamDock::onFrontendEvent(int event)
{
	switch (static_cast<obs_frontend_event>(event)) {
	case OBS_FRONTEND_EVENT_STREAMING_STARTING:
		for (TargetRow *row : rows_) {
			if (row->destination().syncStart && !row->hasOutput())
				row->start();
		}
		updateSummary();
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPING:
		for (TargetRow *row : rows_) {
			if (row->destination().syncStop)
				row->stop();
		}
		updateSummary();
		break;
	case OBS_FRONTEND_EVENT_PROFILE_CHANGING:
		save();
		break;
	case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
		reload();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		shutdown();
		break;
	default:
		break;
	}
}

Destination *RestreamDock::findDestination(const std::string &id)
{
	for (Destination &destination : destinations_) {
		if (destination.id == id)
			return &destination;
	}
	return nullptr;
}

} // namespace manycast
