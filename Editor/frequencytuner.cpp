#include "FrequencyTuner.h"

#include <QStackedWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QTableWidget>
#include <QHeaderView>
#include <QPlainTextEdit>
#include <QClipboard>
#include <QGuiApplication>
#include <QMessageBox>
#include <QRandomGenerator>
#include <QSignalBlocker>
#include <QDataStream>
#include <QtMath>
#include <utility>

#include <windows.h>
#include <mmsystem.h>

static const int kSampleRate = 44100;

static void appendTone(QVector<qint16>& samples, double freq, double db, int ms)
{
    const double pi = 3.14159265358979323846;
    int count = kSampleRate * ms / 1000;
    int fade = kSampleRate * 20 / 1000;
    double amplitude = qPow(10.0, db / 20.0) * 32767.0;
    for (int i = 0; i < count; i++)
    {
        double envelope = 1.0;
        if (i < fade)
            envelope = 0.5 - 0.5 * qCos(pi * i / fade);
        else if (i >= count - fade)
            envelope = 0.5 - 0.5 * qCos(pi * (count - 1 - i) / fade);
        samples.append(qint16(amplitude * envelope * qSin(2.0 * pi * freq * i / kSampleRate)));
    }
}

static void appendSilence(QVector<qint16>& samples, int ms)
{
    int count = kSampleRate * ms / 1000;
    for (int i = 0; i < count; i++)
        samples.append(0);
}

static QByteArray buildWav(const QVector<qint16>& samples)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    quint32 dataSize = quint32(samples.size() * 2);
    stream.writeRawData("RIFF", 4);
    stream << quint32(36 + dataSize);
    stream.writeRawData("WAVE", 4);
    stream.writeRawData("fmt ", 4);
    stream << quint32(16) << quint16(1) << quint16(1) << quint32(kSampleRate) << quint32(kSampleRate * 2) << quint16(2) << quint16(16);
    stream.writeRawData("data", 4);
    stream << dataSize;
    stream.writeRawData(reinterpret_cast<const char*>(samples.constData()), int(dataSize));
    return data;
}

FrequencyTuner::FrequencyTuner(QWidget* parent)
    : QWidget(parent)
{
    debounce = new QTimer(this);
    debounce->setSingleShot(true);
    debounce->setInterval(150);
    connect(debounce, &QTimer::timeout, this, [this]() {
        if (running)
            updatePlayback();
    });

    pages = new QStackedWidget;
    pages->addWidget(createSetupPage());
    pages->addWidget(createSessionPage());
    pages->addWidget(createResultPage());

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(pages);
}

FrequencyTuner::~FrequencyTuner()
{
    stopPlayback();
}

void FrequencyTuner::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (running && !steps.isEmpty())
        updatePlayback();
}

void FrequencyTuner::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    debounce->stop();
    stopPlayback();
}

