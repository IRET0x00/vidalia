/****************************************************************
 *  $Id$
 * 
 *  Vidalia is distributed under the following license:
 *
 *  Copyright (C) 2006,  Matt Edman, Justin Hipple
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, 
 *  Boston, MA  02110-1301, USA.
 ****************************************************************/

#include <QMessageBox>
#include <QInputDialog>

#include "../mainwindow.h"
#include "messagelog.h"

#define COL_TIME  0 /** Date/time column */
#define COL_TYPE  1 /** Message severity type column */
#define COL_MSG   2 /** Message body column */
#define ROLE_TYPE 1 /** Role used to store the numeric type */

/** Defines the format used for displaying the date and time of a log message */
#define DATETIME_FMT  "MMM dd hh:mm:ss:zzz"
      
/** Constructor. The constructor will load the message log's settings from
 * VidaliSettings and register for log events according to the most recently
 * set severity filter. 
 * \param torControl A TorControl object used to register for log events.
 * \param parent The parent widget of this MessageLog object.
 * \param flags Any desired window creation flags. 
 */
MessageLog::MessageLog(TorControl *torControl, QWidget *parent, Qt::WFlags flags)
: QMainWindow(parent, flags)
{
  /* Invoke Qt Designer generated QObject setup routine */
  ui.setupUi(this);

  /* Create necessary Message Log QObjects */
  _torControl = torControl;
  _settings = new VidaliaSettings();
 
  /* Bind events to actions */
  createActions();

  /* Set tooltips for necessary widgets */
  setToolTips();
  
  /* Initialize message counters */
  _messagesShown = 0;
  _maxCount = _settings->getMaxMsgCount();

  /* Load the message log's stored settings */
  _logFile = 0;
  if (_settings->isLogFileEnabled()) {
    openLogFile(_settings->getLogFile());
  }

  /* Ask Tor to give me some log events */
  registerLogEvents();

  /* Show number of message displayed in Status bar */
  ui.lstMessages->setStatusTip(tr("Messages Shown: ") += "0");

  /* Hide Message Log Settings frame */
  showSettingsFrame(false);

  /* Turn off opacity group on unsupported platforms */
#if defined(Q_WS_WIN)
  if(!(QSysInfo::WV_2000 <= QSysInfo::WindowsVersion <= QSysInfo::WV_2003)) {
    ui.grpOpacity->setVisible(false);
  }
#endif
  
#if defined(Q_WS_X11)
  ui.grpOpacity->setVisible(false);
#endif
}

/** Default Destructor. Simply frees up any memory allocated for member
 * variables. */
MessageLog::~MessageLog()
{
  if (_settings) {
    delete _settings;
  }
}

/** Binds events (signals) to actions (slots). */
void
MessageLog::createActions()
{
  connect(ui.actionSave_Selected, SIGNAL(triggered()), 
      this, SLOT(saveSelected()));
  
  connect(ui.actionSave_All, SIGNAL(triggered()), 
      this, SLOT(saveAll()));
  
  connect(ui.actionCopy, SIGNAL(triggered()),
      this, SLOT(copy()));

  connect(ui.actionClear, SIGNAL(triggered()),
      this, SLOT(clear()));
  
  connect(ui.actionFind, SIGNAL(triggered()),
      this, SLOT(find()));

  connect(ui.btnSaveSettings, SIGNAL(clicked()),
      this, SLOT(saveChanges()));

  connect(ui.btnCancelSettings, SIGNAL(clicked()),
      this, SLOT(cancelChanges()));

  connect(ui.sldrOpacity, SIGNAL(valueChanged(int)),
      this, SLOT(setOpacity(int)));

  connect(ui.btnToggleSettings, SIGNAL(toggled(bool)),
      this, SLOT(showSettingsFrame(bool)));

  connect(ui.btnBrowse, SIGNAL(clicked()),
      this, SLOT(browse()));
}

/** Set tooltips for Message Filter checkboxes in code because they are long
 * and Designer wouldn't let us insert newlines into the text. */
