#include <QApplication>
#include <QComboBox>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSizePolicy>
#include <QThread>
#include <QVBoxLayout>
#include <QWidget>
#include <QCheckBox>
#include <QCloseEvent>
#include <QFileInfo>
#include <QIcon>

#include <filesystem>
#include <memory>
#include <string>

#include "declick.hpp"

namespace
{

class NoWheelLineEdit final : public QLineEdit
{
public:
	using QLineEdit::QLineEdit;

protected:
	void wheelEvent(QWheelEvent* event) override
	{
		event->ignore();
	}
};


class NoWheelComboBox final : public QComboBox
{
public:
	using QComboBox::QComboBox;

protected:
	void wheelEvent(QWheelEvent* event) override
	{
		event->ignore();
	}
};


class NoWheelSpinBox final : public QSpinBox
{
public:
	using QSpinBox::QSpinBox;

protected:
	void wheelEvent(QWheelEvent* event) override
	{
		event->ignore();
	}
};



class MainWindow final : public QWidget
{
public:

	MainWindow()
	{
		setWindowTitle("Lick Clean");
		setMinimumSize(620, 480);
		resize(840, 620);

		dark_theme_ =
			QApplication::palette().color(QPalette::Window).lightness() < 128;

		build_ui();
		apply_style();

		theme_button_->setText(dark_theme_ ? "☀ Day mode" : "☾ Night mode");

		connect(theme_button_, &QPushButton::clicked, this, [this]
		{
			dark_theme_ = !dark_theme_;
			theme_button_->setText(dark_theme_ ? "☀ Day mode" : "☾ Night mode");
			apply_style();
		});

		connect(input_edit_, &QLineEdit::textChanged, this, [this]
		{
			if (output_edit_->text().isEmpty() && !input_edit_->text().isEmpty()) {
				const QFileInfo input(input_edit_->text());
				output_edit_->setText(
					input.absolutePath() + "/" + input.completeBaseName() + ".processed." +
					input.suffix());
			}

			update_start_state();
		});

		connect(output_edit_, &QLineEdit::textChanged, this, [this]
		{
			update_start_state();
		});

		connect(start_button_, &QPushButton::clicked, this, [this]
		{
			start_processing();
		});
	}

	~MainWindow() override
	{
		if (worker_ && worker_->isRunning()) {
			worker_->requestInterruption();
			worker_->wait();
		}
	}

protected:
	void closeEvent(QCloseEvent* event) override
	{
		if (worker_ && worker_->isRunning()) {
			event->ignore();
			status_label_->setText("Please wait for processing to finish.");
			return;
		}

		event->accept();
	}

private:
	QLineEdit* input_edit_ = nullptr;
	QLineEdit* output_edit_ = nullptr;
	QLineEdit* report_edit_ = nullptr;
	QComboBox* preset_combo_ = nullptr;
	QSpinBox* opus_bitrate_spin_ = nullptr;
	QSpinBox* mp3_bitrate_spin_ = nullptr;
	QCheckBox* dry_run_check_ = nullptr;
	QPushButton* start_button_ = nullptr;
	QPushButton* cancel_button_ = nullptr;
	QPushButton* theme_button_ = nullptr;
	QProgressBar* progress_bar_ = nullptr;
	QLabel* status_label_ = nullptr;
	QLabel* details_label_ = nullptr;
	QThread* worker_ = nullptr;
	bool dark_theme_ = true;

