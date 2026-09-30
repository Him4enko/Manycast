#pragma once

#include "core/destination.hpp"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QRadioButton;
class QSpinBox;

namespace manycast {

class EditDialog : public QDialog {
public:
	explicit EditDialog(const Destination &destination, QWidget *parent = nullptr);

	Destination result() const;

private:
	void accept() override;
	void reloadServers();
	void updateModeVisibility();
	void updateEncoderVisibility();
	void updateCanvasMode();
	void updateInheritedValues();
	bool isCustomMode() const;
	bool isPortraitMode() const;

	Destination destination_;

	QLineEdit *name_ = nullptr;

	QComboBox *mode_ = nullptr;

	QLabel *platformLabel_ = nullptr;
	QComboBox *platform_ = nullptr;
	QLabel *serverLabel_ = nullptr;
	QComboBox *server_ = nullptr;

	QLabel *urlLabel_ = nullptr;
	QLineEdit *url_ = nullptr;

	QLineEdit *streamKey_ = nullptr;
	QCheckBox *showKey_ = nullptr;

	QLabel *canvasLabel_ = nullptr;
	QComboBox *canvas_ = nullptr;
	QLabel *canvasHint_ = nullptr;

	QRadioButton *sharedEncoder_ = nullptr;
	QRadioButton *ownEncoder_ = nullptr;
	QWidget *ownOptions_ = nullptr;
	QComboBox *videoEncoder_ = nullptr;
	QSpinBox *videoBitrate_ = nullptr;
	QSpinBox *scaleWidth_ = nullptr;
	QSpinBox *scaleHeight_ = nullptr;
	QSpinBox *fpsDivisor_ = nullptr;
	QSpinBox *audioBitrate_ = nullptr;

	QCheckBox *syncStart_ = nullptr;
	QCheckBox *syncStop_ = nullptr;
	QSpinBox *delay_ = nullptr;
	QCheckBox *preserveDelay_ = nullptr;
	QSpinBox *retries_ = nullptr;
	QSpinBox *retryDelay_ = nullptr;
};

} // namespace manycast
