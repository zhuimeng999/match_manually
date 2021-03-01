//
// Created by lucius on 2/7/21.
//

#include <QLabel>
#include <QLineEdit>
#include <QFileInfo>
#include <QGridLayout>
#include <QPushButton>
#include <QFileDialog>
#include "LoadProjectDialog.h"

LoadProjectDialog::LoadProjectDialog() {
  fileNameLabel = new QLabel(tr("image path:"));
  imagePathEdit = new QLineEdit();
  imagePathButton = new QPushButton("...");
  connect(imagePathButton, &QPushButton::clicked, this, &LoadProjectDialog::getImageDirectoryPath);

  pathLabel = new QLabel(tr("colmap workspacce:"));
  ColmapPathEdit = new QLineEdit();
  colmapPathButton = new QPushButton("...");
  connect(colmapPathButton, &QPushButton::clicked, this, &LoadProjectDialog::getColmapDirectoryPath);

  okButton = new QPushButton("Ok");
  connect(okButton, &QPushButton::clicked, this, &LoadProjectDialog::setResultOk);
  cancelButton = new QPushButton("Cancel");
  connect(cancelButton, &QPushButton::clicked, this, &LoadProjectDialog::reject);

  QGridLayout *mainLayout = new QGridLayout;
  mainLayout->addWidget(fileNameLabel, 0, 0);
  mainLayout->addWidget(imagePathEdit, 0, 1);
  mainLayout->addWidget(imagePathButton, 0, 2);
  mainLayout->addWidget(pathLabel, 1, 0);
  mainLayout->addWidget(ColmapPathEdit, 1, 1);
  mainLayout->addWidget(colmapPathButton, 1, 2);
  mainLayout->addWidget(cancelButton, 2, 0);
  mainLayout->addWidget(okButton, 2, 2);
  setLayout(mainLayout);
}

void LoadProjectDialog::getImageDirectoryPath() {
  auto dirPath = QFileDialog::getExistingDirectory(this, "open images directory");
  imagePathEdit->setText(dirPath);
}

void LoadProjectDialog::getColmapDirectoryPath() {
  auto dirPath = QFileDialog::getExistingDirectory(this, "open colmap directory");
  ColmapPathEdit->setText(dirPath);
}

void LoadProjectDialog::setResultOk(){
  if(imagePathEdit->text().isEmpty() or ColmapPathEdit->text().isEmpty()){
    return;
  }
  accept();
}

QString LoadProjectDialog::getImagePath(){
  return imagePathEdit->text();
}

QString LoadProjectDialog::getColmapPath(){
  return ColmapPathEdit->text();
}
