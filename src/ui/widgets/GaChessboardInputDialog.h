#ifndef GACHESSBOARDINPUTDIALOG_H
#define GACHESSBOARDINPUTDIALOG_H

#include <QDialog>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>

class GaChessboardInputDialog : public QDialog {
    Q_OBJECT

public:
    explicit GaChessboardInputDialog(QWidget* parent = nullptr);

    QString getClockScheme() const;
    int getWidth() const;
    int getHeight() const;
    int getGeneration() const;
    int getPopulation() const;

private slots:
    void selectSchemeButton(); 

private:
    QPushButton* clockSchemeButtons[3]; 
    QString selectedScheme;  

    QLineEdit* widthInput;
    QLineEdit* heightInput;
    QLineEdit* generationInput;
    QLineEdit* populationInput;

    void updateSelectedButton(QPushButton* selectedButton);
};

#endif 