QWidget* FrequencyTuner::createSetupPage()
{
    QWidget* page = new QWidget;
    QVBoxLayout* outer = new QVBoxLayout(page);

    QLabel* intro = new QLabel(tr("Each step loops a reference tone followed by a test tone. Move the slider until the test tone sounds exactly as loud as the reference. Set your system volume low before starting, and measure with Equalizer APO off or flat."));
    intro->setWordWrap(true);
    outer->addWidget(intro);

    QFormLayout* form = new QFormLayout;

    startFreqSpin = new QDoubleSpinBox;
    startFreqSpin->setRange(10, 20000);
    startFreqSpin->setDecimals(0);
    startFreqSpin->setSuffix(" Hz");
    startFreqSpin->setValue(20);
    form->addRow(tr("Start frequency:"), startFreqSpin);

    endFreqSpin = new QDoubleSpinBox;
    endFreqSpin->setRange(20, 22000);
    endFreqSpin->setDecimals(0);
    endFreqSpin->setSuffix(" Hz");
    endFreqSpin->setValue(16000);
    form->addRow(tr("End frequency:"), endFreqSpin);

    bandsSpin = new QSpinBox;
    bandsSpin->setRange(3, 60);
    bandsSpin->setValue(10);
    form->addRow(tr("Number of bands (log spaced):"), bandsSpin);

    iterationsSpin = new QSpinBox;
    iterationsSpin->setRange(1, 10);
    iterationsSpin->setValue(2);
    form->addRow(tr("Iterations:"), iterationsSpin);

    refFreqSpin = new QDoubleSpinBox;
    refFreqSpin->setRange(100, 8000);
    refFreqSpin->setDecimals(0);
    refFreqSpin->setSuffix(" Hz");
    refFreqSpin->setValue(1000);
    form->addRow(tr("Reference frequency:"), refFreqSpin);

    refLevelSpin = new QDoubleSpinBox;
    refLevelSpin->setRange(-70, -12);
    refLevelSpin->setDecimals(0);
    refLevelSpin->setSuffix(" dBFS");
    refLevelSpin->setValue(-36);
    form->addRow(tr("Reference level:"), refLevelSpin);

    maxLevelSpin = new QDoubleSpinBox;
    maxLevelSpin->setRange(-40, -6);
    maxLevelSpin->setDecimals(0);
    maxLevelSpin->setSuffix(" dBFS");
    maxLevelSpin->setValue(-12);
    form->addRow(tr("Maximum tone level:"), maxLevelSpin);

    randomOrderCheck = new QCheckBox(tr("Randomize band order"));
    randomOrderCheck->setChecked(true);
    form->addRow(randomOrderCheck);

    randomStartCheck = new QCheckBox(tr("Randomize starting slider position"));
    randomStartCheck->setChecked(true);
    form->addRow(randomStartCheck);

    showFreqCheck = new QCheckBox(tr("Show test frequency during the test"));
    form->addRow(showFreqCheck);

    onlyCutCheck = new QCheckBox(tr("Only cut (shift result so the maximum is 0 dB)"));
    onlyCutCheck->setChecked(true);
    form->addRow(onlyCutCheck);

    outer->addLayout(form);

    QPushButton* startButton = new QPushButton(tr("Start"));
    connect(startButton, &QPushButton::clicked, this, [this]() { startSession(); });
    outer->addWidget(startButton);
    outer->addStretch();

    return page;
}

QWidget* FrequencyTuner::createSessionPage()
{
    QWidget* page = new QWidget;
    QVBoxLayout* layout = new QVBoxLayout(page);

    stepLabel = new QLabel;
    QFont boldFont = stepLabel->font();
    boldFont.setBold(true);
    stepLabel->setFont(boldFont);
    layout->addWidget(stepLabel);

    progress = new QProgressBar;
    progress->setTextVisible(false);
    layout->addWidget(progress);

    freqLabel = new QLabel;
    layout->addWidget(freqLabel);

    QLabel* hint = new QLabel(tr("You hear the reference tone, then the test tone, repeating. Move the slider until both sound equally loud."));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    levelSlider = new QSlider(Qt::Horizontal);
    layout->addWidget(levelSlider);

    levelLabel = new QLabel;
    levelLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(levelLabel);

    connect(levelSlider, &QSlider::valueChanged, this, [this]() {
        updateLevelLabel();
        debounce->start();
    });

    QHBoxLayout* buttons = new QHBoxLayout;

    QPushButton* confirmButton = new QPushButton(tr("Confirm and next"));
    connect(confirmButton, &QPushButton::clicked, this, [this]() { advance(true); });
    buttons->addWidget(confirmButton);

    QPushButton* skipButton = new QPushButton(tr("Can't hear it (skip)"));
    connect(skipButton, &QPushButton::clicked, this, [this]() { advance(false); });
    buttons->addWidget(skipButton);

    QPushButton* stopButton = new QPushButton(tr("Stop"));
    connect(stopButton, &QPushButton::clicked, this, [this]() {
        debounce->stop();
        stopPlayback();
        running = false;
        pages->setCurrentIndex(0);
    });
    buttons->addWidget(stopButton);

    layout->addLayout(buttons);
    layout->addStretch();

    return page;
}

