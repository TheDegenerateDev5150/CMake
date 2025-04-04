/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file LICENSE.rst or https://cmake.org/licensing for details.  */

#define QT_DEPRECATED_WARNINGS_SINCE QT_VERSION_CHECK(5, 14, 0)

#include "QCMakeWidgets.h"

#include <utility>

#include <QFileDialog>
#include <QFileInfo>
#include <QResizeEvent>
#include <QSortFilterProxyModel>
#include <QToolButton>

#if (QT_VERSION >= QT_VERSION_CHECK(5, 15, 0))
#  include <QRegularExpression>
#endif

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
#  include <QFileSystemModel>
#else
#  include <QDirModel>
#endif

QCMakeFileEditor::QCMakeFileEditor(QWidget* p, QString var)
  : QLineEdit(p)
  , Variable(std::move(var))
{
  this->ToolButton = new QToolButton(this);
  this->ToolButton->setText("...");
  this->ToolButton->setCursor(QCursor(Qt::ArrowCursor));
  QObject::connect(this->ToolButton, &QAbstractButton::clicked, this,
                   &QCMakeFileEditor::chooseFile);
}

QCMakeFilePathEditor::QCMakeFilePathEditor(QWidget* p, QString const& var)
  : QCMakeFileEditor(p, var)
{
  this->setCompleter(new QCMakeFileCompleter(this, false));
}

QCMakePathEditor::QCMakePathEditor(QWidget* p, QString const& var)
  : QCMakeFileEditor(p, var)
{
  this->setCompleter(new QCMakeFileCompleter(this, true));
}

void QCMakeFileEditor::resizeEvent(QResizeEvent* e)
{
  // make the tool button fit on the right side
  int h = e->size().height();
  // move the line edit to make room for the tool button
  this->setContentsMargins(0, 0, h, 0);
  // put the tool button in its place
  this->ToolButton->resize(h, h);
  this->ToolButton->move(this->width() - h, 0);
}

void QCMakeFilePathEditor::chooseFile()
{
  // choose a file and set it
  QString path;
  QFileInfo info(this->text());
  QString title;
  if (this->Variable.isEmpty()) {
    title = tr("Select File");
  } else {
    title = tr("Select File for %1");
    title = title.arg(this->Variable);
  }
  emit this->fileDialogExists(true);
  path =
    QFileDialog::getOpenFileName(this, title, info.absolutePath(), QString(),
                                 nullptr, QFileDialog::DontResolveSymlinks);
  emit this->fileDialogExists(false);

  if (!path.isEmpty()) {
    this->setText(QDir::fromNativeSeparators(path));
  }
}

void QCMakePathEditor::chooseFile()
{
  // choose a file and set it
  QString path;
  QString title;
  if (this->Variable.isEmpty()) {
    title = tr("Select Path");
  } else {
    title = tr("Select Path for %1");
    title = title.arg(this->Variable);
  }
  emit this->fileDialogExists(true);
  path = QFileDialog::getExistingDirectory(this, title, this->text(),
                                           QFileDialog::ShowDirsOnly |
                                             QFileDialog::DontResolveSymlinks);
  emit this->fileDialogExists(false);
  if (!path.isEmpty()) {
    this->setText(QDir::fromNativeSeparators(path));
  }
}

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
// use same QFileSystemModel for all completers
static QFileSystemModel* fileDirModel()

{
  static QFileSystemModel* m = nullptr;
  if (!m) {
    m = new QFileSystemModel();
  }
  return m;
}
static QFileSystemModel* pathDirModel()
{
  static QFileSystemModel* m = nullptr;
  if (!m) {
    m = new QFileSystemModel();
    m->setFilter(QDir::AllDirs | QDir::Drives | QDir::NoDotAndDotDot);
  }
  return m;
}
#else
// use same QDirModel for all completers
static QDirModel* fileDirModel()

{
  static QDirModel* m = nullptr;
  if (!m) {
    m = new QDirModel();
  }
  return m;
}
static QDirModel* pathDirModel()
{
  static QDirModel* m = nullptr;
  if (!m) {
    m = new QDirModel();
    m->setFilter(QDir::AllDirs | QDir::Drives | QDir::NoDotAndDotDot);
  }
  return m;
}
#endif

QCMakeFileCompleter::QCMakeFileCompleter(QObject* o, bool dirs)
  : QCompleter(o)
{
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
  QFileSystemModel* m = dirs ? pathDirModel() : fileDirModel();
  this->setModel(m);
  m->setRootPath(QString());
#else
  QDirModel* m = dirs ? pathDirModel() : fileDirModel();
  this->setModel(m);
#endif
}

QString QCMakeFileCompleter::pathFromIndex(QModelIndex const& idx) const
{
  return QDir::fromNativeSeparators(QCompleter::pathFromIndex(idx));
}

namespace QtCMake {
bool setSearchFilter(QSortFilterProxyModel* model, QString const& searchString)
{
#if (QT_VERSION >= QT_VERSION_CHECK(5, 15, 0))
  QRegularExpression const regex(searchString,
                                 QRegularExpression::CaseInsensitiveOption |
                                   QRegularExpression::DontCaptureOption);
  if (regex.isValid()) {
    model->setFilterRegularExpression(regex);
    return true;
  }
  model->setFilterFixedString(QString{});
  return false;
#else
  model->setFilterFixedString(searchString);
  return true;
#endif
}

void setSearchFilterColor(QLineEdit* edit, bool valid)
{
  QPalette palette;
  if (!valid) {
    palette.setColor(QPalette::Base, Qt::red);
  }
  edit->setPalette(palette);
}
}
