#include "SimulationManager.h"
#include "QMessageBox"
#include "QFileDialog"
#include <QDebug>
#include <QThread>
SimulationManager::SimulationManager(QObject *parent) : QObject(parent) {
    // Initialization if needed
}

void SimulationManager::bistableSim(const std::string &fname, Result &result) {
    QCADesign design;
    VectorTable vector_table;
    // Result result;
    QVector<std::string> inames;
    QVector<std::string> onames;
    parse_design(fname, design);
    QCABistableOption option;

    BistableAlgorithm algorithm(option);
    algorithm.run(design, vector_table, result, SimulationMode::Exhaustive, inames.toStdVector(), onames.toStdVector());
}

void SimulationManager::bistableSimWithSelective(const std::string &fname, const std::string &vfname, Result &result) {
    
    QCADesign design;
    bool ret = parse_design(fname, design);

    QCABistableOption option;
    VectorTable vector_table;

    bool parsing_result = parse_vector_table(vfname, vector_table);

    BistableAlgorithm algo(option);
    algo.run(design, vector_table, result, SimulationMode::Selective);
}

void SimulationManager::coherenceSim(const std::string &fname, Result &result) {
    QCADesign design;
    VectorTable vector_table;
    // Result result;
    QVector<std::string> inames;
    QVector<std::string> onames;
    parse_design(fname, design);
    QCACoherenceOption option;

    CoherenceAlgorithm algorithm(option);
    algorithm.run(design, vector_table, result, SimulationMode::Exhaustive, inames.toStdVector(), onames.toStdVector());
}

void SimulationManager::coherenceSimWithSelective(const std::string &fname, const std::string &vfname, Result &result) {
    QCADesign design;
    bool ret = parse_design(fname, design);

    QCACoherenceOption option;
    VectorTable vector_table;

    bool parsing_result = parse_vector_table(vfname, vector_table);

    CoherenceAlgorithm algo(option);
    algo.run(design, vector_table, result, SimulationMode::Selective);
}

void SimulationManager::energyAnalysis(const std::string &fname, Result &result){
    
    QCADesign design;

    bool ret = parse_design(fname, design);
    assert(ret);

    EnergyAnalysisOption option;
    VectorTable vector_table;

    COSEnergyAnalysisAlgorithm algo(option);
    algo.run(design, vector_table, result, SimulationMode::Exhaustive);
 
}
void SimulationManager::savedname(QString fileName)//for 仿真文件名
{
    this->curfileName = fileName;
}
void SimulationManager::slotBistableSim() {

    //QString curFile = QFileDialog::getOpenFileName(nullptr, tr("Open File"), ".", tr("QCA files (*.qca)"));
    QString curFile = curfileName;
    if (curFile.isEmpty()) {
        qDebug()<<"文件为空";
        return;
    }
    Result result;
    qDebug() << "当前线程对象的地址: " << QThread::currentThread();
    bistableSim(curFile.toStdString(), result);
    QString output_file_name;
    QFileInfo fileInfo(curFile);
    output_file_name = fileInfo.absolutePath() + "/" + fileInfo.baseName() + ".rst";
    result.write_text_file(output_file_name.toStdString());
    WaveformWindow* waveWindow =  new WaveformWindow(nullptr, output_file_name);
    waveWindow->show();
}
void SimulationManager::slotSimWithSelective()
{
    Typewindow * typewindow = new Typewindow(nullptr, this->lablename);
    typewindow->show();
    connect(typewindow, &Typewindow::sendvtnames, this, &SimulationManager::sendvtnames);
}

void SimulationManager::savedinputname(QVector<QString> inputname)
{
    this->lablename = inputname;
}

void SimulationManager::sendvtnames(const QString &fileName)
{
    this->vtfilenames = fileName;
}

void SimulationManager::slotBistableSimWithSelective() {
    QString curFile = curfileName;
    if (curFile.isEmpty()) {
        return;
    }
    Result result;
    QString vframe = vtfilenames;
    bistableSimWithSelective(curFile.toStdString(),vframe.toStdString(), result);
    QString output_file_name;
    QFileInfo fileInfo(curFile);
    output_file_name = fileInfo.absolutePath() + "/" + fileInfo.baseName() + ".rst";
    result.write_text_file(output_file_name.toStdString());
    WaveformWindow* waveWindow =  new WaveformWindow(nullptr, output_file_name);
    waveWindow->show();  
}

void SimulationManager::slotCoherenceSim() {
    QString curFile = curfileName;
    if (curFile.isEmpty()) {
        qDebug()<<"文件为空";
        return;
    }
    Result result;
    qDebug() << "当前线程对象的地址: " << QThread::currentThread();
    coherenceSim(curFile.toStdString(), result);
    QString output_file_name;
    QFileInfo fileInfo(curFile);
    output_file_name = fileInfo.absolutePath() + "/" + fileInfo.baseName() + ".rst";
    result.write_text_file(output_file_name.toStdString());
            // 发射仿真完成信号到主线程
        emit simulationFinished(output_file_name);
}

void SimulationManager::slotCoherenceSimWithSelective() {
    QString curFile = curfileName;
    if (curFile.isEmpty()) {
        return;
    }
    Result result;
    QString vframe = vtfilenames;
    coherenceSimWithSelective(curFile.toStdString(),vframe.toStdString(), result);
    QString output_file_name;
    QFileInfo fileInfo(curFile);
    output_file_name = fileInfo.absolutePath() + "/" + fileInfo.baseName() + ".rst";
    result.write_text_file(output_file_name.toStdString());
    WaveformWindow* waveWindow =  new WaveformWindow(nullptr, output_file_name);
    waveWindow->show();
}

void SimulationManager::slotEnergyAnalysis() {
    QString curFile = QFileDialog::getOpenFileName(nullptr, tr("Open File"), ".", tr("QCA files (*.qca)"));
    if (curFile.isEmpty()) {
        return;
    }
    Result result;
    energyAnalysis(curFile.toStdString(), result);
    result.write_text_file(curFile.toStdString() + ".rst");
}