void
MessageLog::setToolTips()
{
  ui.chkTorErr->setToolTip(tr("Messages that appear when something has \n"
                              "gone very wrong and Tor cannot proceed."));
  ui.chkTorWarn->setToolTip(tr("Messages that only appear when \n"
                               "something has gone wrong with Tor."));
  ui.chkTorNote->setToolTip(tr("Messages that appear infrequently \n"
                               "during normal Tor operation and are \n"
                               "not considered errors, but you may \n"
                               "care about."));
  ui.chkTorInfo->setToolTip(tr("Messages that appear frequently \n"
                               "during normal Tor operation."));
  ui.chkTorDebug->setToolTip(tr("Hyper-verbose messages primarily of \n"
                                "interest to Tor developers.")); 
}

/** Loads the saved Message Log settings, including maximum message count,
 * message log window opacity, and message severity filter. */
void
MessageLog::loadSettings()
{
  /* Set Max Count widget */
  ui.spnbxMaxCount->setValue(_settings->getMaxMsgCount());

  /* Set the window opacity slider widget */
  ui.sldrOpacity->setValue(_settings->getMsgLogOpacity());

  /* Set the window opacity label */
  ui.lblPercentOpacity->setNum(ui.sldrOpacity->value());

  /* Set whether or not logging to file is enabled */
  _enableLogging = _settings->isLogFileEnabled();
  ui.chkEnableLogFile->setChecked(_enableLogging);
  ui.lineFile->setText(_settings->getLogFile());

  /* Set the checkboxes accordingly */
  _filter = _settings->getMsgFilter();
  ui.chkTorErr->setChecked(_filter & LogEvent::TorError);
  ui.chkTorWarn->setChecked(_filter & LogEvent::TorWarn);
  ui.chkTorNote->setChecked(_filter & LogEvent::TorNotice);
  ui.chkTorInfo->setChecked(_filter & LogEvent::TorInfo);
  ui.chkTorDebug->setChecked(_filter & LogEvent::TorDebug);
}

/** Attempts to register the selected message filter with Tor and displays an
 * error if setting the events fails. */
void
MessageLog::registerLogEvents()
{
  QString errmsg;
  _filter = _settings->getMsgFilter();
  if (!_torControl->setLogEvents(_filter, this, &errmsg)) {
    QMessageBox::warning(this, tr("Error Setting Filter"),
      tr("Vidalia was unable to register for Tor's log events.\n\n"
         "Error: ") + errmsg,
       QMessageBox::Ok, QMessageBox::NoButton);
  }
}

/** Saves the Message Log settings, adjusts the message list if required, and
 * then hides the settings frame. */
void
MessageLog::saveChanges()
{
  /* Try to open the log file. If it can't be opened, then give the user an
   * error message and stop saving the changes. */
  if (ui.chkEnableLogFile->isChecked()) {
    //QFileInfo file(ui.lineFile->text());
    if (!openLogFile(ui.lineFile->text())) {
      QMessageBox::warning(this, tr("Error Opening Log File"),
        tr("Vidalia was unable to open the specified log file for writing."),
        QMessageBox::Ok, QMessageBox::NoButton);
      return;
    }
    _settings->setLogFile(ui.lineFile->text());
  }
  _settings->enableLogFile(ui.chkEnableLogFile->isChecked());
  
  
  /* Hide the settings frame and reset toggle button*/
  showSettingsFrame(false);
  
  /* Disable the cursor to prevent problems while refiltering */
  QApplication::setOverrideCursor(Qt::WaitCursor);
  
  int newMax = ui.spnbxMaxCount->value();
  /* If necessary, save new max counter and remove extra messages */
  if (_maxCount != newMax) {
    /* if new max is < number of shown messages then remove some */
    while (newMax < _messagesShown) {
      if (!ui.lstMessages->isItemHidden(ui.lstMessages->topLevelItem(0))) {
        _messagesShown--;
      }
      ui.lstMessages->takeTopLevelItem(0);
    }
    _settings->setMaxMsgCount(newMax);
    _maxCount = newMax;
  }

  /* Save message filter and refilter the list */
  _settings->setMsgFilter(LogEvent::TorError, ui.chkTorErr->isChecked());
  _settings->setMsgFilter(LogEvent::TorWarn, ui.chkTorWarn->isChecked());
  _settings->setMsgFilter(LogEvent::TorNotice, ui.chkTorNote->isChecked());
  _settings->setMsgFilter(LogEvent::TorInfo, ui.chkTorInfo->isChecked());
  _settings->setMsgFilter(LogEvent::TorDebug, ui.chkTorDebug->isChecked());

  /* Refilter the list */
  registerLogEvents();
  filterLog();

  /* Set Message Counter */
  ui.lstMessages->setStatusTip(QString("Messages Shown: %1")
                                  .arg(_messagesShown));
  /* Save Message Log opacity */
  _settings->setMsgLogOpacity(ui.sldrOpacity->value());

  /* Restore the cursor */
  QApplication::restoreOverrideCursor();
}

