/*
	This file is part of EqualizerAPO, a system-wide equalizer.
	Copyright (C) 2015  Jonas Thedering

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along
	with this program; if not, write to the Free Software Foundation, Inc.,
	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include <sstream>
#include <QDrag>
#include <QElapsedTimer>
#include <QLabel>
#include <QMimeData>
#include <QPushButton>
#include <QStandardItemModel>
#include <QStringBuilder>
#include <QScrollArea>
#include <QFileInfo>
#include <QFileDialog>
#include <QMessageBox>
#include <QProcess>
#include <QTabWidget>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QSlider>
#include <QComboBox>
#include <QCheckBox>
#include <QColorDialog>
#include <QSettings>
#include <functional>
#include <QPainter>
#include <QPixmap>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QGraphicsBlurEffect>
#include "FrequencyTuner.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include "helpers/StringHelper.h"
#include "helpers/LogHelper.h"
#include "helpers/ChannelHelper.h"
#include "Editor/helpers/GUIChannelHelper.h"
#include "Editor/helpers/GUIHelper.h"
#include "version.h"
#include "FilterTable.h"
#include "MainWindow.h"
#include "ui_MainWindow.h"

using namespace std;

static QPixmap blurPixmap(const QPixmap& source, int radius)
{
    QGraphicsScene scene;
    QGraphicsPixmapItem* item = new QGraphicsPixmapItem(source);
    QGraphicsBlurEffect* effect = new QGraphicsBlurEffect;
    effect->setBlurRadius(radius);
    item->setGraphicsEffect(effect);
    scene.addItem(item);

    QPixmap result(source.size());
    result.fill(Qt::transparent);
    QPainter painter(&result);
    scene.render(&painter, QRectF(), QRectF(0, 0, source.width(), source.height()));
    return result;
}

static QWidget* createPreferencesPage(QWidget* parent, std::function<void()> onSaved)
{
    QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);

    QTabWidget* subTabs = new QTabWidget(parent);
    QWidget* themePage = new QWidget;
    QFormLayout* form = new QFormLayout(themePage);

    QCheckBox* enableBg = new QCheckBox(QObject::tr("Enable background image"));
    enableBg->setChecked(settings.value("theme/enabled", false).toBool());
    form->addRow(enableBg);

    QLineEdit* imagePath = new QLineEdit(settings.value("theme/imagePath").toString());
    QPushButton* browse = new QPushButton(QObject::tr("Browse..."));
    QHBoxLayout* imageRow = new QHBoxLayout;
    imageRow->addWidget(imagePath);
    imageRow->addWidget(browse);
    form->addRow(QObject::tr("Background image:"), imageRow);
    QObject::connect(browse, &QPushButton::clicked, [=]() {
        QString file = QFileDialog::getOpenFileName(parent, QObject::tr("Choose image"), QString(), "Images (*.png *.jpg *.jpeg *.bmp *.webp)");
        if (!file.isEmpty())
            imagePath->setText(file);
    });

    QComboBox* scaleMode = new QComboBox;
    scaleMode->addItems({QObject::tr("Fill"), QObject::tr("Fit"), QObject::tr("Stretch"), QObject::tr("Center"), QObject::tr("Tile")});
    scaleMode->setCurrentIndex(settings.value("theme/scaleMode", 0).toInt());
    form->addRow(QObject::tr("Image scale:"), scaleMode);

    auto addSlider = [&](const QString& label, const QString& key, int min, int max, int def) {
        QSlider* slider = new QSlider(Qt::Horizontal);
        slider->setRange(min, max);
        slider->setValue(settings.value(key, def).toInt());
        QLabel* valueLabel = new QLabel(QString::number(slider->value()));
        valueLabel->setMinimumWidth(30);
        QObject::connect(slider, &QSlider::valueChanged, valueLabel, QOverload<int>::of(&QLabel::setNum));
        QHBoxLayout* row = new QHBoxLayout;
        row->addWidget(slider);
        row->addWidget(valueLabel);
        form->addRow(label, row);
        return slider;
    };

    QSlider* imageOpacity = addSlider(QObject::tr("Image opacity (%):"), "theme/imageOpacity", 0, 100, 40);
    QSlider* blur = addSlider(QObject::tr("Image blur (px):"), "theme/blur", 0, 50, 0);
    QSlider* darken = addSlider(QObject::tr("Darken image (%):"), "theme/darken", 0, 100, 0);
    QSlider* panelOpacity = addSlider(QObject::tr("Panel opacity (%):"), "theme/panelOpacity", 20, 100, 100);
    QSlider* uiAlpha = addSlider(QObject::tr("UI element alpha (%):"), "theme/uiAlpha", 0, 100, 100);

    auto addColor = [&](const QString& label, const QString& key, const QColor& def) {
        QPushButton* button = new QPushButton;
        QColor color(settings.value(key, def.name()).toString());
        button->setProperty("color", color.name());
        button->setStyleSheet(QString("background-color: %1; min-height: 20px;").arg(color.name()));
        QObject::connect(button, &QPushButton::clicked, [=]() {
            QColor chosen = QColorDialog::getColor(QColor(button->property("color").toString()), parent);
            if (chosen.isValid())
            {
                button->setProperty("color", chosen.name());
                button->setStyleSheet(QString("background-color: %1; min-height: 20px;").arg(chosen.name()));
            }
        });
        form->addRow(label, button);
        return button;
    };

    QPushButton* bgColor = addColor(QObject::tr("Background color:"), "theme/bgColor", QColor(45, 45, 45));
    QPushButton* textColor = addColor(QObject::tr("Text color:"), "theme/textColor", QColor(255, 255, 255));
    QPushButton* accentColor = addColor(QObject::tr("Accent color:"), "theme/accentColor", QColor(42, 130, 218));

    QPushButton* saveButton = new QPushButton(QObject::tr("Save preferences"));
    form->addRow(saveButton);
    QObject::connect(saveButton, &QPushButton::clicked, [=]() {
        QSettings s(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
        s.setValue("theme/enabled", enableBg->isChecked());
        s.setValue("theme/imagePath", imagePath->text());
        s.setValue("theme/scaleMode", scaleMode->currentIndex());
        s.setValue("theme/imageOpacity", imageOpacity->value());
        s.setValue("theme/blur", blur->value());
        s.setValue("theme/darken", darken->value());
        s.setValue("theme/panelOpacity", panelOpacity->value());
        s.setValue("theme/bgColor", bgColor->property("color").toString());
        s.setValue("theme/textColor", textColor->property("color").toString());
        s.setValue("theme/accentColor", accentColor->property("color").toString());
        s.setValue("theme/uiAlpha", uiAlpha->value());
        s.sync();
        onSaved();
        QMessageBox::information(parent, QObject::tr("Preferences"), QObject::tr("Preferences saved."));
    });

    subTabs->addTab(themePage, QObject::tr("Theme and Looks"));
    return subTabs;
}

MainWindow::MainWindow(QDir configDir, QWidget* parent)
	: QMainWindow(parent), ui(new Ui::MainWindow), configDir(configDir)
{
	outputDevices = toQList(DeviceAPOInfo::loadAllInfos(false));
	inputDevices = toQList(DeviceAPOInfo::loadAllInfos(true));

	defaultOutputDevice = NULL;
	for (shared_ptr<AbstractAPOInfo>& apoInfo : outputDevices)
	{
		if (apoInfo->isDefaultDevice())
		{
			defaultOutputDevice = apoInfo;
			break;
		}
	}

	ui->setupUi(this);
	resize(GUIHelper::scale(QSize(1024, 768)));

	LogHelper::set(stderr, true, false, false);

	QString version = QString("%0.%1").arg(MAJOR).arg(MINOR);
	if (REVISION != 0)
		version += QString(".%0").arg(REVISION);
	setWindowTitle(tr("Equalizer APO %0 Configuration Editor").arg(version));

	QWidget* spacer = new QWidget;
	spacer->setFixedWidth(10);
	ui->mainToolBar->addWidget(spacer);

	instantModeCheckBox = new QCheckBox(tr("Instant mode"));
	instantModeCheckBox->setChecked(true);
	instantModeCheckBox->setToolTip(tr("Changes are saved immediately"));
	connect(instantModeCheckBox, SIGNAL(toggled(bool)), this, SLOT(instantModeEnabled(bool)));
	ui->mainToolBar->addWidget(instantModeCheckBox);

	spacer = new QWidget;
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	ui->mainToolBar->addWidget(spacer);

	ui->mainToolBar->addWidget(new QLabel(tr("Device: ")));

	deviceComboBox = new QComboBox;
	connect(deviceComboBox, SIGNAL(activated(int)), this, SLOT(deviceSelected(int)));
	ui->mainToolBar->addWidget(deviceComboBox);

	spacer = new QWidget;
	spacer->setFixedWidth(10);
	ui->mainToolBar->addWidget(spacer);

	ui->mainToolBar->addWidget(new QLabel(tr("Channel configuration: ")));

	channelConfigurationComboBox = new QComboBox;
	channelConfigurationComboBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
	ui->mainToolBar->addWidget(channelConfigurationComboBox);

	QStandardItemModel* model = qobject_cast<QStandardItemModel*>(deviceComboBox->model());
	if (defaultOutputDevice != NULL)
		deviceComboBox->addItem(tr("Default") + " (" + QString::fromStdWString(defaultOutputDevice->getConnectionName()) + " - " + QString::fromStdWString(defaultOutputDevice->getDeviceName()) + ")", NULL);

	deviceComboBox->addItem(tr("Playback devices:"));
	QStandardItem* item = model->item(model->rowCount() - 1);
	QFont font = item->font();
	font.setBold(true);
	item->setFont(font);
	item->setSelectable(false);

	for (shared_ptr<AbstractAPOInfo>& apoInfo : outputDevices)
		if (apoInfo->isInstalled())
			deviceComboBox->addItem(QString::fromStdWString(apoInfo->getConnectionName()) + " - " + QString::fromStdWString(apoInfo->getDeviceName()), QVariant::fromValue(apoInfo));

	deviceComboBox->addItem(tr("Capture devices:"));
	item = model->item(model->rowCount() - 1);
	item->setFont(font);
	item->setSelectable(false);

	for (shared_ptr<AbstractAPOInfo>& apoInfo : inputDevices)
		if (apoInfo->isInstalled())
			deviceComboBox->addItem(QString::fromStdWString(apoInfo->getConnectionName()) + " - " + QString::fromStdWString(apoInfo->getDeviceName()), QVariant::fromValue(apoInfo));

	connect(channelConfigurationComboBox, SIGNAL(activated(int)), this, SLOT(channelConfigurationSelected(int)));

	analysisPlotScene = new AnalysisPlotScene(ui->graphicsView);
	ui->graphicsView->setScene(analysisPlotScene);

	analysisThread = new AnalysisThread;
	analysisThread->start();
	connect(analysisThread, SIGNAL(analysisFinished()), this, SLOT(updateAnalysisPanel()));

	QLocale autoLocale = QLocale::system();
	if (autoLocale.language() != QLocale::German && autoLocale.language() != QLocale::Chinese && autoLocale.language() != QLocale::French)
		autoLocale = QLocale("en");
	QLocale::Language languages[] = {QLocale::AnyLanguage, QLocale::English, QLocale::German, QLocale::Chinese, QLocale::French};
	for (size_t i = 0; i < sizeof(languages) / sizeof(QLocale::Language); i++)
	{
		QLocale::Language language = languages[i];
		QString languageName;
		if (language == QLocale::AnyLanguage)
			languageName = autoLocale.nativeLanguageName();
		else
			languageName = QLocale(language).nativeLanguageName();
		if (languageName == "American English")
			languageName = "English";
		QString text;
		if (language == QLocale::AnyLanguage)
			text = tr("Automatic (%0)").arg(languageName);
		else
			text = languageName;
		if(text[0].isLower())
			text[0] = text[0].toUpper();
		QAction* action = ui->menuLanguage->addAction(text);
		action->setData(language);
		action->setCheckable(true);
		connect(action, SIGNAL(triggered(bool)), this, SLOT(languageSelected(bool)));
	}

	loadPreferences();
    applyTheme();
}

MainWindow::~MainWindow()
{
	delete ui;

	delete analysisThread;
}

void MainWindow::doChecks()
{
	if (!DeviceAPOInfo::checkProtectedAudioDG(false) || !DeviceAPOInfo::checkAPORegistration(false))
	{
		if (QMessageBox::warning(this, tr("Registry problem"), tr("A registry value that is required for the operation of Equalizer APO is not set correctly.\nDo you want to run the Device Selector application to fix the problem?"), QMessageBox::Yes, QMessageBox::No) == QMessageBox::Yes)
		{
			runDeviceSelector();
			return;
		}
	}

	if (defaultOutputDevice != NULL && !defaultOutputDevice->isInstalled())
	{
		if (QMessageBox::warning(this, tr("APO not installed to device"), tr("Equalizer APO has not been installed to the selected device.\nDo you want to run the Device Selector application to fix the problem?"), QMessageBox::Yes, QMessageBox::No) == QMessageBox::Yes)
		{
			runDeviceSelector();
			return;
		}
	}

	AbstractAPOInfo* disabledApoInfo = NULL;
	for (shared_ptr<AbstractAPOInfo>& apoInfo : outputDevices)
	{
		if (apoInfo->isInstalled() && apoInfo->isEnhancementsDisabled())
		{
			disabledApoInfo = apoInfo.get();
			break;
		}
	}

	if (disabledApoInfo == NULL)
	{
		for (shared_ptr<AbstractAPOInfo>& apoInfo : inputDevices)
		{
			if (apoInfo->isInstalled() && apoInfo->isEnhancementsDisabled())
			{
				disabledApoInfo = apoInfo.get();
				break;
			}
		}
	}

	if (disabledApoInfo != NULL)
	{
		if (QMessageBox::warning(this, tr("Audio enhancements disabled"), tr("Audio enhancements are not enabled for the device\n%0 %1.\nDo you want to run the Device Selector application to fix the problem?").arg(QString::fromStdWString(disabledApoInfo->getConnectionName())).arg(QString::fromStdWString(disabledApoInfo->getDeviceName())), QMessageBox::Yes, QMessageBox::No) == QMessageBox::Yes)
		{
			runDeviceSelector();
			return;
		}
	}
}

void MainWindow::runDeviceSelector()
{
	// cannot use QProcess::startDetached because of UAC
	wstring file = (QDir::toNativeSeparators(QCoreApplication::applicationDirPath() + "/DeviceSelector.exe")).toStdWString();
	unsigned long long result = (unsigned long long)ShellExecuteW(NULL, L"open", file.c_str(), NULL, NULL, SW_SHOWNORMAL);
	if (result == SE_ERR_ACCESSDENIED)
		ShellExecuteW(NULL, L"runas", file.c_str(), NULL, NULL, SW_SHOWNORMAL);
}

void MainWindow::load(QString path)
{
	path = QDir::toNativeSeparators(path); 

	for (int i = 0; i < ui->tabWidget->count(); i++)
	{
		QScrollArea* scrollArea = qobject_cast<QScrollArea*>(ui->tabWidget->widget(i));
        if (scrollArea == NULL) continue;
		FilterTable* filterTable = qobject_cast<FilterTable*>(scrollArea->widget());


		if (filterTable->getConfigPath() == path)
		{
			ui->tabWidget->setCurrentIndex(i);
			return;
		}
	}

	QElapsedTimer timer;
	timer.start();

	HANDLE hFile = INVALID_HANDLE_VALUE;
	while (hFile == INVALID_HANDLE_VALUE)
	{
		hFile = CreateFile(path.toStdWString().c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hFile == INVALID_HANDLE_VALUE)
		{
			DWORD error = GetLastError();
			if (error != ERROR_SHARING_VIOLATION)
			{
				QMessageBox::critical(this, tr("Error"), tr("Error while reading configuration file: %0").arg(QString::fromStdWString(StringHelper::getSystemErrorString(error))));
				return;
			}

			// file is being written, so wait
			Sleep(1);
		}
	}

	stringstream inputStream;

	char buf[8192];
	unsigned long bytesRead = -1;
	while (ReadFile(hFile, buf, sizeof(buf), &bytesRead, NULL) && bytesRead != 0)
	{
		inputStream.write(buf, bytesRead);
	}

	CloseHandle(hFile);

	inputStream.seekg(0);

	QList<QString> lines;
	while (inputStream.good())
	{
		string encodedLine;
		getline(inputStream, encodedLine);
		if (encodedLine.size() > 0 && encodedLine[encodedLine.size() - 1] == '\r')
			encodedLine.resize(encodedLine.size() - 1);

		wstring line = StringHelper::toWString(encodedLine, CP_UTF8);
		if (line.find(L'\uFFFD') != wstring::npos)
			line = StringHelper::toWString(encodedLine, CP_ACP);

		lines.append(QString::fromStdWString(line));
	}

	QFileInfo fileInfo(path);
	FilterTable* filterTable = addTab(fileInfo.fileName(), QDir::toNativeSeparators(fileInfo.absoluteFilePath()), path, lines);

	connect(filterTable, SIGNAL(linesChanged()), this, SLOT(linesChanged()));

	qDebug("Loading took %.1f ms", timer.nsecsElapsed() / 1e6);

	ui->tabWidget->setCurrentIndex(ui->tabWidget->count() - 1);

	recentFiles.removeAll(path);
	recentFiles.prepend(path);
	if (recentFiles.size() > 10)
		recentFiles.removeLast();
	updateRecentFiles();
}

void MainWindow::save(FilterTable* filterTable, QString path)
{
	QElapsedTimer timer;
	timer.start();

	QList<QString> lines = filterTable->getLines();

	bool first = true;
	QByteArray byteArray;
	for (QString line : lines)
	{
		if (first)
			first = false;
		else
			byteArray.append("\r\n");
		byteArray.append(line.toUtf8());
	}

	HANDLE hFile = INVALID_HANDLE_VALUE;
	while (hFile == INVALID_HANDLE_VALUE)
	{
		hFile = CreateFile(path.toStdWString().c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hFile == INVALID_HANDLE_VALUE)
		{
			DWORD error = GetLastError();
			if (error != ERROR_SHARING_VIOLATION)
			{
				QMessageBox::critical(this, tr("Error"), tr("Error while writing configuration file: %0").arg(QString::fromStdWString(StringHelper::getSystemErrorString(error))));
				return;
			}

			// file is being written, so wait
			Sleep(1);
		}
	}

	unsigned long bytesWritten;
	WriteFile(hFile, byteArray.constData(), byteArray.length(), &bytesWritten, NULL);
	if (bytesWritten != byteArray.length())
	{
		// should never happen
		QMessageBox::critical(this, tr("Error"), tr("Only %0/%1 bytes have been written!").arg(bytesWritten).arg(byteArray.length()));
	}

	CloseHandle(hFile);

	qDebug("Saving took %.1f ms", timer.nsecsElapsed() / 1e6);

	startAnalysis();
}

bool MainWindow::isEmpty()
{
	return ui->tabWidget->count() == 0;
}

bool MainWindow::shouldRestart()
{
	return restart;
}

void MainWindow::closeEvent(QCloseEvent* event)
{
	bool canceled = false;
	for (int i = 0; i < ui->tabWidget->count(); i++)
	{
		if (!askForClose(i))
		{
			canceled = true;
			break;
		}
	}

	if (canceled)
	{
		event->ignore();
		restart = false;
		noSavePreferences = false;
		noSaveFilePreferences = false;
	}
	else
	{
		savePreferences();
	}
}

void MainWindow::deviceSelected(int index)
{
	shared_ptr<AbstractAPOInfo> apoInfo = deviceComboBox->itemData(index).value<shared_ptr<AbstractAPOInfo>>();
	if (apoInfo == NULL)
		apoInfo = defaultOutputDevice;

	channelConfigurationComboBox->clear();

	const QList<GUIChannelHelper::ChannelConfigurationInfo>& infos = GUIChannelHelper::getInstance()->getChannelConfigurationInfos();

	if (apoInfo != NULL)
	{
		const GUIChannelHelper::ChannelConfigurationInfo* selectedInfo = NULL;
		for (const GUIChannelHelper::ChannelConfigurationInfo& info : infos)
		{
			if (info.channelMask == (int)apoInfo->getChannelMask())
			{
				selectedInfo = &info;
				break;
			}
		}

		if (selectedInfo != NULL)
			channelConfigurationComboBox->addItem(tr("From device") + " (" + selectedInfo->name + ")", 0);
		else if (apoInfo->getChannelCount() != 0)
			channelConfigurationComboBox->addItem((tr("From device") + " (%1 channels)").arg(apoInfo->getChannelCount()), 0);
		else
			channelConfigurationComboBox->addItem(tr("From device") + " (? channels)", 0);
	}
	else
	{
		channelConfigurationComboBox->addItem(tr("From device") + " (?)", 0);
	}

	for (const GUIChannelHelper::ChannelConfigurationInfo& info : infos)
		channelConfigurationComboBox->addItem(info.name, info.channelMask);

	channelConfigurationSelected(channelConfigurationComboBox->currentIndex());
}

void MainWindow::channelConfigurationSelected(int index)
{
	shared_ptr<AbstractAPOInfo> selectedDevice;
	int channelMask;
	getDeviceAndChannelMask(&selectedDevice, &channelMask);

	for (int i = 0; i < ui->tabWidget->count(); i++)
	{
		QScrollArea* scrollArea = qobject_cast<QScrollArea*>(ui->tabWidget->widget(i));
        if (scrollArea == NULL) continue;
		FilterTable* filterTable = qobject_cast<FilterTable*>(scrollArea->widget());
		filterTable->updateDeviceAndChannelMask(selectedDevice, channelMask);
	}

	ui->analysisChannelComboBox->clear();

	if (selectedDevice != NULL)
	{
		unsigned channelCount = selectedDevice->getChannelCount();
		if (channelMask != 0 && channelMask != (int)selectedDevice->getChannelMask())
		{
			channelCount = 0;
			for (int i = 0; i < 31; i++)
			{
				int channelPos = 1 << i;
				if (channelMask & channelPos)
					channelCount++;
			}
		}
		if (channelCount == 0)
		{
			channelCount = 8;
			channelMask = KSAUDIO_SPEAKER_7POINT1_SURROUND;
		}

		vector<wstring> channelNames = ChannelHelper::getChannelNames(channelCount, channelMask);
		for (const wstring& channelName : channelNames)
		{
			ui->analysisChannelComboBox->addItem(QString::fromStdWString(channelName));
		}
	}

	startAnalysis();
}

void MainWindow::linesChanged()
{
	FilterTable* filterTable = qobject_cast<FilterTable*>(sender());

	if (instantModeCheckBox->isChecked())
	{
		QString configPath = filterTable->getConfigPath();
		if (configPath.length() > 0)
		{
			save(filterTable, configPath);
			return;
		}
	}

	int tabIndex = -1;
	for (int i = 0; i < ui->tabWidget->count(); i++)
	{
		QScrollArea* scrollArea = qobject_cast<QScrollArea*>(ui->tabWidget->widget(i));
        if (scrollArea == NULL) continue;
		if (scrollArea->widget() == filterTable)
		{
			tabIndex = i;
			break;
		}
	}
	QString tabText = ui->tabWidget->tabText(tabIndex);
	if (!tabText.endsWith('*'))
	{
		tabText += '*';
		ui->tabWidget->setTabText(tabIndex, tabText);
	}
}

bool MainWindow::on_tabWidget_tabCloseRequested(int index)
{
    if (askForClose(index))
    {
        QScrollArea* scrollArea = qobject_cast<QScrollArea*>(ui->tabWidget->widget(index));
        if (scrollArea != NULL)
        {
            FilterTable* filterTable = qobject_cast<FilterTable*>(scrollArea->widget());
            if (filterTable != NULL)
            {
                QString path = filterTable->getConfigPath();
                recentFiles.removeAll(path);
                recentFiles.prepend(path);
                if (recentFiles.size() > 10)
                    recentFiles.removeLast();
                updateRecentFiles();
            }
        }

        ui->tabWidget->removeTab(index);
    }
    return true;
}

void MainWindow::on_actionOpen_triggered()
{
	QScrollArea* scrollArea = qobject_cast<QScrollArea*>(ui->tabWidget->currentWidget());
	QString path;
	if (scrollArea != NULL)
	{
		FilterTable* filterTable = qobject_cast<FilterTable*>(scrollArea->widget());
		if (filterTable->getConfigPath().length() > 0)
		{
			QFileInfo fileInfo(filterTable->getConfigPath());
			path = fileInfo.absolutePath();
		}
	}
	if (path.length() == 0)
		path = configDir.absolutePath();

	QFileDialog dialog(this, tr("Open file"), path, "*.txt");
	dialog.setFileMode(QFileDialog::ExistingFiles);
	dialog.setNameFilter(tr("E-APO configurations (*.txt)"));

	if (dialog.exec() == QDialog::Accepted)
	{
		for (QString file : dialog.selectedFiles())
			load(file);
	}
}

void MainWindow::on_actionSave_triggered()
{
	QScrollArea* scrollArea = qobject_cast<QScrollArea*>(ui->tabWidget->currentWidget());
	if (scrollArea == NULL)
		return;

	FilterTable* filterTable = qobject_cast<FilterTable*>(scrollArea->widget());

	if (filterTable->getConfigPath().length() == 0)
	{
		ui->actionSaveAs->trigger();
	}
	else
	{
		save(filterTable, filterTable->getConfigPath());

		QString tabText = ui->tabWidget->tabText(ui->tabWidget->currentIndex());
		if (tabText.endsWith('*'))
			ui->tabWidget->setTabText(ui->tabWidget->currentIndex(), tabText.left(tabText.length() - 1));
	}
}

void MainWindow::on_actionSaveAs_triggered()
{
	QScrollArea* scrollArea = qobject_cast<QScrollArea*>(ui->tabWidget->currentWidget());
	if (scrollArea == NULL)
		return;

	FilterTable* filterTable = qobject_cast<FilterTable*>(scrollArea->widget());
	QString path;
	QString filename;
	if (filterTable->getConfigPath().length() == 0)
	{
		path = configDir.absolutePath();
	}
	else
	{
		QFileInfo fileInfo(filterTable->getConfigPath());
		path = fileInfo.absolutePath();
		filename = fileInfo.fileName();
	}
	QFileDialog dialog(this, tr("Save file as"), path, "*.txt");
	dialog.setFileMode(QFileDialog::AnyFile);
	dialog.setAcceptMode(QFileDialog::AcceptSave);
	dialog.setNameFilter(tr("E-APO configurations (*.txt)"));
	dialog.setDefaultSuffix(".txt");
	if (filename.length() > 0)
		dialog.selectFile(filename);

	if (dialog.exec() == QDialog::Accepted)
	{
		QString savePath = dialog.selectedFiles().at(0);
		save(filterTable, savePath);
		filterTable->setConfigPath(QDir::toNativeSeparators(savePath));

		QFileInfo fileInfo(savePath);
		ui->tabWidget->setTabText(ui->tabWidget->currentIndex(), fileInfo.fileName());
		ui->tabWidget->setTabToolTip(ui->tabWidget->currentIndex(), QDir::toNativeSeparators(fileInfo.absoluteFilePath()));
	}
}

void MainWindow::on_actionNew_triggered()
{
	FilterTable* filterTable = addTab(tr("Unsaved"), "", "", QList<QString>());

	connect(filterTable, SIGNAL(linesChanged()), this, SLOT(linesChanged()));
	ui->tabWidget->setCurrentIndex(ui->tabWidget->count() - 1);
}

void MainWindow::recentFileSelected()
{
	QAction* action = qobject_cast<QAction*>(sender());
	load(action->text());
}

void MainWindow::on_actionCut_triggered()
{
	QScrollArea* scrollArea = qobject_cast<QScrollArea*>(ui->tabWidget->currentWidget());
	if (scrollArea == NULL)
		return;

	FilterTable* filterTable = qobject_cast<FilterTable*>(scrollArea->widget());
	filterTable->cut();
}

void MainWindow::on_actionCopy_triggered()
{
	QScrollArea* scrollArea = qobject_cast<QScrollArea*>(ui->tabWidget->currentWidget());
	if (scrollArea == NULL)
		return;

	FilterTable* filterTable = qobject_cast<FilterTable*>(scrollArea->widget());
	filterTable->copy();
}

void MainWindow::on_actionPaste_triggered()
{
	QScrollArea* scrollArea = qobject_cast<QScrollArea*>(ui->tabWidget->currentWidget());
	if (scrollArea == NULL)
		return;

	FilterTable* filterTable = qobject_cast<FilterTable*>(scrollArea->widget());
	filterTable->paste();
}

void MainWindow::on_actionDelete_triggered()
{
	QScrollArea* scrollArea = qobject_cast<QScrollArea*>(ui->tabWidget->currentWidget());
	if (scrollArea == NULL)
		return;

	FilterTable* filterTable = qobject_cast<FilterTable*>(scrollArea->widget());
	filterTable->deleteSelectedLines();
}

void MainWindow::on_actionSelectAll_triggered()
{
	QScrollArea* scrollArea = qobject_cast<QScrollArea*>(ui->tabWidget->currentWidget());
	if (scrollArea == NULL)
		return;

	FilterTable* filterTable = qobject_cast<FilterTable*>(scrollArea->widget());
	filterTable->selectAll();
}

void MainWindow::instantModeEnabled(bool enabled)
{
	if (enabled)
	{
		for (int i = 0; i < ui->tabWidget->count(); i++)
		{
			QScrollArea* scrollArea = qobject_cast<QScrollArea*>(ui->tabWidget->widget(i));
            if (scrollArea == NULL) continue;
			FilterTable* filterTable = qobject_cast<FilterTable*>(scrollArea->widget());

			if (filterTable->getConfigPath().length() > 0)
			{
				save(filterTable, filterTable->getConfigPath());

				QString tabText = ui->tabWidget->tabText(i);
				if (tabText.endsWith('*'))
					ui->tabWidget->setTabText(i, tabText.left(tabText.length() - 1));
			}
		}
	}
}

void MainWindow::on_tabWidget_currentChanged(int index)
{
	startAnalysis();
}

void MainWindow::on_startFromComboBox_activated(int index)
{
	startAnalysis();
}

void MainWindow::on_analysisChannelComboBox_activated(int index)
{
	startAnalysis();
}

void MainWindow::on_resolutionSpinBox_valueChanged(int value)
{
	startAnalysis();
}

void MainWindow::updateAnalysisPanel()
{
	analysisThread->beginGetResult();
	int sampleRate = analysisThread->getFreqDataSampleRate();
	int latency = analysisThread->getLatency();
	analysisPlotScene->setFreqData(analysisThread->getFreqData(), analysisThread->getFreqDataLength(), sampleRate);

	double peakGain = analysisThread->getPeakGain();
	ui->peakGainValueLabel->setText(tr("%0 dB").arg(peakGain, 0, 'f', 1));
	ui->peakGainValueLabel->setForegroundRole(peakGain > 0 ? QPalette::Dark : QPalette::WindowText);

	ui->latencyValueLabel->setText(tr("%0 ms (%1 s.)").arg(latency * 1000.0 / sampleRate, 0, 'f', 1).arg(latency));

	ui->initTimeValueLabel->setText(tr("%0 ms").arg(analysisThread->getInitializationTime(), 0, 'f', 1));

	double cpuUsage = analysisThread->getProcessingTime() * 100.0 / (analysisThread->getProcessedFrames() * 1000.0 / sampleRate);
	ui->cpuUsageValueLabel->setText(tr("%0 % (one core)").arg(cpuUsage, 0, 'f', 1));
	ui->cpuUsageValueLabel->setForegroundRole(cpuUsage >= 20 ? (cpuUsage >= 50 ? QPalette::Dark : QPalette::Midlight) : QPalette::WindowText);

	analysisThread->endGetResult();
}

void MainWindow::on_mainToolBar_visibilityChanged(bool visible)
{
	ui->actionToolbar->setChecked(visible);
}

void MainWindow::on_analysisDockWidget_visibilityChanged(bool visible)
{
	ui->actionAnalysisPanel->setChecked(visible);

	if (visible)
		startAnalysis();
}

void MainWindow::on_actionToolbar_triggered(bool checked)
{
	ui->mainToolBar->setVisible(checked);
}

void MainWindow::on_actionAnalysisPanel_triggered(bool checked)
{
	ui->analysisDockWidget->setVisible(checked);
}

void MainWindow::languageSelected(bool selected)
{
	QAction* action = qobject_cast<QAction*>(sender());

	if (!selected)
	{
		action->setChecked(true);
		return;
	}

	QLocale::Language language = (QLocale::Language)action->data().toInt();

	if (QMessageBox::question(this, tr("Restart required"), tr("Configuration Editor will be restarted to apply the changed settings. Proceed?")) == QMessageBox::Yes)
	{
		QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
		if (language == QLocale::AnyLanguage)
		{
			settings.remove("language");
		}
		else
		{
			QString name = QLocale(language).name();
			int index = name.indexOf('_');
			if (index != -1)
				name = name.left(index);
			settings.setValue("language", name);
		}

		restart = true;
		close();
	}
	else
	{
		action->setChecked(false);
	}
}

void MainWindow::on_actionResetAllGlobalPreferences_triggered()
{
	if (QMessageBox::question(this, tr("Restart required"), tr("Configuration Editor will be restarted to apply the changed settings. Proceed?")) == QMessageBox::Yes)
	{
		QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
		for (const QString& key : settings.childGroups())
		{
			if (key != "file-specific")
				settings.remove(key);
		}
		for (const QString& key : settings.childKeys())
			settings.remove(key);

		restart = true;
		noSavePreferences = true;
		close();
	}
}

void MainWindow::on_actionResetAllFileSpecificPreferences_triggered()
{
	if (QMessageBox::question(this, tr("Restart required"), tr("Configuration Editor will be restarted to apply the changed settings. Proceed?")) == QMessageBox::Yes)
	{
		QSettings settings(QString::fromWCharArray(EDITOR_PER_FILE_REGPATH), QSettings::NativeFormat);
		for (const QString& key : settings.childGroups())
			settings.remove(key);
		for (const QString& key : settings.childKeys())
			settings.remove(key);

		restart = true;
		noSaveFilePreferences = true;
		close();
	}
}

FilterTable* MainWindow::addTab(QString title, QString tooltip, QString configPath, QList<QString> lines)
{
	QScrollArea* scrollArea = new QScrollArea(ui->tabWidget);
	scrollArea->setWidgetResizable(true);
	FilterTable* filterTable = new FilterTable(this);
	scrollArea->setWidget(filterTable);
	filterTable->setAcceptDrops(true);
	filterTable->setFocusPolicy(Qt::WheelFocus);

	shared_ptr<AbstractAPOInfo> selectedDevice;
	int channelMask;
	getDeviceAndChannelMask(&selectedDevice, &channelMask);
	filterTable->updateDeviceAndChannelMask(selectedDevice, channelMask);
	filterTable->initialize(scrollArea, outputDevices, inputDevices);
	filterTable->setLines(configPath, lines);

	int tabIndex = ui->tabWidget->addTab(scrollArea, title);
	ui->tabWidget->setTabToolTip(tabIndex, tooltip);

	return filterTable;
}

void MainWindow::getDeviceAndChannelMask(shared_ptr<AbstractAPOInfo>* selectedDevice, int* channelMask)
{
	*selectedDevice = deviceComboBox->currentData().value<shared_ptr<AbstractAPOInfo>>();
	if (*selectedDevice == NULL)
		*selectedDevice = defaultOutputDevice;

	*channelMask = channelConfigurationComboBox->currentData().toInt();
	if (*channelMask == 0 && selectedDevice->get() != NULL)
	{
		*channelMask = (*selectedDevice)->getChannelMask();

		if (*channelMask == 0)
			*channelMask = ChannelHelper::getDefaultChannelMask((*selectedDevice)->getChannelCount());
	}
}

bool MainWindow::askForClose(int tabIndex)
{
	bool discarded = false;
	if (ui->tabWidget->tabText(tabIndex).endsWith('*'))
	{
		ui->tabWidget->setCurrentIndex(tabIndex);
		QString configPath = ui->tabWidget->tabToolTip(tabIndex);
		QMessageBox messageBox;
		messageBox.setWindowTitle(tr("Unsaved changes"));
		messageBox.setText(tr("The configuration file %0 has unsaved changes.").arg(configPath));
		messageBox.setInformativeText(tr("Do you want to save the changes before closing the file?"));
		messageBox.setIcon(QMessageBox::Question);
		messageBox.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
		messageBox.setDefaultButton(QMessageBox::Save);
		messageBox.setEscapeButton(QMessageBox::Cancel);
		messageBox.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
		int result = messageBox.exec();

		switch (result)
		{
		case QMessageBox::Save:
			ui->actionSave->trigger();
			if (ui->tabWidget->tabText(tabIndex).endsWith('*'))
				// saving was canceled
				return false;
			break;
		case QMessageBox::Discard:
			discarded = true;
			break;
		case QMessageBox::Cancel:
			return false;
		}
	}

	if (!discarded && !noSaveFilePreferences)
	{
		QScrollArea* scrollArea = qobject_cast<QScrollArea*>(ui->tabWidget->widget(tabIndex));
		if (scrollArea != NULL)
		{
            FilterTable* filterTable = qobject_cast<FilterTable*>(scrollArea->widget());
            if (filterTable != NULL)
                filterTable->savePreferences();
		}
	}

	return true;
}

void MainWindow::startAnalysis()
{
	if (!ui->analysisDockWidget->isVisible())
		return;

	shared_ptr<AbstractAPOInfo> selectedDevice;

	int channelMask;
	getDeviceAndChannelMask(&selectedDevice, &channelMask);

	if (selectedDevice != NULL)
	{
		QString configPath;

		if (ui->startFromComboBox->currentIndex() == 1)
		{
			QScrollArea* scrollArea = qobject_cast<QScrollArea*>(ui->tabWidget->currentWidget());
			if (scrollArea != NULL)
			{
				FilterTable* filterTable = qobject_cast<FilterTable*>(scrollArea->widget());

				if (filterTable->getConfigPath().length() > 0)
					configPath = filterTable->getConfigPath();
			}
		}

		if (configPath.isEmpty())
			configPath = configDir.absoluteFilePath("config.txt");
		configPath = QDir::toNativeSeparators(configPath);

		analysisThread->setParameters(selectedDevice, channelMask, ui->analysisChannelComboBox->currentIndex(), configPath, ui->resolutionSpinBox->value());
	}
}

void MainWindow::loadPreferences()
{
	QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
	QVariant geometryValue = settings.value("geometry");
	if (geometryValue.isValid())
		restoreGeometry(geometryValue.toByteArray());
	instantModeCheckBox->setChecked(settings.value("instantMode", true).toBool());
	QString selectedDevice = settings.value("selectedDevice").toString();
	if (!selectedDevice.isEmpty())
	{
		for (int i = 0; i < deviceComboBox->count(); i++)
		{
			shared_ptr<AbstractAPOInfo> apoInfo = deviceComboBox->itemData(i).value<shared_ptr<AbstractAPOInfo>>();
			if (apoInfo != NULL)
			{
				if (QString::fromStdWString(apoInfo->getDeviceString()).compare(selectedDevice, Qt::CaseInsensitive) == 0)
				{
					deviceComboBox->setCurrentIndex(i);
					break;
				}
			}
		}
	}
	deviceSelected(deviceComboBox->currentIndex());

	int selectedChannelMask = settings.value("selectedChannelMask").toInt();
	if (selectedChannelMask != 0)
	{
		int index = channelConfigurationComboBox->findData(selectedChannelMask);
		if (index != -1)
			channelConfigurationComboBox->setCurrentIndex(index);
	}
	channelConfigurationSelected(channelConfigurationComboBox->currentIndex());

	ui->startFromComboBox->setCurrentIndex(settings.value("analysis/startFrom").toInt());
	ui->analysisChannelComboBox->setCurrentText(settings.value("analysis/channel").toString());
	ui->resolutionSpinBox->setValue(settings.value("analysis/resolution", 65536).toInt());
	double zoomX = GUIHelper::scaleZoom(settings.value("analysis/zoomX", 1.0).toDouble());
	double zoomY = GUIHelper::scaleZoom(settings.value("analysis/zoomY", 1.0).toDouble());
	analysisPlotScene->setZoom(zoomX, zoomY);
	bool ok;
	int scrollX = GUIHelper::scale(settings.value("analysis/scrollX").toDouble(&ok));
	if (!ok)
		scrollX = round(analysisPlotScene->hzToX(20));
	int scrollY = GUIHelper::scale(settings.value("analysis/scrollY").toDouble(&ok));
	if (!ok)
		scrollY = round(analysisPlotScene->dbToY(22));

	ui->graphicsView->setScrollOffsets(scrollX, scrollY);

	QVariant openFilesValue = settings.value("openFiles");
	int tabIndex = settings.value("tabIndex").toInt();
	if (openFilesValue.isValid())
	{
		QStringList fileList = openFilesValue.toStringList();
		for (int i = 0; i < fileList.size(); i++)
		{
			load(fileList[i]);
			if (i == tabIndex)
				tabIndex = ui->tabWidget->currentIndex();
		}
	}
	ui->tabWidget->setCurrentIndex(tabIndex);
	recentFiles = settings.value("recentFiles").toStringList();
	updateRecentFiles();

	QVariant languageValue = settings.value("language");
	QLocale::Language language;
	if (languageValue.isValid())
		language = QLocale(languageValue.toString()).language();
	else
		language = QLocale::AnyLanguage;

	for (QAction* action : ui->menuLanguage->actions())
		action->setChecked(action->data().toInt() == language);

	// load window state after initializing channels as it may trigger on_analysisDockWidget_visibilityChanged when analysis panel is detached
	QVariant stateValue = settings.value("windowState");
	if (stateValue.isValid())
		restoreState(stateValue.toByteArray());
}

void MainWindow::savePreferences()
{
	if (noSavePreferences)
		return;

	QSettings settings(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
	settings.setValue("geometry", saveGeometry());
	settings.setValue("windowState", saveState());
	settings.setValue("instantMode", instantModeCheckBox->isChecked());
	shared_ptr<AbstractAPOInfo> selectedDevice = deviceComboBox->currentData().value<shared_ptr<AbstractAPOInfo>>();
	settings.setValue("selectedDevice", selectedDevice != NULL ? QString::fromStdWString(selectedDevice->getDeviceString()) : "");
	int channelMask = channelConfigurationComboBox->currentData().toInt();
	settings.setValue("selectedChannelMask", channelMask);

	settings.setValue("analysis/startFrom", ui->startFromComboBox->currentIndex());
	settings.setValue("analysis/channel", ui->analysisChannelComboBox->currentText());
	settings.setValue("analysis/resolution", ui->resolutionSpinBox->value());
	settings.setValue("analysis/zoomX", GUIHelper::invScaleZoom(analysisPlotScene->getZoomX()));
	settings.setValue("analysis/zoomY", GUIHelper::invScaleZoom(analysisPlotScene->getZoomY()));
	QScrollBar* hScrollBar = ui->graphicsView->horizontalScrollBar();
	double value = GUIHelper::invScale(hScrollBar->value());
	settings.setValue("analysis/scrollX", value);
	QScrollBar* vScrollBar = ui->graphicsView->verticalScrollBar();
	value = GUIHelper::invScale(vScrollBar->value());
	settings.setValue("analysis/scrollY", value);

	QStringList fileList;
	for (int i = 0; i < ui->tabWidget->count(); i++)
	{
		QScrollArea* scrollArea = qobject_cast<QScrollArea*>(ui->tabWidget->widget(i));
		if (scrollArea == NULL)
			continue;
		FilterTable* filterTable = qobject_cast<FilterTable*>(scrollArea->widget());
		if (filterTable->getConfigPath().length() > 0)
		{
			fileList.append(filterTable->getConfigPath());
		}
	}
	settings.setValue("openFiles", fileList);
	settings.setValue("tabIndex", ui->tabWidget->currentIndex());
	settings.setValue("recentFiles", recentFiles);

	settings.sync();
}

void MainWindow::updateRecentFiles()
{
	QList<QAction*> actions = ui->menuFile->actions();
	int separatorsFound = 0;
	for (int i = actions.size() - 1; i >= 0; i--)
	{
		QAction* action = actions[i];
		if (action->isSeparator())
		{
			separatorsFound++;

			if (separatorsFound == 1)
			{
				QList<QAction*> newActions;
				for (const QString& recentFile : recentFiles)
				{
					QAction* newAction = new QAction(recentFile, ui->menuFile);
					connect(newAction, SIGNAL(triggered(bool)), this, SLOT(recentFileSelected()));
					newActions.append(newAction);
				}
				ui->menuFile->insertActions(action, newActions);
			}
			else
			{
				break;
			}
		}
		else if (separatorsFound >= 1)
		{
			ui->menuFile->removeAction(action);
		}
	}
}

template<class T> QList<T> MainWindow::toQList(const std::vector<T>& vector)
{
	QList<T> list;
	list.reserve((int)vector.size());
	for (T t : vector)
		list.append(t);

	return list;
}

void MainWindow::on_actionPrefrences_triggered()
{
    for (int i = 0; i < ui->tabWidget->count(); i++)
    {
        if (ui->tabWidget->widget(i)->property("isPreferences").toBool())
        {
            ui->tabWidget->setCurrentIndex(i);
            return;
        }
    }

    QWidget* page = createPreferencesPage(this, [this]() { applyTheme(); });
    page->setProperty("isPreferences", true);

    int index = ui->tabWidget->addTab(page, tr("Preferences"));
    ui->tabWidget->setCurrentIndex(index);
}

void MainWindow::applyTheme()
{
    QSettings s(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);
    QColor bg(s.value("theme/bgColor", "#2d2d2d").toString());
    QColor text(s.value("theme/textColor", "#ffffff").toString());
    QColor accent(s.value("theme/accentColor", "#2a82da").toString());
    int panelAlpha = s.value("theme/panelOpacity", 100).toInt() * 255 / 100;
    bool enabled = s.value("theme/enabled", false).toBool() && !s.value("theme/imagePath").toString().isEmpty();
    int uiAlphaValue = enabled ? s.value("theme/uiAlpha", 100).toInt() * 255 / 100 : 255;
    qApp->setProperty("uiAlpha", uiAlphaValue);
    for (QWidget* w : findChildren<QWidget*>())
        w->update();

    QPalette p = qApp->palette();
    p.setColor(QPalette::Window, bg);
    QColor baseColor = bg.darker(130);
    baseColor.setAlpha(uiAlphaValue);
    QColor alternateColor = bg;
    alternateColor.setAlpha(uiAlphaValue);
    QColor buttonColor = bg.lighter(120);
    buttonColor.setAlpha(uiAlphaValue);
    p.setColor(QPalette::Base, baseColor);
    p.setColor(QPalette::AlternateBase, alternateColor);
    p.setColor(QPalette::Button, buttonColor);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::Link, accent);
    qApp->setPalette(p);

    if (enabled)
    {
        QString panel = QString("rgba(%1,%2,%3,%4)").arg(bg.red()).arg(bg.green()).arg(bg.blue()).arg(panelAlpha);
        setStyleSheet(QString("QTabWidget::pane { background: %1; border: none; } QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; } QDockWidget > QWidget { background: %1; }").arg(panel));
    }
    else
    {
        setStyleSheet(QString());
    }

    updateBackground();
}

void MainWindow::updateBackground()
{
    QSettings s(QString::fromWCharArray(EDITOR_REGPATH), QSettings::NativeFormat);

    if (backgroundLabel == nullptr)
    {
        backgroundLabel = new QLabel(this);
        backgroundLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    QPixmap source(s.value("theme/imagePath").toString());
    if (!s.value("theme/enabled", false).toBool() || source.isNull())
    {
        backgroundLabel->hide();
        return;
    }

    QColor bg(s.value("theme/bgColor", "#2d2d2d").toString());
    QSize size = this->size();

    QPixmap layer(size);
    layer.fill(Qt::transparent);
    {
        QPainter layerPainter(&layer);
        layerPainter.setRenderHint(QPainter::SmoothPixmapTransform);
        int mode = s.value("theme/scaleMode", 0).toInt();
        if (mode == 4)
        {
            layerPainter.drawTiledPixmap(layer.rect(), source);
        }
        else
        {
            QPixmap scaled = source;
            if (mode == 0)
                scaled = source.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            else if (mode == 1)
                scaled = source.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            else if (mode == 2)
                scaled = source.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            layerPainter.drawPixmap((size.width() - scaled.width()) / 2, (size.height() - scaled.height()) / 2, scaled);
        }
    }

    int blur = s.value("theme/blur", 0).toInt();
    if (blur > 0)
        layer = blurPixmap(layer, blur);

    int darken = s.value("theme/darken", 0).toInt();
    if (darken > 0)
    {
        QPainter darkenPainter(&layer);
        darkenPainter.setCompositionMode(QPainter::CompositionMode_SourceAtop);
        darkenPainter.fillRect(layer.rect(), QColor(0, 0, 0, darken * 255 / 100));
    }

    QPixmap canvas(size);
    canvas.fill(bg);
    {
        QPainter canvasPainter(&canvas);
        canvasPainter.setOpacity(s.value("theme/imageOpacity", 40).toInt() / 100.0);
        canvasPainter.drawPixmap(0, 0, layer);
    }

    backgroundLabel->setPixmap(canvas);
    backgroundLabel->setGeometry(rect());
    backgroundLabel->lower();
    backgroundLabel->show();
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    if (backgroundLabel != nullptr && !backgroundLabel->isHidden())
        updateBackground();
}
void MainWindow::on_actionLoudnessMatch_triggered()
{
    for (int i = 0; i < ui->tabWidget->count(); i++)
    {
        if (ui->tabWidget->widget(i)->property("isTuner").toBool())
        {
            ui->tabWidget->setCurrentIndex(i);
            return;
        }
    }

    FrequencyTuner* tuner = new FrequencyTuner(ui->tabWidget);
    tuner->setProperty("isTuner", true);
    connect(tuner, &FrequencyTuner::resultReady, this, [this](const QString& line) {
        FilterTable* filterTable = addTab(tr("Tuned"), "", "", QList<QString>{line});
        connect(filterTable, SIGNAL(linesChanged()), this, SLOT(linesChanged()));
        ui->tabWidget->setCurrentIndex(ui->tabWidget->count() - 1);
    });

    int index = ui->tabWidget->addTab(tuner, tr("Loudness Matching"));
    ui->tabWidget->setCurrentIndex(index);
}

