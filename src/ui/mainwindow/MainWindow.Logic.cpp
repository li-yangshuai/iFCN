#include "ui/mainwindow/MainWindow.h"
#include "ui/mainwindow/MainWindow.Constants.h"
#include "ui/mainwindow/TabbedMainWindow.h"
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPixmap>
#include <QSettings>
#include <QStatusBar>
#include <QTextStream>

void MainWindow::initialDesign()
{
    /******** 初始化layer comboBox, 添加主层"Main Cell Layer" *********/
    layerComboBox->AddItem(tr("Main Cell Layer"), true); 

    // int idx = layerComboBox->GetNumRows() - 1;

    QVector<QGraphicsItem*> cells_layer;
    layers.push_back(cells_layer); 
    // layers[idx]->setZValue(idx);     //由layerComboBox的索引号决定
    // layers[idx]->setVisible(true);

    // scene->addItem(layers[idx]);
    //qDebug() << tr("layer:") << idx << tr(" zValue:") << layers[idx]->zValue();
    setDirty(true);
}

void MainWindow::loadFile(const QString &fileName)
{
    scene->clearFastRender();

    const QString suffix = QFileInfo(fileName).suffix().toLower();
    if (suffix == "ifcn" && shouldMapIfcnFile(fileName)) {
        mapIfcnFile(fileName);
        return;
    }

    QCADesign design;
    parse_design(fileName.toStdString(), design);

    beginSceneBatchUpdate();

    int layer_size = simon::size(design);

    for(int i = 0; i < layer_size; ++i)
    {
        auto &layer = simon::layers(design)[i];
        if(i == 0)
        {
            if(layers.size() == 0)
            {
                layerComboBox->AddItem(QString::fromStdString( simon::description(layer) ), true); 

                QVector<QGraphicsItem*> cells_layer;

                layers.push_back(cells_layer); 
                // layers[i]->setZValue(i);     //由layerComboBox的索引号决定
                // layers[i]->setVisible(true);

                // scene->addItem(layers[i]);
            }
            else    //layers.size() == 1
                layerComboBox->GetItem(0)->setText( QString::fromStdString( simon::description(layer) ) );
        }
        else
        {
            layerComboBox->AddItem(QString::fromStdString( simon::description(layer) ), true); 

            QVector<QGraphicsItem*> cells_layer;
            layers.push_back(cells_layer); 
            // layers[i]->setZValue(i);     //由layerComboBox的索引号决定
            // layers[i]->setVisible(true);

            // scene->addItem(layers[i]);
        }

        for(auto &cell : layer)
        {
            QCADCellItem *cellItem = new QCADCellItem(cell);
            cellItem->setPos(simon::x(*cellItem), simon::y(*cellItem));     //在scene层添加
            cellItem->setZValue(i);     //由layerComboBox的索引号决定
            cellItem->setVisible(true);
            if (!QString::fromStdString(simon::name(*cellItem)).isEmpty()) {
                cellItem->createNameLabel(QString::fromStdString(simon::name(*cellItem)));
            }
            scene->addItem(cellItem);
            layers[i].push_back(cellItem);
            
        }
    }

    setCurrentFile(fileName);
    statusBar()->showMessage(tr("Loaded %1").arg(fileName), 2000);
    emit savedname(fileName);
    endSceneBatchUpdate(true);
    // curFile = fileName;
    // simfileName = fileName;//for 仿真文件名
}

bool MainWindow::shouldMapIfcnFile(const QString &fileName) const
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return true;
    }

    QTextStream in(&file);
    for (int i = 0; i < 64 && !in.atEnd(); ++i) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty()) {
            continue;
        }
        if (line.startsWith(QStringLiteral("#circuit name:")) ||
            line.startsWith(QStringLiteral("#nodes info")) ||
            line.startsWith(QStringLiteral("#paths info")) ||
            line.startsWith(QStringLiteral("#phase map"))) {
            return true;
        }
        if (line.startsWith(QStringLiteral("[VERSION]")) ||
            line.startsWith(QStringLiteral("[TYPE:")) ||
            line.startsWith(QStringLiteral("qcadesigner_version="))) {
            return false;
        }
        break;
    }

    return true;
}