/** Simply restores the previously saved settings and hides the settings
 * frame. */
void 
MessageLog::cancelChanges()
{
  /* Hide the settings frame and reset toggle button */
  showSettingsFrame(false);

  /* Reload the settings */
  loadSettings();
}

/** Attempts to open the specified log file and sets _logStream to use the
 * opened file. If a log file is previously opened and opening the new log
 * file fails, then the old log file will not be affected.
 * \param filename The name of a file to which log messages will be saved.
 * \return true if the log was successfully opened.
 */
bool
MessageLog::openLogFile(QString filename)
{
  QFile *newLogFile;
  if (_logFile && _logFile->isOpen()) {
    if (_logFile->fileName() == filename) {
      /* The specified log file is already open */
      return true;
    }
  }
  newLogFile = new QFile(filename, this);
  
  /* Try to open the new log file */
  if (!newLogFile->open(QFile::WriteOnly | QIODevice::Append | QIODevice::Text)) {
    delete newLogFile;
    return false;
  }
  
  /* Opening succeeded, so swap out the old logfile for the new one and adjust
   * the QTextStream object's device. */
  delete _logFile;
  _logFile = newLogFile;
  _logStream.setDevice(_logFile);
  
  return true;
}

/** Cycles through the list, hiding and showing appropriate messages.
 * Removes messages if newly shown messages put us over _maxCount. */
void
MessageLog::filterLog()
{
  QTreeWidgetItem* current = new QTreeWidgetItem();
  int currentIndex = ui.lstMessages->topLevelItemCount() - 1;
  bool showCurrent;
  _messagesShown = 0;
  
  while (currentIndex > -1) {
    current = ui.lstMessages->topLevelItem(currentIndex);
    
    /* Keep ALL messages until SHOWING maximum possible */
    if (_messagesShown < _maxCount) {
      
      /* Show or hide message accordingly */
      showCurrent = (bool)(_filter & (uint)current->data(COL_TYPE,ROLE_TYPE).toUInt());
      ui.lstMessages->setItemHidden(current, !showCurrent);
      if (showCurrent) {
        _messagesShown++;
      }
    /* If we are showing the maximum, then get rid of the rest */
    } else {
      ui.lstMessages->takeTopLevelItem(currentIndex);
    }
    currentIndex--;
  }
}

/** Formats a message item from the message log into a format suitable for
 * writing to the clipboard or saving to a file.
 * \param messageItem A message log item to format.
 * \return A properly formatted log message as a string.
 */
QString
MessageLog::format(QTreeWidgetItem *item)
{
  return QString("%1 [%2] %3\n").arg(item->text(COL_TIME))
                                .arg(item->text(COL_TYPE))
                                .arg(item->text(COL_MSG).trimmed());
}

/** Sorts the given list of log message items in ascending chronological
 * order.
 * \param list The unsorted list of message items.
 * \return The sorted list of message items.
 */
QList<QTreeWidgetItem *>
MessageLog::sort(QList<QTreeWidgetItem *> items)
{
  QMap<QDateTime, QTreeWidgetItem *> sortedList;
  foreach (QTreeWidgetItem *item, items) {
    sortedList.insert(
       QDateTime::fromString(item->text(COL_TIME), DATETIME_FMT), item);
  }
  return sortedList.values();
}

/** Called when the user clicks "Browse" to select a new log file. */
void
MessageLog::browse()
{
  QString filename = QFileDialog::getSaveFileName(this,
                         tr("Select Log File"), "tor.log");
  if (!filename.isEmpty()) {
    ui.lineFile->setText(filename);
  }
}

/** Saves the given list of items to a file.
 * \param items A list of log message items to save. 
 */
