#include "SimulationManager.h"
#include "QMessageBox"
#include "QFileDialog"
#include <QApplication>
#include <QDebug>
#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QPointer>
#include <QTextStream>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {
constexpr long double kBalancedEnergyWorkThreshold = 1.0e8L;
constexpr long double kFastEnergyWorkThreshold = 1.0e9L;
constexpr double kBalancedEnergyTimeStep = 1.0e-16;
constexpr double kFastEnergyTimeStep = 2.0e-15;

QString formatEnergyValue(double value)
{
    return QString::number(value, 'g', 10);
}

QString simulationResultBase(const QString &inputFile, const QString &configuredBase)
{
    if (!configuredBase.isEmpty()) {
        return configuredBase;
    }
    const QFileInfo fileInfo(inputFile);
    return fileInfo.absolutePath() + "/" + fileInfo.baseName();
}

void removeTemporarySimulationInput(const QString &fileName)
{
    if (!fileName.isEmpty()) {
        QFile::remove(fileName);
    }
}

class TemporarySimulationInputGuard
{
public:
    explicit TemporarySimulationInputGuard(QString fileName)
        : fileName_(std::move(fileName))
    {}

    ~TemporarySimulationInputGuard()
    {
        removeTemporarySimulationInput(fileName_);
    }

private:
    QString fileName_;
};

std::size_t countDesignCells(const QCADesign &design)
{
    std::size_t count = 0;
    for (const auto &layer : design) {
        count += simon::size(layer);
    }
    return count;
}

std::size_t energySampleCount(const EnergyAnalysisOption &option)
{
    return static_cast<std::size_t>(std::ceil(option.duration / option.time_step));
}

bool forceExactEnergyMode()
{
    const QByteArray mode = qgetenv("IFCN_ENERGY_MODE").toLower();
    return mode == "exact" || mode == "accurate";
}

bool forceFastEnergyMode()
{
    const QByteArray mode = qgetenv("IFCN_ENERGY_MODE").toLower();
    return mode == "fast";
}

bool forceBalancedEnergyMode()
{
    const QByteArray mode = qgetenv("IFCN_ENERGY_MODE").toLower();
    return mode == "balanced";
}

const char *energyModeName(SimulationManager::EnergyAnalysisRunInfo::Mode mode)
{
    using Mode = SimulationManager::EnergyAnalysisRunInfo::Mode;
    switch (mode) {
    case Mode::Fast:
        return QT_TRANSLATE_NOOP("SimulationManager", "Fast");
    case Mode::Balanced:
        return QT_TRANSLATE_NOOP("SimulationManager", "Balanced");
    case Mode::Accurate:
    default:
        return QT_TRANSLATE_NOOP("SimulationManager", "Accurate");
    }
}

SimulationManager::EnergyAnalysisRunInfo configureEnergyAnalysis(EnergyAnalysisOption &option,
                                                                 std::size_t cellCount)
{
    SimulationManager::EnergyAnalysisRunInfo info;
    info.cellCount = cellCount;
    info.timeStep = option.time_step;
    info.duration = option.duration;
    info.sampleCount = energySampleCount(option);

    const long double workItems = static_cast<long double>(std::max<std::size_t>(1, cellCount)) *
                                  static_cast<long double>(std::max<std::size_t>(1, info.sampleCount));
    if (forceFastEnergyMode() || (!forceExactEnergyMode() && workItems > kFastEnergyWorkThreshold)) {
        option.time_step = std::max(option.time_step, kFastEnergyTimeStep);
        info.mode = SimulationManager::EnergyAnalysisRunInfo::Mode::Fast;
        info.timeStep = option.time_step;
        info.sampleCount = energySampleCount(option);
    } else if (forceBalancedEnergyMode() ||
               (!forceExactEnergyMode() && workItems > kBalancedEnergyWorkThreshold)) {
        option.time_step = std::max(option.time_step, kBalancedEnergyTimeStep);
        info.mode = SimulationManager::EnergyAnalysisRunInfo::Mode::Balanced;
        info.timeStep = option.time_step;
        info.sampleCount = energySampleCount(option);
    }

    return info;
}