QWidget* FrequencyTuner::createResultPage()
{
    QWidget* page = new QWidget;
    QVBoxLayout* layout = new QVBoxLayout(page);

    resultTable = new QTableWidget(0, 4);
    resultTable->setHorizontalHeaderLabels({tr("Frequency (Hz)"), tr("Correction (dB)"), tr("Spread (dB)"), tr("Samples")});
    resultTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    resultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(resultTable);

    QLabel* note = new QLabel(tr("A spread above about 3 dB means that band was hard to judge. Use more iterations for a better result."));
    note->setWordWrap(true);
    layout->addWidget(note);

    resultEdit = new QPlainTextEdit;
    resultEdit->setMaximumHeight(70);
    layout->addWidget(resultEdit);

    QHBoxLayout* buttons = new QHBoxLayout;

    QPushButton* copyButton = new QPushButton(tr("Copy line"));
    connect(copyButton, &QPushButton::clicked, this, [this]() {
        QGuiApplication::clipboard()->setText(resultEdit->toPlainText());
    });
    buttons->addWidget(copyButton);

    QPushButton* openButton = new QPushButton(tr("Open as new EQ tab"));
    connect(openButton, &QPushButton::clicked, this, [this]() {
        emit resultReady(resultEdit->toPlainText());
    });
    buttons->addWidget(openButton);

    QPushButton* againButton = new QPushButton(tr("New session"));
    connect(againButton, &QPushButton::clicked, this, [this]() { pages->setCurrentIndex(0); });
    buttons->addWidget(againButton);

    layout->addLayout(buttons);

    return page;
}

double FrequencyTuner::sliderToDb(int value) const
{
    return value / 2.0;
}

void FrequencyTuner::updateLevelLabel()
{
    double db = sliderToDb(levelSlider->value());
    levelLabel->setText(QString::asprintf("%+.1f dB vs reference  (%.1f dBFS)", db - refLevel, db));
}

void FrequencyTuner::startSession()
{
    double startFreq = startFreqSpin->value();
    double endFreq = endFreqSpin->value();
    if (endFreq <= startFreq)
    {
        QMessageBox::warning(this, tr("Loudness Matching"), tr("The end frequency must be higher than the start frequency."));
        return;
    }

    refLevel = refLevelSpin->value();
    maxLevel = maxLevelSpin->value();
    if (maxLevel < refLevel + 6.0)
    {
        QMessageBox::warning(this, tr("Loudness Matching"), tr("The maximum tone level must be at least 6 dB above the reference level."));
        return;
    }

    minLevel = qMax(-80.0, refLevel - 30.0);
    refFreq = refFreqSpin->value();
    iterationCount = iterationsSpin->value();
    randomStart = randomStartCheck->isChecked();
    showFrequency = showFreqCheck->isChecked();
    onlyCut = onlyCutCheck->isChecked();

    int bandCount = bandsSpin->value();
    bandFreqs.clear();
    for (int i = 0; i < bandCount; i++)
        bandFreqs.append(startFreq * qPow(endFreq / startFreq, i / double(bandCount - 1)));

    steps.clear();
    for (int iteration = 0; iteration < iterationCount; iteration++)
    {
        QVector<int> order;
        for (int b = 0; b < bandCount; b++)
            order.append(b);
        if (randomOrderCheck->isChecked())
        {
            for (int i = int(order.size()) - 1; i > 0; i--)
                std::swap(order[i], order[int(QRandomGenerator::global()->bounded(i + 1))]);
        }
        for (int b : order)
            steps.append(Step{b, iteration});
    }

    measurements.clear();
    measurements.resize(bandCount);

    progress->setRange(0, int(steps.size()));
    stepIndex = 0;
    running = true;
    pages->setCurrentIndex(1);
    beginStep();
}