void
MessageLog::save(QList<QTreeWidgetItem *> items)
{
  if (!items.size()) {
    return;
  }

  QString fileName = QFileDialog::getSaveFileName(this,
                          tr("Save Log Messages"),
                          "VidaliaLog-" + 
                          QDateTime::currentDateTime().toString("MM.dd.yyyy") +
                          ".txt");
  
  /* If the choose to save */
  if (!fileName.isEmpty()) {
    QFile file(fileName);
    
    /* If can't write to file, show error message */
    if (!file.open(QFile::WriteOnly | QFile::Text)) {
      QMessageBox::warning(this, tr("Vidalia"),
                          tr("Cannot write file %1:\n%2.")
                          .arg(fileName)
                          .arg(file.errorString()));
      return;
    }
   
    /* Write out the message log to the file */
    QTextStream out(&file);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    foreach (QTreeWidgetItem *item, sort(items)) {
      out << format(item);
    }
    QApplication::restoreOverrideCursor();
  }
}

/** Saves currently selected messages to a file. */
void
MessageLog::saveSelected()
{
  save(ui.lstMessages->selectedItems());
}

/** Saves all shown messages to a file. */
void
MessageLog::saveAll()
{
  save(ui.lstMessages->findItems("*", Qt::MatchWildcard));
}

/** Copies contents of currently selected messages to the 'clipboard'. */
void
MessageLog::copy()
{
  QList<QTreeWidgetItem *> selected = sort(ui.lstMessages->selectedItems());
  int count = selected.size();
  
  /* Do nothing if there are no selected messages */
  if (!count) {
    return;
  }
  
  /* Clear anything on the clipboard */
  QApplication::clipboard()->clear();

  /* Copy the selected messages to the clipboard */
  QString contents;
  for(int i=0; i < count; i++) {
    contents += format(selected[i]); 
  }
  QApplication::clipboard()->setText(contents);
}

/** Prompts the user for a search string. If the search string is not found in
 * any of the currently displayed log entires, then a message will be
 * displayed for the user informing them that no matches were found. 
 * \sa search()
 */
void
MessageLog::find()
{
  QString empty;
  bool ok;
  QString text = QInputDialog::getText(this, tr("Find in Message Log"),
                  tr("Find:"), QLineEdit::Normal, QString(), &ok);
  
  if (ok && !text.isEmpty()) {
    QTreeWidget *tree = ui.lstMessages;
    QList<QTreeWidgetItem *> results = sort(search(text));
    if (!results.size()) {
      QMessageBox::information(this, tr("Not Found"), 
                               tr("Search found 0 matches."), 
                               QMessageBox::Ok, QMessageBox::NoButton);
    } else {
      /* Deselect all currently selected items */
      deselectAllItems();
      /* Select the new matching items */
      foreach(QTreeWidgetItem *item, results) {
        if (!tree->isItemHidden(item)) {
           tree->setItemSelected(item, true);
         }
      }
      /* Set the focus to the first match */
      tree->scrollToItem(results.at(0));
    }
  }
}

/** Clears the message list and resets the message counter. */
void
MessageLog::clear()
{
  _messagesShown = 0;
  ui.lstMessages->setStatusTip(QString("Messages Shown: %1")
                                  .arg(_messagesShown));
}

/** Searches the currently displayed log entries for the given search text.
 * \param text The text to search for.
 * \return A list of all log items containing the search text. 
 */
QList<QTreeWidgetItem *>
MessageLog::search(QString text)
{
  QTreeWidget *tree = ui.lstMessages;
  QList<QTreeWidgetItem *> results;

  /* Search through the messages in the tree, case-insensitively */
  return tree->findItems(text, Qt::MatchContains|Qt::MatchWrap, COL_MSG);
}

/** Deselects all currently selected items. */
void
MessageLog::deselectAllItems()
{
  QTreeWidget *tree = ui.lstMessages;
  foreach(QTreeWidgetItem *item, tree->selectedItems()) {
    tree->setItemSelected(item, false);
  }
}

/** Toggles the Settings pane on and off and changes toggle button text.
 * \param show Whether to show or hide the Settings frame.
 */
void
MessageLog::showSettingsFrame(bool show)
{
  if (show) {
    ui.frmSettings->setVisible(true);
    ui.btnToggleSettings->setChecked(true);
    ui.btnToggleSettings->setText("Hide Settings");
  } else {
    ui.frmSettings->setVisible(false);
    ui.btnToggleSettings->setChecked(false);
    ui.btnToggleSettings->setText("Show Settings");
  }
}

/** Sets the opacity of the Message Log window.
 * \param value The opaqueness of the window (0-100)
 */
