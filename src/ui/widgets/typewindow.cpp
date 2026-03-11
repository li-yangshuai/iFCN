#include "typewindow.h"
#include <QVBoxLayout>
#include <QToolBar>
#include <QHeaderView>
#include <QStandardItem>
#include <QStyledItemDelegate>
#include <QCheckBox>
#include <QSpinBox>

// 禁止编辑的委托类
class NoEditDelegate : public QStyledItemDelegate
{
public:
    NoEditDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

    QWidget *createEditor(QWidget *, const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return nullptr;  // 禁止双击编辑
    }
};

Typewindow::Typewindow(QWidget *parent, const QVector<QString>& lablename)
    : QMainWindow(parent)
{
    this->resize(560, 160);
    // 初始化中央窗口部件
    createCentralWidget();
    this->inputname = lablename;
    // 设置表头
    model->setHorizontalHeaderLabels({"LableName", "Active"});
    model->setRowCount(lablename.size());

    for (int i = 0; i < lablename.size(); ++i) {
        model->setItem(i, 0, new QStandardItem(lablename[i]));
        auto *item = new QStandardItem();
        item->setCheckable(true);
        item->setCheckState(Qt::Checked);
        model->setItem(i, 1, item);
    }

    connect(model, &QStandardItemModel::itemChanged, this, &Typewindow::onItemChanged);

    // 设置禁止编辑的委托
    tableView->setItemDelegate(new NoEditDelegate(this));

    // 设置列宽和居中对齐
    tableView->setColumnWidth(0, 90);  // 设置LableName列宽度
    tableView->setColumnWidth(1, 50);   // 设置Active列宽度
    tableView->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);

    // 初始化工具栏
    createToolBar();

    // 默认状态
    slotExhaustive();  // 默认按下exhaustive按钮
}

Typewindow::~Typewindow() {}

void Typewindow::createCentralWidget()
{
    model = new QStandardItemModel(this);
    tableView = new QTableView(this);
    tableView->setModel(model);
    tableView->setSelectionBehavior(QTableView::SelectColumns);
    tableView->horizontalHeader()->setStretchLastSection(true);

    // 设置列宽和对齐方式
    tableView->horizontalHeader()->setDefaultSectionSize(30);
    tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);

    setCentralWidget(tableView);
}

void Typewindow::createToolBar()
{
    QToolBar *toolBar = addToolBar("Tool Bar");
    
    openAction = new QAction(tr("Open"), this);
    saveAction = new QAction(tr("Save"), this);
    insertbeforAction = new QAction("Insert Before", this);
    insertafterAction = new QAction("Insert After", this);
    deleteAction = new QAction("Delete", this);

    connect(openAction, SIGNAL(triggered()), this, SLOT(slotOpenFile()));
    connect(saveAction, SIGNAL(triggered()), this, SLOT(slotSaveFile()));
    connect(insertbeforAction, &QAction::triggered, this, &Typewindow::slotInsertBefore);
    connect(insertafterAction, &QAction::triggered, this, &Typewindow::slotInsertAfter);
    connect(deleteAction, &QAction::triggered, this, &Typewindow::slotDeleteColumn);

    exhaustiveAction = new QAction("Exhaustive", this);
    exhaustiveAction->setCheckable(true);
    exhaustiveAction->setChecked(true);  // 默认按下

    vectortableAction = new QAction("Vector Table", this);
    vectortableAction->setCheckable(true);

    connect(exhaustiveAction, &QAction::triggered, this, &Typewindow::slotExhaustive);
    connect(vectortableAction, &QAction::triggered, this, &Typewindow::slotVectortable);

    toolBar->addAction(openAction);
    toolBar->addAction(saveAction);
    toolBar->addAction(insertbeforAction);
    toolBar->addAction(insertafterAction);
    toolBar->addAction(deleteAction);
    toolBar->addAction(exhaustiveAction);
    toolBar->addAction(vectortableAction);
}
void Typewindow::loadDataFromFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        qWarning() << "Cannot open file for reading:" << file.errorString();
        return;
    }

    QTextStream in(&file);
    QString line;
    QStringList headerLabels;
    //model->clear();
    // 读取并跳过第一行（列标签）
    if (!in.atEnd()) {
        line = in.readLine();
        headerLabels = line.split(", ");
    }

    // 将 QVector<QString> 转换为 QStringList 进行比较
   QStringList expectedLabels = QStringList::fromVector(inputname);


    // 检查文件头与预期标签是否一致
    if (headerLabels != expectedLabels) {
        qWarning() << "File header does not match expected labels.";
        return;
    }

    // 逐行读取并设置到模型中
    int col = 0;
    while (!in.atEnd()) {
        line = in.readLine();
        QStringList values = line.split(", ");
        
        for (int row = 0; row < values.size(); ++row) {
            bool isChecked = (values[row].trimmed() == "1");
            QStandardItem *item = new QStandardItem();
            item->setCheckState(isChecked ? Qt::Checked : Qt::Unchecked);
            model->setItem(row, col+2,item);
        }
        col++;
    }

    file.close();
}