void FrequencyTuner::beginStep()
{
    Step step = steps[stepIndex];
    double freq = bandFreqs[step.band];

    progress->setValue(stepIndex);
    stepLabel->setText(tr("Step %1 of %2  (round %3 of %4)").arg(stepIndex + 1).arg(int(steps.size())).arg(step.iteration + 1).arg(iterationCount));
    freqLabel->setText(showFrequency ? tr("Test tone: %1 Hz").arg(freq, 0, 'f', 1) : tr("Test tone: hidden"));

    double startDb = refLevel;
    if (randomStart)
        startDb = refLevel - 10.0 + QRandomGenerator::global()->generateDouble() * 20.0;
    startDb = qBound(minLevel, startDb, maxLevel);

    {
        QSignalBlocker blocker(levelSlider);
        levelSlider->setRange(qRound(minLevel * 2), qRound(maxLevel * 2));
        levelSlider->setValue(qRound(startDb * 2));
    }

    updateLevelLabel();
    updatePlayback();
}

void FrequencyTuner::advance(bool record)
{
    debounce->stop();
    stopPlayback();

    if (record)
        measurements[steps[stepIndex].band].append(sliderToDb(levelSlider->value()) - refLevel);

    stepIndex++;
    if (stepIndex >= steps.size())
        finishSession();
    else
        beginStep();
}

void FrequencyTuner::finishSession()
{
    stopPlayback();
    running = false;

    int bandCount = int(bandFreqs.size());
    QVector<double> means(bandCount, 0.0);
    QVector<double> spreads(bandCount, 0.0);
    double peak = -1000.0;
    bool anyData = false;

    for (int b = 0; b < bandCount; b++)
    {
        const QVector<double>& values = measurements[b];
        if (values.isEmpty())
            continue;

        double sum = 0.0;
        for (double v : values)
            sum += v;
        means[b] = sum / values.size();

        double squares = 0.0;
        for (double v : values)
            squares += (v - means[b]) * (v - means[b]);
        spreads[b] = values.size() > 1 ? qSqrt(squares / (values.size() - 1)) : 0.0;

        peak = qMax(peak, means[b]);
        anyData = true;
    }

    if (!anyData)
    {
        QMessageBox::information(this, tr("Loudness Matching"), tr("No measurements were recorded."));
        pages->setCurrentIndex(0);
        return;
    }

    double shift = onlyCut ? peak : 0.0;
    resultTable->setRowCount(bandCount);
    QStringList parts;

    for (int b = 0; b < bandCount; b++)
    {
        const QVector<double>& values = measurements[b];
        bool hasData = !values.isEmpty();
        double gain = means[b] - shift;
        QString freqText = QString::number(bandFreqs[b], 'f', 1);

        auto setCell = [this, b](int column, const QString& text) {
            QTableWidgetItem* item = new QTableWidgetItem(text);
            item->setTextAlignment(Qt::AlignCenter);
            resultTable->setItem(b, column, item);
        };

        setCell(0, freqText);
        setCell(1, hasData ? QString::number(gain, 'f', 1) : tr("no data"));
        setCell(2, values.size() > 1 ? QString::number(spreads[b], 'f', 1) : QString("-"));
        setCell(3, QString::number(values.size()));

        if (hasData)
            parts.append(freqText + " " + QString::number(gain, 'f', 1));
    }

    resultEdit->setPlainText("GraphicEQ: " + parts.join("; "));
    pages->setCurrentIndex(2);
}

void FrequencyTuner::stopPlayback()
{
    PlaySoundW(nullptr, nullptr, 0);
    wavData.clear();
}

void FrequencyTuner::updatePlayback()
{
    if (!running || stepIndex >= steps.size())
        return;

    PlaySoundW(nullptr, nullptr, 0);

    QVector<qint16> samples;
    appendTone(samples, refFreq, refLevel, 500);
    appendSilence(samples, 300);
    appendTone(samples, bandFreqs[steps[stepIndex].band], sliderToDb(levelSlider->value()), 500);
    appendSilence(samples, 700);

    wavData = buildWav(samples);
    PlaySoundW(reinterpret_cast<LPCWSTR>(wavData.constData()), nullptr, SND_MEMORY | SND_ASYNC | SND_LOOP | SND_NODEFAULT);
}