void
MessageLog::setOpacity(int value)
{
  qreal newValue = value / 100.0;

  /** Opacity only supported by Mac and Win32 **/
  #if defined(Q_WS_MAC)
    this->setWindowOpacity(newValue);
  #endif

  #if defined(Q_WS_WIN)
    if(QSysInfo::WV_2000 <= QSysInfo::WindowsVersion <= QSysInfo::WV_2003) {
      this->setWindowOpacity(newValue);
    }
  #endif
}

/** Creates a new log item in the message log and returns a pointer to it.
 * \param type The log event severity
 * \param message The log message.
 * \return A pointer to the new message log item.
 */
QTreeWidgetItem *
MessageLog::newMessageItem(LogEvent::Severity type, QString message)
{
  QTreeWidgetItem *newMessage = new QTreeWidgetItem();
  
  /* Change row color and text for serious warnings and errors */
  if (type == LogEvent::TorError) {
    /* Critical messages are red with white text */
    for (int i=0; i < ui.lstMessages->columnCount(); i++) {
      newMessage->setBackgroundColor(i, Qt::red);
      newMessage->setTextColor(i, Qt::white);
    }
  } else if (type == LogEvent::TorWarn) {
    /* Warning messages are yellow with black text */
    for (int i=0; i < ui.lstMessages->columnCount(); i++) {
      newMessage->setBackgroundColor(i, Qt::yellow);
    }
  }
  
  /* Assemble the new log message item */
  newMessage->setText(COL_TIME,
      QDateTime::currentDateTime().toString(DATETIME_FMT));
  newMessage->setTextAlignment(COL_TYPE, Qt::AlignCenter);
  newMessage->setText(COL_TYPE, LogEvent::severityToString(type));
  newMessage->setText(COL_MSG, message);
  newMessage->setData(COL_TYPE, ROLE_TYPE, (uint)type);
  
  return newMessage;
}

/** Writes a message to the Message History and tags it with
 * the proper date, time and type.
 * \param type The message's severity type.
 * \param message The log message to be added.
 */
void 
MessageLog::log(LogEvent::Severity type, QString message)
{
  QTreeWidgetItem *newMessage = newMessageItem(type, message);
  
  /* Remove top message if message log is at maximum setting */
  if (ui.lstMessages->topLevelItemCount() == _maxCount) {
    /* Decrease shown messages counter if removing a shown message */
    if (!ui.lstMessages->isItemHidden(ui.lstMessages->topLevelItem(0))) {
      _messagesShown--;
    }
    ui.lstMessages->takeTopLevelItem(0);
  }
  
  /* Add the message to the bottom of the list */
  ui.lstMessages->addTopLevelItem(newMessage);

  /* Hide the message if necessary */
  if (_filter & (uint)type) {
    /* If shown, update counter and select the newly added message */
    _messagesShown++;
    ui.lstMessages->setStatusTip(QString("Messages Shown: %1")
                                  .arg(_messagesShown));
    ui.lstMessages->scrollToItem(newMessage);
  } else {
    ui.lstMessages->setItemHidden(newMessage, true);
  }

  /* If we're saving log messages to a file, go ahead and do that now */
  if (_enableLogging) {
    _logStream << format(newMessage);
    _logStream.flush(); /* Write to disk right away */
  }
}

/** Custom event handler. Checks if the event is a log event. If it is, then
 * it will write the message to the message log. 
 * \param event The custom log event. 
 */
void
MessageLog::customEvent(QEvent *event)
{
  if (event->type() == CustomEventType::LogEvent) {
    LogEvent *e = (LogEvent *)event;
    log(e->severity(), e->message());
  }
}

/** Overloads the default show() slot so we can set opacity. */
void
MessageLog::show()
{
  loadSettings();
  QWidget::show();
}

/** Overloads the default close() slot, so we can force the parent to become
 * visible. This only matters on Mac, so we can ensure the correct menubar is
 * displayed. */
void
MessageLog::close()
{
  MainWindow *p = (MainWindow *)parent();
  if (p) {
    p->show();
  }
  QMainWindow::close();
}

/** Serves the same purpose as MessageLog::close(), but this time responds to
 * when the user clicks on the X in the titlebar */
void
MessageLog::closeEvent(QCloseEvent *e)
{
  MainWindow *p = (MainWindow *)parent();
  if (p) {
    p->show();
  }
  e->accept();
}

