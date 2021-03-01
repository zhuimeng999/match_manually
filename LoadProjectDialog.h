//
// Created by lucius on 2/7/21.
//

#ifndef MATCH_MANUALLY_LOADPROJECTDIALOG_H
#define MATCH_MANUALLY_LOADPROJECTDIALOG_H

#include <QDialog>

QT_BEGIN_NAMESPACE
class QLabel;

class QLineEdit;

QT_END_NAMESPACE

class LoadProjectDialog : public QDialog {
Q_OBJECT
public:
  LoadProjectDialog();
  QString getImagePath();
  QString getColmapPath();

private:
  void getImageDirectoryPath();
  void getColmapDirectoryPath();
  void setResultOk();

  QLabel *fileNameLabel;
  QLineEdit *imagePathEdit;
  QPushButton *imagePathButton;

  QLabel *pathLabel;
  QLineEdit *ColmapPathEdit;
  QPushButton *colmapPathButton;

  QPushButton *okButton;
  QPushButton *cancelButton;
};


#endif //MATCH_MANUALLY_LOADPROJECTDIALOG_H