void MainWindow::mapIfcnFile(const QString &fileName)
{
    scene->clearFastRender();
    gateLevelMapping->parseGateLevelMappingFile(fileName);
    setCurrentFile(fileName);
    setDirty(true);
    statusBar()->showMessage(tr("Mapped %1").arg(fileName), 2000);
    emit savedname(fileName);
}
bool MainWindow::saveFile(const QString &fileName)
{
    QFile file(fileName);
    if(!file.open(QFile::WriteOnly))
    {
        QMessageBox::warning( this, tr("警告！"), tr("不能写入文件 %1:\n%2.")
                .arg( QDir::toNativeSeparators(fileName), file.errorString() ) );
        return false;
    }

    QTextStream out(&file);
    /*********文本保存************/
    //version信息
    out << "[VERSION]\n";
    out << "qcadesigner_version=2.000000\n";
    out << "[#VERSION]\n";

    //design信息
    out << "[TYPE:DESIGN]\n";

    //drawing layer
    out << "[TYPE:QCADLayer]\n";
    out << "type=3\n";
    out << "status=1\n";
    out << "pszDescription=Drawing Layer\n";
    out << "[#TYPE:QCADLayer]\n";

    //substrate
    out << "[TYPE:QCADLayer]\n";
    out << "type=0\n";
    out << "status=1\n";
    out << "pszDescription=Substrate\n";
    out << "[TYPE:QCADSubstrate]\n";
    out << "[TYPE:QCADStretchyObject]\n";
    out << "[TYPE:QCADDesignObject]\n";
    out << "x=3000.000000\n";
    out << "y=1500.000000\n";
    out << "bSelected=FALSE\n";
    out << "clr.red=65535\n";
    out << "clr.green=65535\n";
    out << "clr.blue=65535\n";
    out << "bounding_box.xWorld=0.000000\n";
    out << "bounding_box.yWorld=0.000000\n";
    out << "bounding_box.cxWorld=6000.000000\n";
    out << "bounding_box.cyWorld=3000.000000\n";
    out << "[#TYPE:QCADDesignObject]\n";
    out << "[#TYPE:QCADStretchyObject]\n";
    out << "grid_spacing=20.000000\n";
    out << "[#TYPE:QCADSubstrate]\n";
    out << "[#TYPE:QCADLayer]\n";
    //cell layers
    const bool useFastCells = scene->hasFastRender();
    const auto &fastCellsByLayer = scene->fastCellsByLayer();
    auto writeCellColor = [&out](FCNCellFunction function, int phase) {
        switch(function) {
            case FCNCellFunction::INPUT :
                out << "clr.red=0\n";
                out << "clr.green=0\n";
                out << "clr.blue=65535\n";
                break;
            case FCNCellFunction::OUTPUT :
                out << "clr.red=65535\n";
                out << "clr.green=65535\n";
                out << "clr.blue=0\n";
                break;
            case FCNCellFunction::FIXED :
                out << "clr.red=65535\n";
                out << "clr.green=32768\n";
                out << "clr.blue=0\n";
                break;
            default:
                switch (phase) {
                    case 0:
                        out << "clr.red=0\n";
                        out << "clr.green=65535\n";
                        out << "clr.blue=0\n";
                        break;
                    case 1:
                        out << "clr.red=65535\n";
                        out << "clr.green=0\n";
                        out << "clr.blue=65535\n";
                        break;
                    case 2:
                        out << "clr.red=0\n";
                        out << "clr.green=65535\n";
                        out << "clr.blue=65535\n";
                        break;
                    case 3:
                        out << "clr.red=65535\n";
                        out << "clr.green=65535\n";
                        out << "clr.blue=65535\n";
                        break;
                    default:
                        out << "clr.red=0\n";
                        out << "clr.green=0\n";
                        out << "clr.blue=0\n";
                        break;
                }
        }
    };
    auto writeCellData = [&](double x, double y, double width, double height, FCNCellFunction function,
                             QCACellMode mode, int phase, const QString &name) {
        out << "[TYPE:QCADCell]\n";
        out << "[TYPE:QCADDesignObject]\n";
        out << "x=" << x << "\n";
        out << "y=" << y << "\n";
        out << "bSelected=FALSE\n";
        writeCellColor(function, phase);
        out << "bounding_box.xWorld=" << x - 10 << "\n";
        out << "bounding_box.yWorld=" << y - 10 << "\n";
        out << "bounding_box.cxWorld=20\n";
        out << "bounding_box.cyWorld=20\n";
        out << "[#TYPE:QCADDesignObject]\n";

        out << "cell_options.cxCell=" << width << "\n";
        out << "cell_options.cyCell=" << height << "\n";
        out << "cell_options.dot_diameter=5.000000\n";
        out << "cell_options.clock=" << phase << "\n";
        switch (mode)
        {
        case QCACellMode::VERTICAL:
            out << "cell_options.mode=QCAD_CELL_MODE_VERTICAL\n";
            break;
        case QCACellMode::CLUSTER:
            out << "cell_options.mode=QCAD_CELL_MODE_CLUSTER\n";
            break;
        case QCACellMode::CROSSOVER:
            out << "cell_options.mode=QCAD_CELL_MODE_CROSSOVER\n";
            break;
        default:
            out << "cell_options.mode=QCAD_CELL_MODE_NORMAL\n";
            break;
        }
        switch(function) {
            case FCNCellFunction::NORMAL:
                out << "cell_function=QCAD_CELL_NORMAL\n";
                break;
            case FCNCellFunction::INPUT:
                out << "cell_function=QCAD_CELL_INPUT\n";
                break;
            case FCNCellFunction::OUTPUT:
                out << "cell_function=QCAD_CELL_OUTPUT\n";
                break;
            case FCNCellFunction::FIXED:
                out << "cell_function=QCAD_CELL_FIXED\n";
                break;
            case FCNCellFunction::LAST_FUNCTION:
                out << "cell_function=QCAD_CELL_LAST_FUNCTION\n";
                break;
            default:
                out << "cell_function=QCAD_CELL_NORMAL\n";
                break;
        }
        out << "number_of_dots=4\n";

        static const QPointF dotOffsets[] = {
            QPointF(4.5, -4.5),
            QPointF(4.5, 4.5),
            QPointF(-4.5, 4.5),
            QPointF(-4.5, -4.5)
        };
        for (const QPointF &offset : dotOffsets)
        {
            out << "[TYPE:CELL_DOT]\n";
            out << "x=" << x + offset.x() << "\n";
            out << "y=" << y + offset.y() << "\n";
            out << "diameter=5\n";
            out << "charge=0\n";
            out << "spin=0\n";
            out << "potential=0\n";
            out << "[#TYPE:CELL_DOT]\n";
        }

        if (!name.isEmpty())
        {
            out << "[TYPE:QCADLabel]\n";
            out << "[TYPE:QCADStretchyObject]\n";
            out << "[TYPE:QCADDesignObject]\n";
            out << "x=" << x << "\n";
            out << "y=" << y - 20 << "\n";
            out << "bSelected=FALSE\n";
            writeCellColor(function, phase);
            out << "bounding_box.xWorld=" << x - 10 << "\n";
            out << "bounding_box.yWorld=" << y - 31 << "\n";
            out << "bounding_box.cxWorld=20\n";
            out << "bounding_box.cyWorld=22\n";
            out << "[#TYPE:QCADDesignObject]\n";
            out << "[#TYPE:QCADStretchyObject]\n";
            out << "psz=" << name << "\n";
            out << "[#TYPE:QCADLabel]\n";
        }

        out << "[#TYPE:QCADCell]\n";
    };
    int layer_n = layers.size();
    for(int i = 0; i < layer_n; ++i)
    {
        out << "[TYPE:QCADLayer]\n";
        out << "type=1\n";
        out << "status=0\n";
        out << "pszDescription=" << layerComboBox->GetItem(i)->text() << "\n";

        if (useFastCells && i < fastCellsByLayer.size())
        {
            for (const auto &cell : fastCellsByLayer[i])
            {
                FCNCellFunction function = FCNCellFunction::NORMAL;
                QCACellMode mode = QCACellMode::NORMAL;
                switch (cell.type) {
                    case CellType::InputCell:
                        function = FCNCellFunction::INPUT;
                        break;
                    case CellType::OutputCell:
                        function = FCNCellFunction::OUTPUT;
                        break;
                    case CellType::FixedCell_0:
                    case CellType::FixedCell_1:
                        function = FCNCellFunction::FIXED;
                        break;
                    case CellType::VerticalCell:
                        mode = QCACellMode::VERTICAL;
                        break;
                    case CellType::CrossoverCell:
                        mode = QCACellMode::CROSSOVER;
                        break;
                    default:
                        break;
                }
                const QString name = (function == FCNCellFunction::FIXED)
                    ? (cell.type == CellType::FixedCell_0 ? QStringLiteral("-1.00") : QStringLiteral("1.00"))
                    : cell.name;
                writeCellData(cell.x, cell.y, 18.0, 18.0, function, mode, cell.phase, name);
            }
        }
        else
        {
            auto itemGroup = layers[i];
            for(QGraphicsItem *item: itemGroup)
            {
                const QCADCellItem *cell = static_cast<QCADCellItem *>(item);
                writeCellData(
                    simon::x(*cell),
                    simon::y(*cell),
                    simon::width(*cell),
                    simon::height(*cell),
                    simon::function(*cell),
                    simon::cellMode(*cell),
                    simon::timezone(*cell),
                    QString::fromStdString(simon::name(*cell))
                );
            }
        }

        out << "[#TYPE:QCADLayer]\n";
    }
    //添加 bus layout

    out << "[#TYPE:DESIGN]\n";


    /*********文本保存结束********/

    file.close();
    setCurrentFile(fileName);
    statusBar()->showMessage(tr("文本保存成功"), 2000);
    return true;
}

