#ifndef SIMULATIONMANAGER_H
#define SIMULATIONMANAGER_H

#include <QObject>
#include <string>
#include <simon/simon.hpp>
#include "widgets/waveformwindow.h"
#include "widgets/typewindow.h"
// using namespace fcngraph;
using namespace simon;
class SimulationManager : public QObject {
    Q_OBJECT
public:
    explicit SimulationManager(QObject *parent = nullptr);

    void bistableSim(const std::string &fname, Result &result);
    void bistableSimWithSelective(const std::string &fname, const std::string &vfname, Result &result);
    void coherenceSim(const std::string &fname, Result &result);
    void coherenceSimWithSelective(const std::string &fname, const std::string &vfname, Result &result);
    void energyAnalysis(const std::string &fname, Result &result);

signals:
    void simulationFinished(const QString &outputFileName);

public slots:
    void slotBistableSim();
    void slotBistableSimWithSelective();
    void slotCoherenceSim();
    void slotCoherenceSimWithSelective();
    void slotEnergyAnalysis();
    void savedname(QString fileName);
    void savedinputname(QVector<QString> inputname);
    void sendvtnames(const QString &fileName);
    void slotSimWithSelective();
public:
    QString curfileName;
private:
    QString currentFile;
    Result result;
    QVector<QString> lablename;
    QString vtfilenames;
};

#endif // SIMULATIONMANAGER_H