QColor energyHeatColor(double normalized)
{
    normalized = std::clamp(normalized, 0.0, 1.0);

    if (normalized < 0.33) {
        const double t = normalized / 0.33;
        return QColor(static_cast<int>(40 + 20 * t),
                      static_cast<int>(85 + 150 * t),
                      static_cast<int>(190 + 45 * t));
    }
    if (normalized < 0.66) {
        const double t = (normalized - 0.33) / 0.33;
        return QColor(static_cast<int>(60 + 215 * t),
                      static_cast<int>(235 - 35 * t),
                      static_cast<int>(235 - 185 * t));
    }

    const double t = (normalized - 0.66) / 0.34;
    return QColor(static_cast<int>(255),
                  static_cast<int>(200 - 150 * t),
                  static_cast<int>(50 - 20 * t));
}
}

SimulationManager::SimulationManager(QObject *parent) : QObject(parent) {
    // Initialization if needed
}

bool SimulationManager::setSimulationInputFile(
        const QString &inputFile,
        const QString &resultBasePath,
        const QString &temporaryInputFile)
{
    if (operationRunning) {
        emit operationProgress(
                tr("Another simulation or energy analysis is already running; current task continues."),
                -1, 0);
        return false;
    }

    curfileName = inputFile;
    simulationResultBasePath = resultBasePath;
    temporarySimulationInputFile = temporaryInputFile;
    return true;
}

