#include "ui/mainwindow/MainWindow.h"
#include <QCoreApplication>

void MainWindow::printToStatusBar(const QString &message)
{
    customStatusBar->addMessage(message);
    QCoreApplication::processEvents();
}
void MainWindow::setInputNames(const QVector<QString> &names)
{
    inputname = names;
    emit savedinputname(inputname);
}

QString MainWindow::currentFilePath() const
{
    return curFile;
}

void MainWindow::setDirty(bool on)
{
    if (isBatchUpdating) {
        if (on) {
            batchDirtyPending = true;
        }
        return;
    }
    //禁止其他页面响应
    setWindowModified(on);
    updateUi();
}

void MainWindow::updateUi()
{
    saveAction->setEnabled(isWindowModified());
    //更新Action状态
}
void MainWindow::toggleStatusBar(bool checked)
{
    if (checked) {
        customStatusBar->show();
    } else {
        customStatusBar->hide();
    }
}