	void build_ui()
	{
		auto* outer_layout = new QVBoxLayout(this);
		outer_layout->setContentsMargins(0, 0, 0, 0);
		outer_layout->setSpacing(0);

		auto* scroll_area = new QScrollArea;
		scroll_area->setWidgetResizable(true);
		scroll_area->setFrameShape(QFrame::NoFrame);
		scroll_area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		scroll_area->setObjectName("scrollArea");

		auto* page = new QWidget;
		page->setObjectName("page");

		auto* root = new QVBoxLayout(page);
		root->setContentsMargins(42, 34, 42, 34);
		root->setSpacing(22);

		auto* heading_layout = new QVBoxLayout;
		heading_layout->setSpacing(5);

		auto* title = new QLabel("Lick Clean");
		title->setObjectName("title");

		auto* subtitle = new QLabel(
			"Restore your recordings by removing clicks, pops, and short transient artifacts.");
		subtitle->setObjectName("subtitle");
		subtitle->setWordWrap(true);

		heading_layout->addWidget(title);
		heading_layout->addWidget(subtitle);

		auto* heading_row = new QHBoxLayout;
		heading_row->setSpacing(18);
		heading_row->addLayout(heading_layout, 1);

		theme_button_ = new QPushButton;
		theme_button_->setObjectName("secondaryButton");
		theme_button_->setToolTip("Switch between the day and night themes");
		theme_button_->setMinimumWidth(125);

		heading_row->addWidget(theme_button_, 0, Qt::AlignTop);
		root->addLayout(heading_row);

		auto* files_group = new QGroupBox("Audio files");
		auto* files_layout = new QGridLayout(files_group);
		files_layout->setContentsMargins(20, 22, 20, 20);
		files_layout->setHorizontalSpacing(12);
		files_layout->setVerticalSpacing(14);

		input_edit_ = new NoWheelLineEdit;
		input_edit_->setPlaceholderText("Choose a WAV, Opus, or MP3 file…");

		auto* input_button = new QPushButton("Browse");
		input_button->setObjectName("secondaryButton");

		output_edit_ = new NoWheelLineEdit;
		output_edit_->setPlaceholderText("Choose where the processed file should be saved…");

		auto* output_button = new QPushButton("Browse");
		output_button->setObjectName("secondaryButton");

		report_edit_ = new NoWheelLineEdit;
		report_edit_->setPlaceholderText("Optional CSV report path");

		auto* report_button = new QPushButton("Browse");
		report_button->setObjectName("secondaryButton");

		files_layout->addWidget(new QLabel("Input"), 0, 0);
		files_layout->addWidget(input_edit_, 0, 1);
		files_layout->addWidget(input_button, 0, 2);

		files_layout->addWidget(new QLabel("Output"), 1, 0);
		files_layout->addWidget(output_edit_, 1, 1);
		files_layout->addWidget(output_button, 1, 2);

		files_layout->addWidget(new QLabel("Report"), 2, 0);
		files_layout->addWidget(report_edit_, 2, 1);
		files_layout->addWidget(report_button, 2, 2);

		files_layout->setColumnStretch(1, 1);
		root->addWidget(files_group);

		auto* settings_group = new QGroupBox("Processing");
		auto* settings_layout = new QGridLayout(settings_group);
		settings_layout->setContentsMargins(20, 22, 20, 20);
		settings_layout->setHorizontalSpacing(14);
		settings_layout->setVerticalSpacing(14);

		preset_combo_ = new NoWheelComboBox;
		preset_combo_->addItem("ASMR — balanced", "asmr");
		preset_combo_->addItem("Conservative — preserve more detail", "conservative");
		preset_combo_->addItem("Aggressive — remove stronger artifacts", "aggressive");
		preset_combo_->addItem("Clip only — repair clipping", "clip-only");

		opus_bitrate_spin_ = new NoWheelSpinBox;
		opus_bitrate_spin_->setRange(6, 512);
		opus_bitrate_spin_->setValue(192);
		opus_bitrate_spin_->setSuffix(" kbps");
		opus_bitrate_spin_->setToolTip("Target bitrate used when writing Opus output.");

		mp3_bitrate_spin_ = new NoWheelSpinBox;
		mp3_bitrate_spin_->setRange(8, 320);
		mp3_bitrate_spin_->setValue(192);
		mp3_bitrate_spin_->setSuffix(" kbps");
		mp3_bitrate_spin_->setToolTip("Target bitrate used when writing MP3 output.");

		dry_run_check_ = new QCheckBox("Analyze only — do not write an output file");

		settings_layout->addWidget(new QLabel("Preset"), 0, 0);
		settings_layout->addWidget(preset_combo_, 0, 1);

		settings_layout->addWidget(new QLabel("Opus bitrate"), 1, 0);
		settings_layout->addWidget(opus_bitrate_spin_, 1, 1);

		settings_layout->addWidget(new QLabel("MP3 bitrate"), 2, 0);
		settings_layout->addWidget(mp3_bitrate_spin_, 2, 1);

		settings_layout->addWidget(dry_run_check_, 3, 0, 1, 2);
		settings_layout->setColumnStretch(1, 1);

		root->addWidget(settings_group);

		auto* progress_card = new QFrame;
		progress_card->setObjectName("progressCard");

		auto* progress_layout = new QVBoxLayout(progress_card);
		progress_layout->setContentsMargins(20, 18, 20, 18);
		progress_layout->setSpacing(10);

		status_label_ = new QLabel("Ready to clean your recording.");
		status_label_->setObjectName("statusLabel");

		progress_bar_ = new QProgressBar;
		progress_bar_->setRange(0, 100);
		progress_bar_->setValue(0);
		progress_bar_->setTextVisible(true);
		progress_bar_->setFormat("%p%");

		details_label_ = new QLabel;
		details_label_->setObjectName("detailsLabel");
		details_label_->setWordWrap(true);

		progress_layout->addWidget(status_label_);
		progress_layout->addWidget(progress_bar_);
		progress_layout->addWidget(details_label_);

		root->addWidget(progress_card);

		auto* actions_layout = new QHBoxLayout;
		actions_layout->addStretch();

		cancel_button_ = new QPushButton("Reset");
		cancel_button_->setObjectName("secondaryButton");
		cancel_button_->setEnabled(false);

		start_button_ = new QPushButton("Start cleaning");
		start_button_->setObjectName("primaryButton");
		start_button_->setMinimumWidth(160);

		actions_layout->addWidget(cancel_button_);
		actions_layout->addWidget(start_button_);
		root->addLayout(actions_layout);

		root->addStretch();

		scroll_area->setWidget(page);
		outer_layout->addWidget(scroll_area);

		connect(input_button, &QPushButton::clicked, this, [this]
		{
			const QString path = QFileDialog::getOpenFileName(
				this,
				"Choose input audio",
				QString(),
				"Audio files (*.wav *.opus *.mp3)");
			if (!path.isEmpty())
				input_edit_->setText(path);
		});

		connect(output_button, &QPushButton::clicked, this, [this]
		{
			const QString path = QFileDialog::getSaveFileName(
				this,
				"Choose output audio",
				output_edit_->text(),
				"Audio files (*.wav *.opus *.mp3)");
			if (!path.isEmpty())
				output_edit_->setText(path);
		});

		connect(report_button, &QPushButton::clicked, this, [this]
		{
			const QString path = QFileDialog::getSaveFileName(
				this,
				"Choose CSV report",
				report_edit_->text(),
				"CSV files (*.csv)");
			if (!path.isEmpty())
				report_edit_->setText(path);
		});

		connect(cancel_button_, &QPushButton::clicked, this, [this]
		{
			input_edit_->clear();
			output_edit_->clear();
			report_edit_->clear();
			progress_bar_->setValue(0);
			status_label_->setText("Ready to clean your recording.");
			details_label_->clear();
			update_start_state();
		});
	}