void SimulationManager::startWorkerOperation(const QString &title,
                                             const QString &detail,
                                             std::function<void()> task)
{
    if (operationRunning) {
        emit operationProgress(tr("Another simulation or energy analysis is already running; current task continues."), -1, 0);
        return;
    }

    operationRunning = true;
    emit operationStarted(title, detail);

    QThread *thread = QThread::create([task = std::move(task)]() mutable {
        task();
    });

    connect(thread, &QThread::finished, this, [this]() {
        operationRunning = false;
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void SimulationManager::bistableSim(const std::string &fname, Result &result) {
    QCADesign design;
    VectorTable vector_table;
    // Result result;
    QVector<std::string> inames;
    QVector<std::string> onames;
    if (!parse_design(fname, design)) {
        throw std::runtime_error("Unable to parse QCA circuit file");
    }
    QCABistableOption option;

    BistableAlgorithm algorithm(option);
    algorithm.run(design, vector_table, result, SimulationMode::Exhaustive, inames.toStdVector(), onames.toStdVector());
}

AcceleratedBistableStatistics SimulationManager::acceleratedBistableSim(
        const std::string &fname, Result &result) {
    QCADesign design;
    VectorTable vector_table;
    QVector<std::string> inames;
    QVector<std::string> onames;
    if (!parse_design(fname, design)) {
        throw std::runtime_error("Unable to parse QCA circuit file");
    }

    QCABistableOption option;
    AcceleratedBistableAlgorithm algorithm(option);
    algorithm.run(design, vector_table, result, SimulationMode::Exhaustive,
                  inames.toStdVector(), onames.toStdVector());
    return algorithm.statistics();
}

void SimulationManager::bistableSimWithSelective(const std::string &fname, const std::string &vfname, Result &result) {
    
    QCADesign design;
    if (!parse_design(fname, design)) {
        throw std::runtime_error("Unable to parse QCA circuit file");
    }

    QCABistableOption option;
    VectorTable vector_table;

    if (!parse_vector_table(vfname, vector_table)) {
        throw std::runtime_error("Unable to parse vector table file");
    }

    BistableAlgorithm algo(option);
    algo.run(design, vector_table, result, SimulationMode::Selective);
}

void SimulationManager::coherenceSim(const std::string &fname, Result &result) {
    QCADesign design;
    VectorTable vector_table;
    // Result result;
    QVector<std::string> inames;
    QVector<std::string> onames;
    if (!parse_design(fname, design)) {
        throw std::runtime_error("Unable to parse QCA circuit file");
    }
    QCACoherenceOption option;

    CoherenceAlgorithm algorithm(option);
    algorithm.run(design, vector_table, result, SimulationMode::Exhaustive, inames.toStdVector(), onames.toStdVector());
}

AcceleratedCoherenceStatistics SimulationManager::acceleratedCoherenceSim(
        const std::string &fname, Result &result) {
    QCADesign design;
    VectorTable vector_table;
    QVector<std::string> inames;
    QVector<std::string> onames;
    if (!parse_design(fname, design)) {
        throw std::runtime_error("Unable to parse QCA circuit file");
    }

    QCACoherenceOption option;
    AcceleratedCoherenceAlgorithm algorithm(option);
    algorithm.run(design, vector_table, result, SimulationMode::Exhaustive,
                  inames.toStdVector(), onames.toStdVector());
    return algorithm.statistics();
}

void SimulationManager::coherenceSimWithSelective(const std::string &fname, const std::string &vfname, Result &result) {
    QCADesign design;
    if (!parse_design(fname, design)) {
        throw std::runtime_error("Unable to parse QCA circuit file");
    }

    QCACoherenceOption option;
    VectorTable vector_table;

    if (!parse_vector_table(vfname, vector_table)) {
        throw std::runtime_error("Unable to parse vector table file");
    }

    CoherenceAlgorithm algo(option);
    algo.run(design, vector_table, result, SimulationMode::Selective);
}

SimulationManager::EnergyAnalysisRunInfo SimulationManager::energyAnalysis(const std::string &fname, Result &result){
    
    QCADesign design;

    bool ret = parse_design(fname, design);
    assert(ret);

    EnergyAnalysisOption option;
    VectorTable vector_table;
    const EnergyAnalysisRunInfo runInfo = configureEnergyAnalysis(option, countDesignCells(design));

    COSEnergyAnalysisAlgorithm algo(option);
    algo.run(design, vector_table, result, SimulationMode::Exhaustive);
    return runInfo;
}

void SimulationManager::runEnergyAnalysisForFile(const QString &fileName, const QString &sourceFileName)
{
    if (fileName.isEmpty()) {
        emit operationFailed(tr("Energy analysis canceled: no input file."));
        emit energyAnalysisFailed(tr("Energy analysis canceled: no input file."));
        return;
    }

    const QString displayName = sourceFileName.isEmpty() ? fileName : sourceFileName;
    QPointer<SimulationManager> self(this);
    startWorkerOperation(
        tr("Energy analysis"),
        tr("Running %1").arg(QFileInfo(displayName).fileName()),
        [self, fileName, sourceFileName, displayName]() {
            if (!self) {
                return;
            }

            try {
                emit self->operationProgress(QObject::tr("Solving energy model"), -1, 0);

                Result result;
                const EnergyAnalysisRunInfo runInfo = self->energyAnalysis(fileName.toStdString(), result);

                emit self->operationProgress(QObject::tr("Writing energy report"), -1, 0);

                const QFileInfo outputInfo(sourceFileName.isEmpty() ? fileName : sourceFileName);
                const QString outputBase = outputInfo.absolutePath() + "/" + outputInfo.completeBaseName();
                const QString waveformFileName = outputBase + "_energy.rst";
                const QString reportFileName = outputBase + "_energy.txt";

                if (!result.outputs.empty() && result.clocks.size() == 4) {
                    result.write_text_file(waveformFileName.toStdString());
                }
                result.write_energy_analysis_file(reportFileName.toStdString());

                emit self->operationProgress(QObject::tr("Rendering energy distribution"), -1, 0);

                const QString distributionImageName = self->writeEnergyDistributionImage(
                    outputBase + "_energy_distribution.png", result);
                const QString message = self->formatEnergyAnalysisStatus(
                    displayName, reportFileName, distributionImageName, result, runInfo);
                emit self->energyAnalysisFinished(message, waveformFileName, reportFileName, distributionImageName);
                emit self->operationFinished(QObject::tr("Energy analysis completed: %1").arg(reportFileName));
            } catch (const std::exception &e) {
                const QString message = QObject::tr("Energy analysis failed: %1").arg(e.what());
                emit self->energyAnalysisFailed(message);
                emit self->operationFailed(message);
            } catch (...) {
                const QString message = QObject::tr("Energy analysis failed.");
                emit self->energyAnalysisFailed(message);
                emit self->operationFailed(message);
            }
        });
}

QString SimulationManager::formatEnergyAnalysisStatus(const QString &sourceFileName,
                                                      const QString &reportFileName,
                                                      const QString &distributionImageName,
                                                      const Result &result,
                                                      const EnergyAnalysisRunInfo &runInfo) const
{
    QString message;
    QTextStream out(&message);

    out << tr("Energy Analysis: %1").arg(QFileInfo(sourceFileName).fileName()) << "\n";
    if (!result.energy_analysis.available) {
        out << tr("No energy summary was generated.") << "\n";
        out << tr("Report: %1").arg(reportFileName);
        return message;
    }

    const auto &energy = result.energy_analysis;
    out << tr("Mode: %1, time step=%2 s, samples=%3, cells=%4")
               .arg(tr(energyModeName(runInfo.mode)),
                    formatEnergyValue(runInfo.timeStep))
               .arg(runInfo.sampleCount)
               .arg(runInfo.cellCount)
        << "\n";
    out << tr("Total: E_bath=%1 eV, E_clk=%2 eV, E_io=%3 eV, E_error=%4 eV")
               .arg(formatEnergyValue(energy.total_bath_eV),
                    formatEnergyValue(energy.total_clock_eV),
                    formatEnergyValue(energy.total_io_eV),
                    formatEnergyValue(energy.total_error_eV))
        << "\n";
    out << tr("Average/cycle: E_bath=%1 eV, E_clk=%2 eV, E_io=%3 eV, E_error=%4 eV")
               .arg(formatEnergyValue(energy.average_bath_eV),
                    formatEnergyValue(energy.average_clock_eV),
                    formatEnergyValue(energy.average_io_eV),
                    formatEnergyValue(energy.average_error_eV))
        << "\n";
    out << tr("Cycles: %1, Cells: %2").arg(energy.cycle_count).arg(energy.cells.size()) << "\n";
    out << tr("Report: %1").arg(reportFileName);
    if (!distributionImageName.isEmpty()) {
        out << "\n" << tr("Distribution: %1").arg(distributionImageName);
    }
    return message;
}

QString SimulationManager::writeEnergyDistributionImage(const QString &fileName, const Result &result) const
{
    if (!result.energy_analysis.available || result.energy_analysis.cells.empty()) {
        return QString();
    }

    const auto &cells = result.energy_analysis.cells;
    double minX = std::numeric_limits<double>::max();
    double minY = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double maxY = std::numeric_limits<double>::lowest();
    double maxEnergy = 0.0;

    for (const auto &cell : cells) {
        minX = std::min(minX, cell.x);
        minY = std::min(minY, cell.y);
        maxX = std::max(maxX, cell.x);
        maxY = std::max(maxY, cell.y);
        maxEnergy = std::max(maxEnergy, std::abs(cell.error_eV));
    }

    if (!std::isfinite(minX) || !std::isfinite(minY) || !std::isfinite(maxX) || !std::isfinite(maxY)) {
        return QString();
    }

    const double spanX = std::max(20.0, maxX - minX + 40.0);
    const double spanY = std::max(20.0, maxY - minY + 40.0);
    const int margin = 52;
    const int legendWidth = 160;
    const int plotMaxWidth = 1800;
    const int plotMaxHeight = 1400;
    const double scale = std::min(static_cast<double>(plotMaxWidth) / spanX,
                                  static_cast<double>(plotMaxHeight) / spanY);
    const int plotWidth = std::max(260, static_cast<int>(spanX * scale));
    const int plotHeight = std::max(220, static_cast<int>(spanY * scale));
    const int imageWidth = plotWidth + margin * 2 + legendWidth;
    const int imageHeight = plotHeight + margin * 2;
    const double cellSize = std::clamp(18.0 * scale, 3.0, 18.0);

    QImage image(imageWidth, imageHeight, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(45, 45, 45), 1));
    painter.drawText(QPointF(margin, 28), QObject::tr("Energy distribution (E_error eV)"));

    const QRectF plotRect(margin, margin, plotWidth, plotHeight);
    painter.fillRect(plotRect, QColor(248, 249, 250));
    painter.drawRect(plotRect);

    for (const auto &cell : cells) {
        double normalized = 0.0;
        if (maxEnergy > 0.0) {
            normalized = std::sqrt(std::abs(cell.error_eV) / maxEnergy);
        }
        QColor color = energyHeatColor(normalized);
        if (std::abs(cell.error_eV) <= 0.0) {
            color = QColor(210, 214, 220);
        }

        const double xPos = plotRect.left() + (cell.x - minX + 20.0) * scale;
        const double yPos = plotRect.top() + (cell.y - minY + 20.0) * scale;
        const double layerOffset = static_cast<double>(cell.layer_index) * std::min(2.0, cellSize * 0.2);
        const QRectF cellRect(xPos - cellSize / 2.0 + layerOffset,
                              yPos - cellSize / 2.0 + layerOffset,
                              cellSize,
                              cellSize);
        painter.fillRect(cellRect, color);
        painter.setPen(QPen(QColor(40, 40, 40, 150), 0.6));
        painter.drawRect(cellRect);
    }

    const int legendX = margin + plotWidth + 38;
    const int legendY = margin + 26;
    const int legendHeight = std::min(220, plotHeight - 52);
    for (int y = 0; y < legendHeight; ++y) {
        const double normalized = 1.0 - static_cast<double>(y) / std::max(1, legendHeight - 1);
        painter.setPen(energyHeatColor(normalized));
        painter.drawLine(legendX, legendY + y, legendX + 24, legendY + y);
    }
    painter.setPen(QPen(QColor(45, 45, 45), 1));
    painter.drawRect(QRect(legendX, legendY, 24, legendHeight));
    painter.drawText(QPointF(legendX + 34, legendY + 8), formatEnergyValue(maxEnergy));
    painter.drawText(QPointF(legendX + 34, legendY + legendHeight), QStringLiteral("0"));
    painter.drawText(QPointF(legendX, legendY + legendHeight + 28), QObject::tr("hotter = higher"));

    painter.end();
    return image.save(fileName) ? fileName : QString();
}

void SimulationManager::slotSavedname(QString fileName)//for 仿真文件名
{
    this->curfileName = fileName;
    simulationResultBasePath.clear();
    temporarySimulationInputFile.clear();
}

void SimulationManager::slotBistableSim() {

    //QString curFile = QFileDialog::getOpenFileName(nullptr, tr("Open File"), ".", tr("QCA files (*.qca)"));
    QString curFile = curfileName;
    const QString resultBasePath = simulationResultBase(curFile, simulationResultBasePath);
    const QString temporaryInputFile = temporarySimulationInputFile;
    if (curFile.isEmpty()) {
        removeTemporarySimulationInput(temporaryInputFile);
        const QString message = tr("Bistable simulation canceled: no circuit file.");
        emit simulationFailed(message);
        emit operationFailed(message);
        return;
    }

    QPointer<SimulationManager> self(this);
    startWorkerOperation(
        tr("Bistable simulation"),
        tr("Running %1").arg(QFileInfo(curFile).fileName()),
        [self, curFile, resultBasePath, temporaryInputFile]() {
            const TemporarySimulationInputGuard snapshotGuard(temporaryInputFile);
            if (!self) {
                return;
            }

            try {
                emit self->operationProgress(QObject::tr("Solving bistable simulation"), -1, 0);

                Result result;
                self->bistableSim(curFile.toStdString(), result);
                const QString outputFileName = resultBasePath + ".rst";

                emit self->operationProgress(QObject::tr("Writing simulation waveform"), -1, 0);
                result.write_text_file(outputFileName.toStdString());

                emit self->simulationFinished(outputFileName);
                emit self->operationFinished(QObject::tr("Bistable simulation completed: %1").arg(outputFileName));
            } catch (const std::exception &e) {
                const QString message = QObject::tr("Bistable simulation failed: %1").arg(e.what());
                emit self->simulationFailed(message);
                emit self->operationFailed(message);
            } catch (...) {
                const QString message = QObject::tr("Bistable simulation failed.");
                emit self->simulationFailed(message);
                emit self->operationFailed(message);
            }
        });
}

void SimulationManager::slotAcceleratedBistableSim() {
    const QString curFile = curfileName;
    const QString resultBasePath = simulationResultBase(curFile, simulationResultBasePath);
    const QString temporaryInputFile = temporarySimulationInputFile;
    if (curFile.isEmpty()) {
        removeTemporarySimulationInput(temporaryInputFile);
        const QString message = tr("Accelerated bistable simulation canceled: no circuit file.");
        emit simulationFailed(message);
        emit operationFailed(message);
        return;
    }

    QPointer<SimulationManager> self(this);
    startWorkerOperation(
        tr("Accelerated bistable simulation"),
        tr("Running %1").arg(QFileInfo(curFile).fileName()),
        [self, curFile, resultBasePath, temporaryInputFile]() {
            const TemporarySimulationInputGuard snapshotGuard(temporaryInputFile);
            if (!self) {
                return;
            }

            try {
                emit self->operationProgress(
                        QObject::tr("Solving accelerated bistable simulation"), -1, 0);

                Result result;
                const AcceleratedBistableStatistics statistics =
                        self->acceleratedBistableSim(curFile.toStdString(), result);
                const QString outputFileName = resultBasePath +
                        "_accelerated_bistable.rst";

                emit self->operationProgress(
                        QObject::tr("Writing accelerated bistable waveform"), -1, 0);
                result.write_text_file(outputFileName.toStdString());

                const QString summary = QObject::tr(
                        "Accelerated bistable simulation completed: %1 "
                        "[samples=%2, sweeps=%3, cell updates=%4, converged=%5, "
                        "max-iteration samples=%6, dynamic cells=%7, couplings=%8]")
                        .arg(outputFileName)
                        .arg(static_cast<qulonglong>(statistics.samples))
                        .arg(static_cast<qulonglong>(statistics.sweeps))
                        .arg(static_cast<qulonglong>(statistics.cell_updates))
                        .arg(static_cast<qulonglong>(statistics.converged_samples))
                        .arg(static_cast<qulonglong>(statistics.max_iteration_samples))
                        .arg(static_cast<qulonglong>(statistics.dynamic_cells))
                        .arg(static_cast<qulonglong>(statistics.directed_couplings));
                emit self->simulationFinished(outputFileName);
                emit self->operationFinished(summary);
            } catch (const std::exception &e) {
                const QString message = QObject::tr(
                        "Accelerated bistable simulation failed: %1").arg(e.what());
                emit self->simulationFailed(message);
                emit self->operationFailed(message);
            } catch (...) {
                const QString message = QObject::tr(
                        "Accelerated bistable simulation failed.");
                emit self->simulationFailed(message);
                emit self->operationFailed(message);
            }
        });
}

void SimulationManager::slotSimWithSelective()
{
    Typewindow * typewindow = new Typewindow(nullptr, this->lablename);
    typewindow->show();
    connect(typewindow, &Typewindow::sendvtnames, this, &SimulationManager::slotSendvtnames);
}

void SimulationManager::slotSavedinputname(QVector<QString> inputname)
{
    this->lablename = inputname;
}

void SimulationManager::slotSendvtnames(const QString &fileName)
{
    this->vtfilenames = fileName;
}

void SimulationManager::slotBistableSimWithSelective() {
    QString curFile = curfileName;
    const QString resultBasePath = simulationResultBase(curFile, simulationResultBasePath);
    const QString temporaryInputFile = temporarySimulationInputFile;
    if (curFile.isEmpty()) {
        removeTemporarySimulationInput(temporaryInputFile);
        const QString message = tr("Selective bistable simulation canceled: no circuit file.");
        emit simulationFailed(message);
        emit operationFailed(message);
        return;
    }
    QString vframe = vtfilenames;
    if (vframe.isEmpty()) {
        removeTemporarySimulationInput(temporaryInputFile);
        const QString message = tr("Selective bistable simulation canceled: no vector table file.");
        emit simulationFailed(message);
        emit operationFailed(message);
        return;
    }

    QPointer<SimulationManager> self(this);
    startWorkerOperation(
        tr("Selective bistable simulation"),
        tr("Running %1").arg(QFileInfo(curFile).fileName()),
        [self, curFile, vframe, resultBasePath, temporaryInputFile]() {
            const TemporarySimulationInputGuard snapshotGuard(temporaryInputFile);
            if (!self) {
                return;
            }

            try {
                emit self->operationProgress(QObject::tr("Solving selective bistable simulation"), -1, 0);

                Result result;
                self->bistableSimWithSelective(curFile.toStdString(), vframe.toStdString(), result);
                const QString outputFileName = resultBasePath + ".rst";

                emit self->operationProgress(QObject::tr("Writing simulation waveform"), -1, 0);
                result.write_text_file(outputFileName.toStdString());

                emit self->simulationFinished(outputFileName);
                emit self->operationFinished(QObject::tr("Selective bistable simulation completed: %1").arg(outputFileName));
            } catch (const std::exception &e) {
                const QString message = QObject::tr("Selective bistable simulation failed: %1").arg(e.what());
                emit self->simulationFailed(message);
                emit self->operationFailed(message);
            } catch (...) {
                const QString message = QObject::tr("Selective bistable simulation failed.");
                emit self->simulationFailed(message);
                emit self->operationFailed(message);
            }
        });
}

void SimulationManager::slotCoherenceSim() {
    QString curFile = curfileName;
    const QString resultBasePath = simulationResultBase(curFile, simulationResultBasePath);
    const QString temporaryInputFile = temporarySimulationInputFile;
    if (curFile.isEmpty()) {
        removeTemporarySimulationInput(temporaryInputFile);
        const QString message = tr("Coherence simulation canceled: no circuit file.");
        emit simulationFailed(message);
        emit operationFailed(message);
        return;
    }

    QPointer<SimulationManager> self(this);
    startWorkerOperation(
        tr("Coherence simulation"),
        tr("Running %1").arg(QFileInfo(curFile).fileName()),
        [self, curFile, resultBasePath, temporaryInputFile]() {
            const TemporarySimulationInputGuard snapshotGuard(temporaryInputFile);
            if (!self) {
                return;
            }

            try {
                emit self->operationProgress(QObject::tr("Solving coherence simulation"), -1, 0);

                Result result;
                self->coherenceSim(curFile.toStdString(), result);
                const QString outputFileName = resultBasePath + ".rst";

                emit self->operationProgress(QObject::tr("Writing simulation waveform"), -1, 0);
                result.write_text_file(outputFileName.toStdString());

                emit self->simulationFinished(outputFileName);
                emit self->operationFinished(QObject::tr("Coherence simulation completed: %1").arg(outputFileName));
            } catch (const std::exception &e) {
                const QString message = QObject::tr("Coherence simulation failed: %1").arg(e.what());
                emit self->simulationFailed(message);
                emit self->operationFailed(message);
            } catch (...) {
                const QString message = QObject::tr("Coherence simulation failed.");
                emit self->simulationFailed(message);
                emit self->operationFailed(message);
            }
        });
}

void SimulationManager::slotAcceleratedCoherenceSim() {
    const QString curFile = curfileName;
    const QString resultBasePath = simulationResultBase(curFile, simulationResultBasePath);
    const QString temporaryInputFile = temporarySimulationInputFile;
    if (curFile.isEmpty()) {
        removeTemporarySimulationInput(temporaryInputFile);
        const QString message = tr("Accelerated coherence simulation canceled: no circuit file.");
        emit simulationFailed(message);
        emit operationFailed(message);
        return;
    }

    QPointer<SimulationManager> self(this);
    startWorkerOperation(
        tr("Accelerated coherence simulation"),
        tr("Running %1").arg(QFileInfo(curFile).fileName()),
        [self, curFile, resultBasePath, temporaryInputFile]() {
            const TemporarySimulationInputGuard snapshotGuard(temporaryInputFile);
            if (!self) {
                return;
            }

            try {
                emit self->operationProgress(
                        QObject::tr("Solving accelerated coherence simulation"), -1, 0);

                Result result;
                const AcceleratedCoherenceStatistics statistics =
                        self->acceleratedCoherenceSim(curFile.toStdString(), result);
                const QString outputFileName = resultBasePath +
                        "_accelerated_coherence.rst";

                emit self->operationProgress(
                        QObject::tr("Writing accelerated coherence waveform"), -1, 0);
                result.write_text_file(outputFileName.toStdString());

                const QString summary = QObject::tr(
                        "Accelerated coherence simulation completed: %1 "
                        "[samples=%2, cell updates=%3, field accumulations=%4, "
                        "dynamic cells=%5, couplings=%6, clock cache=%7, input cache=%8]")
                        .arg(outputFileName)
                        .arg(static_cast<qulonglong>(statistics.samples))
                        .arg(static_cast<qulonglong>(statistics.cell_updates))
                        .arg(static_cast<qulonglong>(statistics.field_accumulations))
                        .arg(static_cast<qulonglong>(statistics.dynamic_cells))
                        .arg(static_cast<qulonglong>(statistics.directed_couplings))
                        .arg(statistics.clock_cache_used ? QObject::tr("on")
                                                         : QObject::tr("off"))
                        .arg(statistics.input_cache_used ? QObject::tr("on")
                                                         : QObject::tr("off"));
                emit self->simulationFinished(outputFileName);
                emit self->operationFinished(summary);
            } catch (const std::exception &e) {
                const QString message = QObject::tr(
                        "Accelerated coherence simulation failed: %1").arg(e.what());
                emit self->simulationFailed(message);
                emit self->operationFailed(message);
            } catch (...) {
                const QString message = QObject::tr(
                        "Accelerated coherence simulation failed.");
                emit self->simulationFailed(message);
                emit self->operationFailed(message);
            }
        });
}

void SimulationManager::slotCoherenceSimWithSelective() {
    QString curFile = curfileName;
    const QString resultBasePath = simulationResultBase(curFile, simulationResultBasePath);
    const QString temporaryInputFile = temporarySimulationInputFile;
    if (curFile.isEmpty()) {
        removeTemporarySimulationInput(temporaryInputFile);
        const QString message = tr("Selective coherence simulation canceled: no circuit file.");
        emit simulationFailed(message);
        emit operationFailed(message);
        return;
    }
    QString vframe = vtfilenames;
    if (vframe.isEmpty()) {
        removeTemporarySimulationInput(temporaryInputFile);
        const QString message = tr("Selective coherence simulation canceled: no vector table file.");
        emit simulationFailed(message);
        emit operationFailed(message);
        return;
    }

    QPointer<SimulationManager> self(this);
    startWorkerOperation(
        tr("Selective coherence simulation"),
        tr("Running %1").arg(QFileInfo(curFile).fileName()),
        [self, curFile, vframe, resultBasePath, temporaryInputFile]() {
            const TemporarySimulationInputGuard snapshotGuard(temporaryInputFile);
            if (!self) {
                return;
            }

            try {
                emit self->operationProgress(QObject::tr("Solving selective coherence simulation"), -1, 0);

                Result result;
                self->coherenceSimWithSelective(curFile.toStdString(), vframe.toStdString(), result);
                const QString outputFileName = resultBasePath + ".rst";

                emit self->operationProgress(QObject::tr("Writing simulation waveform"), -1, 0);
                result.write_text_file(outputFileName.toStdString());

                emit self->simulationFinished(outputFileName);
                emit self->operationFinished(QObject::tr("Selective coherence simulation completed: %1").arg(outputFileName));
            } catch (const std::exception &e) {
                const QString message = QObject::tr("Selective coherence simulation failed: %1").arg(e.what());
                emit self->simulationFailed(message);
                emit self->operationFailed(message);
            } catch (...) {
                const QString message = QObject::tr("Selective coherence simulation failed.");
                emit self->simulationFailed(message);
                emit self->operationFailed(message);
            }
        });
}

void SimulationManager::slotEnergyAnalysis() {
    QString curFile = QFileDialog::getOpenFileName(QApplication::activeWindow(), tr("Open File"), ".", tr("QCA files (*.qca)"));
    if (curFile.isEmpty()) {
        return;
    }
    runEnergyAnalysisForFile(curFile);
}
