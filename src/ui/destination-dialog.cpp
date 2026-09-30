#include "ui/destination-dialog.hpp"
#include "common/localization.hpp"
#include "core/portrait-canvas.hpp"
#include "core/services.hpp"

#include <obs-module.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

namespace manycast {

namespace {

QLabel *formLabel(const char *key, QWidget *parent)
{
	auto *label = new QLabel(text(key), parent);
	label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	return label;
}

QWidget *spinRow(QGridLayout *layout, QWidget *parent, int row, const char *labelKey, int min, int max, int value,
		 const QString &keepText, const QString &suffix, QSpinBox **outSpin)
{
	layout->addWidget(formLabel(labelKey, parent), row, 0);

	auto *spin = new QSpinBox(parent);
	spin->setRange(min, max);
	spin->setValue(value);
	if (!keepText.isEmpty())
		spin->setSpecialValueText(keepText);
	if (!suffix.isEmpty())
		spin->setSuffix(suffix);
	layout->addWidget(spin, row, 1);

	*outSpin = spin;
	return spin;
}

} // namespace

EditDialog::EditDialog(const Destination &destination, QWidget *parent) : QDialog(parent), destination_(destination)
{
	setWindowTitle(destination_.name.empty() ? text("Dialog.AddTitle") : text("Dialog.EditTitle"));
	setMinimumWidth(520);

	auto *root = new QVBoxLayout(this);

	auto *destinationBox = new QGroupBox(text("Dialog.Group.Destination"), this);
	auto *form = new QGridLayout(destinationBox);
	form->setColumnStretch(1, 1);
	form->setHorizontalSpacing(10);
	form->setVerticalSpacing(6);
	int row = 0;

	form->addWidget(formLabel("Dialog.Name", destinationBox), row, 0);
	name_ = new QLineEdit(QString::fromUtf8(destination_.name.c_str()), destinationBox);
	name_->setPlaceholderText(text("Dialog.NamePlaceholder"));
	form->addWidget(name_, row++, 1);

	form->addWidget(formLabel("Dialog.Mode", destinationBox), row, 0);
	mode_ = new QComboBox(destinationBox);
	mode_->addItem(text("Dialog.Mode.Platform"), static_cast<int>(ServiceMode::Platform));
	mode_->addItem(text("Dialog.Mode.Custom"), static_cast<int>(ServiceMode::Custom));
	mode_->setCurrentIndex(destination_.serviceMode == ServiceMode::Custom ? 1 : 0);
	form->addWidget(mode_, row++, 1);

	platformLabel_ = formLabel("Dialog.Platform", destinationBox);
	form->addWidget(platformLabel_, row, 0);
	platform_ = new QComboBox(destinationBox);
	platform_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	form->addWidget(platform_, row++, 1);

	serverLabel_ = formLabel("Dialog.Server", destinationBox);
	form->addWidget(serverLabel_, row, 0);
	server_ = new QComboBox(destinationBox);
	server_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	form->addWidget(server_, row++, 1);

	urlLabel_ = formLabel("Dialog.ServerUrl", destinationBox);
	form->addWidget(urlLabel_, row, 0);
	url_ = new QLineEdit(QString::fromUtf8(destination_.serverUrl.c_str()), destinationBox);
	url_->setPlaceholderText(QStringLiteral("rtmp://a.rtmp.youtube.com/live2"));
	form->addWidget(url_, row++, 1);

	form->addWidget(formLabel("Dialog.StreamKey", destinationBox), row, 0);
	streamKey_ = new QLineEdit(QString::fromUtf8(destination_.streamKey.c_str()), destinationBox);
	streamKey_->setEchoMode(QLineEdit::Password);
	streamKey_->setPlaceholderText(text("Dialog.StreamKey.Placeholder"));
	form->addWidget(streamKey_, row, 1);
	showKey_ = new QCheckBox(text("Button.ShowKey"), destinationBox);
	form->addWidget(showKey_, row++, 2);

	canvasLabel_ = formLabel("Dialog.Canvas", destinationBox);
	form->addWidget(canvasLabel_, row, 0);
	canvas_ = new QComboBox(destinationBox);
	canvas_->addItem(text("Dialog.Canvas.Main"), static_cast<int>(CanvasMode::Main));
	canvas_->addItem(text("Dialog.Canvas.Portrait"), static_cast<int>(CanvasMode::Portrait));
	canvas_->setCurrentIndex(destination_.canvasMode == CanvasMode::Portrait ? 1 : 0);
	form->addWidget(canvas_, row++, 1);

	canvasHint_ = new QLabel(text("Dialog.Canvas.Hint"), destinationBox);
	canvasHint_->setWordWrap(true);
	form->addWidget(canvasHint_, row++, 1, 1, 2);

	root->addWidget(destinationBox);

	for (const NamedValue &platform : listPlatforms())
		platform_->addItem(QString::fromUtf8(platform.name.c_str()), QString::fromUtf8(platform.value.c_str()));
	const int platformIndex = platform_->findData(QString::fromUtf8(destination_.platform.c_str()));
	if (platformIndex >= 0)
		platform_->setCurrentIndex(platformIndex);
	reloadServers();

	auto *encoderBox = new QGroupBox(text("Dialog.Group.Encoder"), this);
	auto *encoderLayout = new QGridLayout(encoderBox);
	encoderLayout->setColumnStretch(0, 1);

	sharedEncoder_ = new QRadioButton(text("Dialog.Encoder.Shared"), encoderBox);
	ownEncoder_ = new QRadioButton(text("Dialog.Encoder.Own"), encoderBox);
	sharedEncoder_->setChecked(destination_.encoderMode != EncoderMode::Own);
	ownEncoder_->setChecked(destination_.encoderMode == EncoderMode::Own);
	encoderLayout->addWidget(sharedEncoder_, 0, 0, 1, 2);
	encoderLayout->addWidget(ownEncoder_, 1, 0, 1, 2);

	ownOptions_ = new QWidget(encoderBox);
	auto *ownLayout = new QGridLayout(ownOptions_);
	ownLayout->setContentsMargins(24, 4, 0, 0);
	ownLayout->setColumnStretch(1, 1);
	ownLayout->setHorizontalSpacing(10);
	ownLayout->setVerticalSpacing(6);

	ownLayout->addWidget(formLabel("Dialog.VideoEncoder", ownOptions_), 0, 0);
	videoEncoder_ = new QComboBox(ownOptions_);
	videoEncoder_->addItem(text("Dialog.Encoder.SameAsMain"), QString());
	for (const NamedValue &encoder : listVideoEncoders())
		videoEncoder_->addItem(QString::fromUtf8(encoder.name.c_str()),
				       QString::fromUtf8(encoder.value.c_str()));
	const int encoderIndex = videoEncoder_->findData(QString::fromUtf8(destination_.videoEncoderId.c_str()));
	if (encoderIndex > 0)
		videoEncoder_->setCurrentIndex(encoderIndex);
	ownLayout->addWidget(videoEncoder_, 0, 1);

	spinRow(ownLayout, ownOptions_, 1, "Dialog.VideoBitrate", 0, 100000, destination_.videoBitrate,
		text("Common.KeepDefault"), QStringLiteral(" kbps"), &videoBitrate_);
	spinRow(ownLayout, ownOptions_, 2, "Dialog.ScaleWidth", 0, 7680, destination_.scaleWidth,
		text("Common.KeepDefault"), QStringLiteral(" px"), &scaleWidth_);
	spinRow(ownLayout, ownOptions_, 3, "Dialog.ScaleHeight", 0, 7680, destination_.scaleHeight,
		text("Common.KeepDefault"), QStringLiteral(" px"), &scaleHeight_);
	spinRow(ownLayout, ownOptions_, 4, "Dialog.FpsDivisor", 1, 10, destination_.fpsDivisor, QString(), QString(),
		&fpsDivisor_);
	spinRow(ownLayout, ownOptions_, 5, "Dialog.AudioBitrate", 0, 1024, destination_.audioBitrate,
		text("Common.KeepDefault"), QStringLiteral(" kbps"), &audioBitrate_);

	encoderLayout->addWidget(ownOptions_, 2, 0, 1, 2);
	root->addWidget(encoderBox);

	auto *streamBox = new QGroupBox(text("Dialog.Group.Stream"), this);
	auto *streamLayout = new QGridLayout(streamBox);
	streamLayout->setColumnStretch(1, 1);
	streamLayout->setHorizontalSpacing(10);
	streamLayout->setVerticalSpacing(6);

	syncStart_ = new QCheckBox(text("Dialog.SyncStart"), streamBox);
	syncStart_->setChecked(destination_.syncStart);
	streamLayout->addWidget(syncStart_, 0, 0, 1, 2);

	syncStop_ = new QCheckBox(text("Dialog.SyncStop"), streamBox);
	syncStop_->setChecked(destination_.syncStop);
	streamLayout->addWidget(syncStop_, 1, 0, 1, 2);

	spinRow(streamLayout, streamBox, 2, "Dialog.Delay", 0, 3600, destination_.delaySeconds, text("Common.Off"),
		QStringLiteral(" s"), &delay_);
	preserveDelay_ = new QCheckBox(text("Dialog.PreserveDelay"), streamBox);
	preserveDelay_->setChecked(destination_.delayPreserve);
	streamLayout->addWidget(preserveDelay_, 3, 0, 1, 2);

	spinRow(streamLayout, streamBox, 4, "Dialog.Retries", 0, 100, destination_.reconnectRetries, QString(),
		QString(), &retries_);
	spinRow(streamLayout, streamBox, 5, "Dialog.RetryDelay", 1, 60, destination_.reconnectDelaySeconds, QString(),
		QStringLiteral(" s"), &retryDelay_);

	root->addWidget(streamBox);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	root->addWidget(buttons);

	QObject::connect(buttons, &QDialogButtonBox::accepted, this, [this]() { accept(); });
	QObject::connect(buttons, &QDialogButtonBox::rejected, this, [this]() { reject(); });
	QObject::connect(mode_, &QComboBox::currentIndexChanged, this, [this]() { updateModeVisibility(); });
	QObject::connect(platform_, &QComboBox::currentIndexChanged, this, [this]() { reloadServers(); });
	QObject::connect(ownEncoder_, &QRadioButton::toggled, this, [this]() { updateEncoderVisibility(); });
	QObject::connect(canvas_, &QComboBox::currentIndexChanged, this, [this]() { updateCanvasMode(); });
	QObject::connect(showKey_, &QCheckBox::toggled, this, [this](bool shown) {
		streamKey_->setEchoMode(shown ? QLineEdit::Normal : QLineEdit::Password);
	});

	if (platform_->count() == 0)
		mode_->setCurrentIndex(1);

	updateModeVisibility();
	updateEncoderVisibility();
	updateCanvasMode();
	updateInheritedValues();
}

void EditDialog::updateInheritedValues()
{
	const MainEncoderInfo main = mainEncoderInfo();

	int width = main.width;
	int height = main.height;
	if (isPortraitMode()) {
		PortraitCanvas &portraitCanvasInstance = portraitCanvas();
		if (portraitCanvasInstance.ready()) {
			width = static_cast<int>(portraitCanvasInstance.width());
			height = static_cast<int>(portraitCanvasInstance.height());
		}
	}

	videoBitrate_->setSpecialValueText(main.videoBitrate > 0
						   ? text("Common.InheritFromMain", QString::number(main.videoBitrate))
						   : text("Common.KeepDefault"));
	audioBitrate_->setSpecialValueText(main.audioBitrate > 0
						   ? text("Common.InheritFromMain", QString::number(main.audioBitrate))
						   : text("Common.KeepDefault"));
	scaleWidth_->setSpecialValueText(width > 0 ? text("Common.InheritFromMain", QString::number(width))
						   : text("Common.KeepDefault"));
	scaleHeight_->setSpecialValueText(height > 0 ? text("Common.InheritFromMain", QString::number(height))
						     : text("Common.KeepDefault"));
	fpsDivisor_->setToolTip(text("Dialog.FpsDivisor.Hint"));
}

void EditDialog::updateCanvasMode()
{
	const bool portrait = isPortraitMode();

	if (portrait)
		ownEncoder_->setChecked(true);

	sharedEncoder_->setEnabled(!portrait);
	canvasHint_->setVisible(portrait);
	updateEncoderVisibility();
	updateInheritedValues();
}

bool EditDialog::isCustomMode() const
{
	return mode_->currentData().toInt() == static_cast<int>(ServiceMode::Custom);
}

bool EditDialog::isPortraitMode() const
{
	return canvas_->currentData().toInt() == static_cast<int>(CanvasMode::Portrait);
}

void EditDialog::reloadServers()
{
	server_->clear();
	const std::string platform = platform_->currentData().toString().toStdString();
	for (const NamedValue &entry : listPlatformServers(platform))
		server_->addItem(QString::fromUtf8(entry.name.c_str()), QString::fromUtf8(entry.value.c_str()));

	const int index = server_->findData(QString::fromUtf8(destination_.serverName.c_str()));
	server_->setCurrentIndex(index >= 0 ? index : 0);
	server_->setEnabled(server_->count() > 0);
}

void EditDialog::updateModeVisibility()
{
	const bool custom = isCustomMode();

	platformLabel_->setVisible(!custom);
	platform_->setVisible(!custom);
	serverLabel_->setVisible(!custom);
	server_->setVisible(!custom);
	urlLabel_->setVisible(custom);
	url_->setVisible(custom);
}

void EditDialog::updateEncoderVisibility()
{
	ownOptions_->setEnabled(ownEncoder_->isChecked());
}

void EditDialog::accept()
{
	Destination updated = destination_;
	updated.name = name_->text().trimmed().toStdString();
	if (updated.name.empty()) {
		QMessageBox::warning(this, text("Dialog.AddTitle"), text("Error.NoName"));
		return;
	}

	updated.serviceMode = isCustomMode() ? ServiceMode::Custom : ServiceMode::Platform;
	updated.platform = platform_->currentData().toString().toStdString();
	updated.serverName = server_->currentData().toString().toStdString();
	updated.serverUrl = url_->text().trimmed().toStdString();
	updated.streamKey = streamKey_->text().toStdString();

	if (updated.serviceMode == ServiceMode::Platform && updated.platform.empty()) {
		QMessageBox::warning(this, text("Dialog.AddTitle"), text("Error.NoPlatform"));
		return;
	}
	if (updated.serviceMode == ServiceMode::Custom && updated.serverUrl.empty()) {
		QMessageBox::warning(this, text("Dialog.AddTitle"), text("Error.NoServer"));
		return;
	}
	if (updated.streamKey.empty()) {
		QMessageBox::warning(this, text("Dialog.AddTitle"), text("Error.NoKey"));
		return;
	}

	updated.encoderMode = ownEncoder_->isChecked() ? EncoderMode::Own : EncoderMode::SharedMain;
	updated.canvasMode = isPortraitMode() ? CanvasMode::Portrait : CanvasMode::Main;
	if (updated.canvasMode == CanvasMode::Portrait)
		updated.encoderMode = EncoderMode::Own;
	updated.videoEncoderId = videoEncoder_->currentData().toString().toStdString();
	updated.videoBitrate = videoBitrate_->value();
	updated.scaleWidth = scaleWidth_->value();
	updated.scaleHeight = scaleHeight_->value();
	updated.fpsDivisor = fpsDivisor_->value();
	updated.audioBitrate = audioBitrate_->value();

	updated.syncStart = syncStart_->isChecked();
	updated.syncStop = syncStop_->isChecked();
	updated.delaySeconds = static_cast<std::uint32_t>(delay_->value());
	updated.delayPreserve = preserveDelay_->isChecked();
	updated.reconnectRetries = retries_->value();
	updated.reconnectDelaySeconds = retryDelay_->value();

	destination_ = updated;
	QDialog::accept();
}

Destination EditDialog::result() const
{
	return destination_;
}

} // namespace manycast
