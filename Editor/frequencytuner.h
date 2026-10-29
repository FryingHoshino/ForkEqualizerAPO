#pragma once

#include <QWidget>
#include <QVector>
#include <QByteArray>
#include <QTimer>

class QStackedWidget;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class QSlider;
class QLabel;
class QProgressBar;
class QTableWidget;
class QPlainTextEdit;

class FrequencyTuner : public QWidget
{
    Q_OBJECT

public:
    explicit FrequencyTuner(QWidget* parent = nullptr);
    ~FrequencyTuner() override;

signals:
    void resultReady(const QString& graphicEqLine);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    struct Step
    {
        int band;
        int iteration;
    };

    QWidget* createSetupPage();
    QWidget* createSessionPage();
    QWidget* createResultPage();

    void startSession();
    void beginStep();
    void advance(bool record);
    void finishSession();
    void updatePlayback();
    void stopPlayback();
    void updateLevelLabel();
    double sliderToDb(int value) const;

    QStackedWidget* pages = nullptr;
    QTimer* debounce = nullptr;

    QDoubleSpinBox* startFreqSpin = nullptr;
    QDoubleSpinBox* endFreqSpin = nullptr;
    QSpinBox* bandsSpin = nullptr;
    QSpinBox* iterationsSpin = nullptr;
    QDoubleSpinBox* refFreqSpin = nullptr;
    QDoubleSpinBox* refLevelSpin = nullptr;
    QDoubleSpinBox* maxLevelSpin = nullptr;
    QCheckBox* randomOrderCheck = nullptr;
    QCheckBox* randomStartCheck = nullptr;
    QCheckBox* showFreqCheck = nullptr;
    QCheckBox* onlyCutCheck = nullptr;

    QLabel* stepLabel = nullptr;
    QLabel* freqLabel = nullptr;
    QLabel* levelLabel = nullptr;
    QSlider* levelSlider = nullptr;
    QProgressBar* progress = nullptr;

    QTableWidget* resultTable = nullptr;
    QPlainTextEdit* resultEdit = nullptr;

    QVector<double> bandFreqs;
    QVector<Step> steps;
    QVector<QVector<double>> measurements;
    QByteArray wavData;

    int stepIndex = 0;
    int iterationCount = 1;
    bool running = false;
    bool randomStart = true;
    bool showFrequency = false;
    bool onlyCut = true;
    double refFreq = 1000.0;
    double refLevel = -36.0;
    double minLevel = -66.0;
    double maxLevel = -12.0;
};