		void apply_style()
	{
		const QString dark_style = R"(
			QWidget {
				background: #101318;
				color: #e9edf5;
				font-family: "Inter", "Segoe UI", sans-serif;
				font-size: 14px;
			}

			QLabel {
				background: transparent;
			}

			#page, #scrollArea, QScrollArea > QWidget > QWidget {
				background: #101318;
			}

			QScrollBar:vertical {
				background: #101318;
				width: 12px;
				margin: 4px 2px 4px 0;
			}

			QScrollBar::handle:vertical {
				background: #3a4353;
				border-radius: 6px;
				min-height: 36px;
			}

			QScrollBar::handle:vertical:hover {
				background: #596680;
			}

			QScrollBar::add-line:vertical,
			QScrollBar::sub-line:vertical {
				height: 0;
			}

			#title {
				font-size: 34px;
				font-weight: 700;
				color: #ffffff;
			}

			#subtitle {
				color: #929baa;
				font-size: 15px;
			}

			QGroupBox {
				background: #171b22;
				border: 1px solid #272d38;
				border-radius: 14px;
				margin-top: 10px;
				padding-top: 13px;
				font-weight: 600;
				color: #cfd6e4;
			}

			QGroupBox::title {
				subcontrol-origin: margin;
				left: 16px;
				padding: 0 7px;
				color: #aeb8ca;
			}

			QLineEdit, QComboBox {
				background: #0e1116;
				border: 1px solid #303745;
				border-radius: 8px;
				padding: 9px 11px;
				color: #f1f4fa;
				selection-background-color: #6578ff;
			}

			QLineEdit:focus, QComboBox:focus {
				border: 1px solid #7384ff;
			}

			QComboBox::drop-down {
				border: none;
				width: 28px;
			}

			QPushButton {
				border: none;
				border-radius: 8px;
				padding: 10px 16px;
				font-weight: 600;
			}

			#primaryButton {
				background: #7283ff;
				color: white;
			}

			#primaryButton:hover {
				background: #8492ff;
			}

			#primaryButton:disabled {
				background: #3e4658;
				color: #8e97aa;
			}

			#secondaryButton {
				background: #252c38;
				color: #d7deeb;
			}

			#secondaryButton:hover {
				background: #303948;
			}

			#secondaryButton:disabled {
				color: #697386;
			}

			QCheckBox {
				color: #aeb8ca;
				spacing: 8px;
			}

			QCheckBox::indicator {
				width: 17px;
				height: 17px;
			}

			#progressCard {
				background: #171b22;
				border: 1px solid #272d38;
				border-radius: 14px;
			}

			#statusLabel {
				font-weight: 600;
				color: #f1f4fa;
			}

			#detailsLabel {
				color: #8993a5;
				font-size: 12px;
			}

			QProgressBar {
				background: #0d1015;
				border: none;
				border-radius: 5px;
				height: 10px;
				text-align: center;
				color: transparent;
			}

			QProgressBar::chunk {
				background: #7283ff;
				border-radius: 5px;
			}
		)";

		const QString light_style = R"(
			QWidget {
				background: #f5f7fb;
				color: #202633;
				font-family: "Inter", "Segoe UI", sans-serif;
				font-size: 14px;
			}

			QLabel {
				background: transparent;
			}

			#page, #scrollArea, QScrollArea > QWidget > QWidget {
				background: #f5f7fb;
			}

			QScrollBar:vertical {
				background: #edf0f6;
				width: 12px;
				margin: 4px 2px 4px 0;
			}

			QScrollBar::handle:vertical {
				background: #c4ccdb;
				border-radius: 6px;
				min-height: 36px;
			}

			QScrollBar::handle:vertical:hover {
				background: #aab5c8;
			}

			QScrollBar::add-line:vertical,
			QScrollBar::sub-line:vertical {
				height: 0;
			}

			#title {
				font-size: 34px;
				font-weight: 700;
				color: #172033;
			}

			#subtitle {
				color: #68748a;
				font-size: 15px;
			}

			QGroupBox {
				background: #ffffff;
				border: 1px solid #dce2ec;
				border-radius: 14px;
				margin-top: 10px;
				padding-top: 13px;
				font-weight: 600;
				color: #39445a;
			}

			QGroupBox::title {
				subcontrol-origin: margin;
				left: 16px;
				padding: 0 7px;
				color: #637087;
			}

			QLineEdit, QComboBox {
				background: #fbfcfe;
				border: 1px solid #cbd3e0;
				border-radius: 8px;
				padding: 9px 11px;
				color: #202633;
				selection-background-color: #7283ff;
			}

			QLineEdit:focus, QComboBox:focus {
				border: 1px solid #7283ff;
			}

			QComboBox::drop-down {
				border: none;
				width: 28px;
			}

			QPushButton {
				border: none;
				border-radius: 8px;
				padding: 10px 16px;
				font-weight: 600;
			}

			#primaryButton {
				background: #6678ed;
				color: white;
			}

			#primaryButton:hover {
				background: #7788f4;
			}

			#primaryButton:disabled {
				background: #cbd2e0;
				color: #8490a5;
			}

			#secondaryButton {
				background: #e7ebf2;
				color: #39445a;
			}

			#secondaryButton:hover {
				background: #dce2ec;
			}

			#secondaryButton:disabled {
				color: #9da7b8;
			}

			QCheckBox {
				color: #566278;
				spacing: 8px;
			}

			QCheckBox::indicator {
				width: 17px;
				height: 17px;
			}

			#progressCard {
				background: #ffffff;
				border: 1px solid #dce2ec;
				border-radius: 14px;
			}

			#statusLabel {
				font-weight: 600;
				color: #202633;
			}

			#detailsLabel {
				color: #788499;
				font-size: 12px;
			}

			QProgressBar {
				background: #e7ebf2;
				border: none;
				border-radius: 5px;
				height: 10px;
				text-align: center;
				color: transparent;
			}

			QProgressBar::chunk {
				background: #7283ff;
				border-radius: 5px;
			}
		)";

		setStyleSheet(dark_theme_ ? dark_style : light_style);
	}



	void update_start_state()
	{
		const bool ready =
			!input_edit_->text().trimmed().isEmpty() &&
			!output_edit_->text().trimmed().isEmpty() &&
			(!worker_ || !worker_->isRunning());

		start_button_->setEnabled(ready);
		cancel_button_->setEnabled(
			!input_edit_->text().isEmpty() ||
			!output_edit_->text().isEmpty() ||
			!report_edit_->text().isEmpty());
	}

	void start_processing()
	{
		if (worker_ && worker_->isRunning())
			return;

		options config;
		config.input = input_edit_->text().toStdString();
		config.output = output_edit_->text().toStdString();
		config.preset = preset_combo_->currentData().toString().toStdString();
		config.opus_bitrate_kbps =
			static_cast<std::size_t>(opus_bitrate_spin_->value());
		config.mp3_bitrate_kbps =
			static_cast<std::size_t>(mp3_bitrate_spin_->value());
		config.dry_run = dry_run_check_->isChecked();

		if (!report_edit_->text().trimmed().isEmpty())
			config.report = report_edit_->text().toStdString();

		start_button_->setEnabled(false);
		cancel_button_->setEnabled(false);
		progress_bar_->setValue(0);
		status_label_->setText("Preparing audio…");
		details_label_->clear();

		worker_ = QThread::create([this, config]
		{
			run_lick_clean(config, [this](const progress_info& info)
			{
				QMetaObject::invokeMethod(this, [this, info]
				{
					handle_progress(info);
				}, Qt::QueuedConnection);
			});
		});

		connect(worker_, &QThread::finished, this, [this]
		{
			worker_->deleteLater();
			worker_ = nullptr;
			update_start_state();
		});

		worker_->start();
	}

	void handle_progress(const progress_info& info)
	{
		switch (info.status_) {
		case progress_info::status::started:
			status_label_->setText("Starting…");
			break;

		case progress_info::status::in_progress: {
			status_label_->setText("Cleaning audio…");

			int percentage = 0;
			if (info.total_frames > 0) {
				percentage = static_cast<int>(
					100.0 * static_cast<double>(info.output_frames) /
					static_cast<double>(info.total_frames));
			}

			progress_bar_->setValue(std::clamp(percentage, 0, 100));
			details_label_->setText(
				QString("Chunk %1 • %2 / %3 frames")
					.arg(info.completed_chunks)
					.arg(info.output_frames)
					.arg(info.total_frames));
			break;
		}

		case progress_info::status::completed:
			progress_bar_->setValue(100);
			status_label_->setText("Cleaning complete");
			details_label_->setText(
				QString("%1 event(s) repaired • %2 Hz • %3 channel(s) • %4")
					.arg(info.event_count)
					.arg(info.sample_rate)
					.arg(info.channels)
					.arg(QString::fromStdString(info.repair_mode)));
			break;

		case progress_info::status::failed:
			status_label_->setText("Processing failed");
			details_label_->setText(
				QString::fromStdString(info.error));
			break;
		}
	}
};
}

int main(int argc, char* argv[])
{
	QApplication application(argc, argv);

	QApplication::setApplicationName("Lick Clean");
	QApplication::setApplicationDisplayName("Lick Clean");
	QApplication::setOrganizationName("Lick Clean");
	QApplication::setDesktopFileName("lick-clean");
	QApplication::setWindowIcon(QIcon(":/icons/app.png"));

	MainWindow window;
	window.show();

	return application.exec();
}