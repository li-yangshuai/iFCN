#include "GaChessboardInputDialog.h"
#include <QPixmap>
#include <QButtonGroup>
#include <QIcon>
#include <QStyle>

GaChessboardInputDialog::GaChessboardInputDialog(QWidget* parent) : QDialog(parent), selectedScheme("TDD") {
    
    setWindowTitle("Generic Algorithm: Parameters set");
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    QHBoxLayout* imageLayout = new QHBoxLayout();
    mainLayout->addLayout(imageLayout);

    //添加时钟方案的图片
    QStringList schemes = {"2DDwave", "USE", "RES"};
    QStringList imagePaths = {":/2dd.svg", ":/use.svg", ":/res.svg"}; 
    for (int i = 0; i < 3; ++i) {

        QVBoxLayout* imageButtonLayout = new QVBoxLayout();
        QLabel* label = new QLabel(schemes[i], this);
        label->setAlignment(Qt::AlignCenter); 
        imageButtonLayout->addWidget(label);

        // Create the image button
        clockSchemeButtons[i] = new QPushButton(this);
        clockSchemeButtons[i]->setIcon(QIcon(imagePaths[i]));
        clockSchemeButtons[i]->setIconSize(QSize(100, 100)); 
        clockSchemeButtons[i]->setCheckable(true); 
        clockSchemeButtons[i]->setStyleSheet("border: 2px solid lightgray;"); 
        clockSchemeButtons[i]->setProperty("scheme", schemes[i]); 
        imageButtonLayout->addWidget(clockSchemeButtons[i]);

        connect(clockSchemeButtons[i], &QPushButton::clicked, this, &GaChessboardInputDialog::selectSchemeButton);

        imageLayout->addLayout(imageButtonLayout);
    }


    clockSchemeButtons[1]->setChecked(true);
    clockSchemeButtons[1]->setStyleSheet("border: 2px solid blue;");
    selectedScheme = "USE";  // Set the default selected scheme to "USE"

    QLabel* notation = new QLabel("Set the size of the layout space :", this);
    mainLayout->addWidget(notation);


    QHBoxLayout* dimensionLayout = new QHBoxLayout();

    QLabel* widthLabel = new QLabel("Width:", this);
    dimensionLayout->addWidget(widthLabel);

    widthInput = new QLineEdit(this);
    widthInput->setText("20"); 
    dimensionLayout->addWidget(widthInput);

    QLabel* heightLabel = new QLabel("Height:", this);
    dimensionLayout->addWidget(heightLabel);

    heightInput = new QLineEdit(this);
    heightInput->setText("20"); 
    dimensionLayout->addWidget(heightInput);

    mainLayout->addLayout(dimensionLayout);

    QLabel* instructionLabel = new QLabel("If you want better results, increase below parameters :", this);
    mainLayout->addWidget(instructionLabel);

    QHBoxLayout* genPopLayout = new QHBoxLayout();

    QLabel* generationLabel = new QLabel("Generation:", this);
    genPopLayout->addWidget(generationLabel);

    generationInput = new QLineEdit(this);
    generationInput->setText("100"); 
    genPopLayout->addWidget(generationInput);

    QLabel* populationLabel = new QLabel("Population:", this);
    genPopLayout->addWidget(populationLabel);

    populationInput = new QLineEdit(this);
    populationInput->setText("100"); 
    genPopLayout->addWidget(populationInput);

    mainLayout->addLayout(genPopLayout); 

    // ok 
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setAlignment(Qt::AlignRight); 

    QPushButton* okButton = new QPushButton("Yes", this);
    QPushButton* cancelButton = new QPushButton("No", this);
    buttonLayout->addWidget(okButton);
    buttonLayout->addWidget(cancelButton);
    mainLayout->addLayout(buttonLayout); 

    connect(okButton, &QPushButton::clicked, this, &GaChessboardInputDialog::accept);
    connect(cancelButton, &QPushButton::clicked, this, &GaChessboardInputDialog::reject);
}

void GaChessboardInputDialog::selectSchemeButton() {

    QPushButton* clickedButton = qobject_cast<QPushButton*>(sender());
    if (clickedButton) {
        selectedScheme = clickedButton->property("scheme").toString();
        updateSelectedButton(clickedButton);
    }
}

void GaChessboardInputDialog::updateSelectedButton(QPushButton* selectedButton) {
    for (int i = 0; i < 3; ++i) {
        if (clockSchemeButtons[i] == selectedButton) {
            clockSchemeButtons[i]->setStyleSheet("border: 2px solid blue;");
            clockSchemeButtons[i]->setChecked(true);
        } else {
            clockSchemeButtons[i]->setStyleSheet("border: 2px solid lightgray;");
            clockSchemeButtons[i]->setChecked(false);
        }
    }
}

QString GaChessboardInputDialog::getClockScheme() const {
    return selectedScheme;
}

int GaChessboardInputDialog::getWidth() const {
    return widthInput->text().toInt();
}

int GaChessboardInputDialog::getHeight() const {
    return heightInput->text().toInt();
}

int GaChessboardInputDialog::getGeneration() const {
    return generationInput->text().toInt();
}

int GaChessboardInputDialog::getPopulation() const {
    return populationInput->text().toInt();
}