void Typewindow::slotOpenFile()
{
    QString filePath = QFileDialog::getOpenFileName(this, tr("Open File"), "", tr("Text Files (*.vt)"));
    loadDataFromFile(filePath);
}

void Typewindow::slotSaveFile()
{
    QString fileName = QFileDialog::getSaveFileName(this, tr("保存文件"), "", tr("vt 文件 (*.vt);;所有文件 (*.*)"));

    if (!fileName.isEmpty()) {
        if (!saveFile(fileName)) {
            QMessageBox::warning(this, tr("保存失败"), tr("文件保存失败，请重试。"));
        } else {
            QMessageBox::information(this, tr("保存成功"), tr("文件已成功保存。"));
        }
    }
}


void Typewindow::slotInsertBefore()
{
    int col = tableView->currentIndex().column();
    if (col > 1) {
        model->insertColumn(col);
        for (int i = 0; i < model->rowCount(); ++i) {
            auto *item = new QStandardItem();
            item->setCheckable(true);
            item->setCheckState(Qt::Unchecked);
            model->setItem(i, col, item);

            // 检查Active列的状态，并设置新列的样式
            if (model->item(i, 1)->checkState() == Qt::Unchecked) {
                item->setBackground(Qt::lightGray);
                item->setEnabled(false);
            }
        }
        updateHeaderLabels();
    }
}

void Typewindow::slotInsertAfter()
{
    int col = tableView->currentIndex().column();
    if (col >= 1) {
        model->insertColumn(col + 1);
        for (int i = 0; i < model->rowCount(); ++i) {
            auto *item = new QStandardItem();
            item->setCheckable(true);
            item->setCheckState(Qt::Unchecked);
            model->setItem(i, col + 1, item);

            // 检查Active列的状态，并设置新列的样式
            if (model->item(i, 1)->checkState() == Qt::Unchecked) {
                item->setBackground(Qt::lightGray);
                item->setEnabled(false);
            }
        }
        updateHeaderLabels();
    }
}

void Typewindow::slotDeleteColumn()
{
    int col = tableView->currentIndex().column();
    if (col > 1) {
        model->removeColumn(col);
        updateHeaderLabels();
    }
}

void Typewindow::updateHeaderLabels()
{
    for (int i = 2; i < model->columnCount(); ++i) {
        model->setHeaderData(i, Qt::Horizontal, QString::number(i - 1));
    }
}

bool Typewindow::saveFile(const QString &fileName)
{
    QFile file(fileName);
    if (!file.open(QFile::WriteOnly | QFile::Text)) {
        QMessageBox::warning(this, tr("警告！"), tr("不能写入文件 %1:\n%2.")
                             .arg(QDir::toNativeSeparators(fileName), file.errorString()));
        return false;
    }

    QTextStream out(&file);

    // 获取总列数（包括LableName、Active以及其他插入的列）
    int columnCount = model->columnCount();

    QStringList inputlable = QStringList::fromVector(this->inputname);
    out << inputlable.join(", ") << "\n";

    // 写入每行的数据
    for (int col = 2; col < columnCount; ++col)
    {
        QStringList colValues;
        for (int row = 0; row < model->rowCount(); ++row) 
        {
            QStandardItem *item = model->item(row, col);
            if (item && item->isCheckable()) {
                colValues << (item->checkState() == Qt::Checked ? "1" : "0");
            } else {
                colValues << "0"; // 如果不是Checkable的项，也可以视作未选中
            }
        }
        out << colValues.join(", ") << "\n";
    }

    file.close();
    emit sendvtnames(fileName);
    return true;
}

// 检查CheckBox状态并更新行的状态
void Typewindow::onItemChanged(QStandardItem *item)
{
    if (item->column() == 1) {  // 监听Active列
        int row = item->row();
        bool isActive = (item->checkState() == Qt::Checked);

        QColor bgColor = isActive ? QColor(Qt::white) : QColor(Qt::lightGray);

        // 更新这一行的背景颜色
        for (int col = 0; col < model->columnCount(); ++col) {
            model->item(row, col)->setBackground(bgColor);

            // 使CheckBox可用或不可用
            if (col > 1) {  // 跳过LableName和Active列
                model->item(row, col)->setEnabled(isActive);
            }
        }
    }
}

// exhaustive按钮被按下时，centralWidget不可见
void Typewindow::slotExhaustive()
{
    exhaustiveAction->setChecked(true);
    vectortableAction->setChecked(false);
    centralWidget()->setVisible(false);
}

// vectortable按钮被按下时，centralWidget可见
void Typewindow::slotVectortable()
{
    vectortableAction->setChecked(true);
    exhaustiveAction->setChecked(false);
    centralWidget()->setVisible(true);
}

