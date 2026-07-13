#ifndef SIMULATIONMANAGER_H
#define SIMULATIONMANAGER_H

#include <QObject>
#include <cstddef>
#include <functional>
#include <string>
#include <simon/simon.hpp>
#include "ui/widgets/waveformwindow.h"
#include "ui/widgets/typewindow.h"
// using namespace fcngraph;
using namespace simon;
class SimulationManager : public QObject {
    Q_OBJECT
public:
    struct EnergyAnalysisRunInfo {
        enum class Mode {
            Accurate,
            Balanced,
            Fast
        };

        std::size_t cellCount = 0;
        std::size_t sampleCount = 0;
        double timeStep = 0.0;
        double duration = 0.0;
        Mode mode = Mode::Accurate;
    };

    explicit SimulationManager(QObject *parent = nullptr);

    void bistableSim(const std::string &fname, Result &result);
    AcceleratedBistableStatistics acceleratedBistableSim(const std::string &fname, Result &result);
    void bistableSimWithSelective(const std::string &fname, const std::string &vfname, Result &result);
    void coherenceSim(const std::string &fname, Result &result);
    AcceleratedCoherenceStatistics acceleratedCoherenceSim(const std::string &fname, Result &result);
    void coherenceSimWithSelective(const std::string &fname, const std::string &vfname, Result &result);
    EnergyAnalysisRunInfo energyAnalysis(const std::string &fname, Result &result);
    void runEnergyAnalysisForFile(const QString &fileName, const QString &sourceFileName = QString());
    bool setSimulationInputFile(const QString &inputFile,
                                const QString &resultBasePath,
                                const QString &temporaryInputFile = QString());

signals:
    void simulationFinished(const QString &outputFileName);
    void simulationFailed(const QString &message);
    void energyAnalysisFinished(const QString &message,
                                const QString &waveformFileName,
                                const QString &reportFileName,
                                const QString &distributionImageName);
    void energyAnalysisFailed(const QString &message);
    void operationStarted(const QString &title, const QString &detail);
    void operationProgress(const QString &detail, int value, int maximum);
    void operationFinished(const QString &message);
    void operationFailed(const QString &message);

public slots:
    void slotBistableSim();
    void slotAcceleratedBistableSim();
    void slotBistableSimWithSelective();
    void slotCoherenceSim();
    void slotAcceleratedCoherenceSim();
    void slotCoherenceSimWithSelective();
    void slotEnergyAnalysis();
    void slotSavedname(QString fileName);
    void slotSavedinputname(QVector<QString> inputname);
    void slotSendvtnames(const QString &fileName);
    void slotSimWithSelective();
public:
    QString curfileName;
private:
    void startWorkerOperation(const QString &title,
                              const QString &detail,
                              std::function<void()> task);
    QString formatEnergyAnalysisStatus(const QString &sourceFileName,
                                       const QString &reportFileName,
                                       const QString &distributionImageName,
                                       const Result &result,
                                       const EnergyAnalysisRunInfo &runInfo) const;
    QString writeEnergyDistributionImage(const QString &fileName, const Result &result) const;

    QString currentFile;
    QString simulationResultBasePath;
    QString temporarySimulationInputFile;
    Result result;
    QVector<QString> lablename;
    QString vtfilenames;
    bool operationRunning = false;
};

#endif // SIMULATIONMANAGER_H
