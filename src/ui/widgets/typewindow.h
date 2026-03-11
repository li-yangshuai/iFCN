#ifndef TYPEWINDOW_H
#define TYPEWINDOW_H
#include "typewindow.h"
#include <QMainWindow>
#include <QVector>
#include <QTableView>
#include <QStandardItemModel>
#include <QAction>
#include <QFileDialog>
#include <QMessageBox>
#include <QTextStream>
#include <QFile>
#include <QDebug>
#include <QStringList>

class Typewindow : public QMainWindow
{
     Q_OBJECT
public:
    Typewindow(QWidget *parent, const QVector<QString>& lablename);
    ~Typewindow();

private:
    void createToolBar();
    void createCentralWidget();

    void updateHeaderLabels();

    bool saveFile(const QString &fileName);    //保存.vt文件

    QTableView *tableView;
    QStandardItemModel *model;

    QAction *openAction;
    QAction *saveAction;
    QAction *insertbeforAction;
    QAction *insertafterAction;
    QAction *deleteAction;
    QAction *exhaustiveAction;
    QAction *vectortableAction;

    // 新增的指针用于备份 centralWidget
    QWidget *centralWidgetBackup;

signals:
    void sendvtnames(const QString &fileName);


private slots:
    void loadDataFromFile(const QString &filePath);
    void slotOpenFile();
    void slotSaveFile();        
    void slotInsertBefore();
    void slotInsertAfter();
    void slotDeleteColumn();
    void slotExhaustive();
    void slotVectortable();
    void onItemChanged(QStandardItem *item);

private:
    QVector<QString> inputname;

};

#endif // TYPEWINDOW_H