void MainWindow::closeEvent(QCloseEvent *event) {
    // 调用保存文件的操作
    if (maybeSave()) {
        QSettings settings;
        // 保存窗口的设置
        settings.setValue(kMostRecentFile, windowFilePath());

        event->accept();  // 允许窗口关闭
    } else {
        event->ignore();  // 如果取消了保存，则忽略窗口关闭事件
    }
}
bool MainWindow::maybeSave()
{
    if(!isWindowModified())
        return true; 
    const QMessageBox::StandardButton ret 
        = QMessageBox::warning(this, tr("警告！"), tr("文档有更改\n""是否保存？"),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    switch (ret) {
        case QMessageBox::Save :
            return slotSave();
        case QMessageBox::Cancel :
            return false;
        default :
            break;
    }
    return true;
}

void MainWindow::setCurrentFile(const QString &fileName)
{
    curFile = fileName;
    setDirty(false);

    QString shownName = curFile;
    if(curFile.isEmpty())
        shownName = "Unnamed";
    //setWindowFilePath(shownName);
    setWindowTitle(tr("%1[*] - iFCN").arg(shownName));
}

QString MainWindow::defaultQcaSavePath() const
{
    if (curFile.isEmpty() || curFile == tr("Unnamed")) {
        return ".";
    }

    const QFileInfo info(curFile);
    if (info.suffix().compare(QStringLiteral("ifcn"), Qt::CaseInsensitive) == 0) {
        return info.dir().filePath(info.completeBaseName() + ".qca");
    }

    return curFile;
}

void MainWindow::slotNew()
{
    if (tabHost) {
        tabHost->openNewTab();
        return;
    }
    MainWindow *newMainWindow = new MainWindow;
    newMainWindow->show();
}

void MainWindow::slotOpen()
{
    QString fileName = QFileDialog::getOpenFileName(this, tr("打开文件"), "/home/lys/projects/github/iFCN/",
                                                    tr("QCA/iFCN files (*.qca *.ifcn);;All file (*)"));
    if (fileName.isEmpty()) {
        return;
    }

    const QString suffix = QFileInfo(fileName).suffix().toLower();
    const bool isIfcn = (suffix == "ifcn");

    if (tabHost) {
        tabHost->openFileInNewTab(fileName);
        return;
    }

    if ((layers.size() == 0) || (layers.size() == 1 && layers[0].isEmpty())) {
        loadFile(fileName);
        return;
    }

    MainWindow *newMainWindow = new MainWindow;
    newMainWindow->show();
    newMainWindow->loadFile(fileName);
}

bool MainWindow::slotSave()
{
    const QString suffix = QFileInfo(curFile).suffix().toLower();
    if(curFile.isEmpty() || curFile == tr("Unnamed") || suffix == "ifcn")
        return slotSaveAs();
    else
        return saveFile(curFile);
}

bool MainWindow::slotSaveAs()
{
    QString fileName = QFileDialog::getSaveFileName(this, tr("文件另存为"), defaultQcaSavePath(), tr("QCA files (*.qca);;All file (*)"));
    if(fileName.isEmpty())
        return false;
    if(!fileName.toLower().endsWith(".qca"))
        fileName += ".qca";
    
    emit savedname(fileName);
    qDebug() << "fileName:" << fileName;
    // simfileName = fileName;//for 仿真文件名
    return saveFile(fileName);
}
        
void MainWindow::slotAddLayer()
{
    layerComboBox->AddItem(tr("New Layer"), true); 
    // int idx = layerComboBox->GetNumRows() - 1;

    QVector<QGraphicsItem*> cells_layer;

    layers.push_back(cells_layer); 

    // for (QGraphicsItem* cellItem : layers[idx]){
    //     if(cellItem){

    //     }
    //     cellItem->setZValue(idx);
    //     cellItem->setVisible(true);
    //     scene->addItem(cellItem);
    // }
    //qDebug() << tr("layer:") << idx << tr(" zValue:") << layers[idx]->zValue();
}

void MainWindow::slotAddLayer(std::string layerName)
{
    layerComboBox->AddItem(QString::fromStdString(layerName), true); 
    QVector<QGraphicsItem*> cells_layer;
    layers.push_back(cells_layer); 
    setDirty(true);
}



void MainWindow::slotDeleteLayer()
{
    int idx = layerComboBox->currentIndex();
    if(idx == -1)
    {
        QMessageBox::information(this, tr("Information消息框"), tr("There have no any cell layers!"));
        return;
    }
    layerComboBox->RemoveItem(idx); 
    
    // QGraphicsItemGroup *itemGroup = layers[idx];
    // auto cells_layer = layers[idx];
    // QList<QGraphicsItem *> items = itemGroup->childItems();
    for(QGraphicsItem *item: layers[idx])
    {
        // itemGroup->removeFromGroup(item);
        scene->removeItem(item);
        delete item;
    }
    
    // scene->destroyItemGroup(itemGroup);
    layers.remove(idx);    

    updateLayerAndCellZValue();
    
}

void MainWindow::slotCaptureFullWindow()
{
    // 创建 QPixmap，大小与窗口相同
    QPixmap screenshot(this->size());

    // 使用 QPainter 将窗口内容渲染到 QPixmap
    QPainter painter(&screenshot);
    this->render(&painter);

    // 保存截图
    QString fileName = QFileDialog::getSaveFileName(this, tr("Save Screenshot"),
                                                    "."+ QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + ".png",
                                                    tr("Images (*.png *.xpm *.jpg *.pdf)"));
    if (!fileName.isEmpty()) {
        screenshot.save(fileName);
        QString message = "The full screen has been captured and the file is saved in " + fileName +" ;";
        printToStatusBar(message);
    }
}

void MainWindow::onSimulationFinished(const QString &outputFileName) {
        // 在主线程中更新UI
    WaveformWindow* waveWindow = new WaveformWindow(nullptr, outputFileName);
    waveWindow->show();
    // QMessageBox::information(this, tr("Simulation Complete"), tr("The simulation result has been saved to %1").arg(resultFile));
}
