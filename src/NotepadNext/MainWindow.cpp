/*
 * This file is part of Notepad Next.
 * Copyright 2019 Justin Dailey
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Notepad Next is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Notepad Next.  If not, see <https://www.gnu.org/licenses/>.
 */


#include "MainWindow.h"
#include "ui_MainWindow.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QStringList>
#include <QClipboard>
#include <QSettings>
#include <QStandardPaths>
#include <QWindow>
#include <QPushButton>

#ifdef Q_OS_WIN
#include <Windows.h>
#endif

#include "NotepadNextApplication.h"
#include "Settings.h"

#include "ScintillaNext.h"
#include "ScintillaBuffer.h"
#include "BufferManager.h"

// Editor plugins
#include "BraceMatch.h"
#include "HighlightedScrollBar.h"
#include "SmartHighlighter.h"

#include "RecentFilesListManager.h"
#include "LuaExtension.h"
#include "LuaState.h"
#include "MacroRecorder.h"

#include "LuaConsoleDock.h"

#include "FindReplaceDialog.h"
#include "LineNumbers.h"
#include "MacroRunDialog.h"
#include "MacroSaveDialog.h"
#include "PreferencesDialog.h"
#include "WindowListDialog.h"


MainWindow::MainWindow(NotepadNextApplication *app, QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    app(app)
{
    qInfo(Q_FUNC_INFO);

    ui->setupUi(this);

    qInfo("setupUi Completed");

    bufferManager = new BufferManager(this);
    recentFilesListManager = new RecentFilesListManager(ui->menuRecentFiles);

    tabbedEditor = new TabbedEditor();
    this->setCentralWidget(tabbedEditor);
    editor = tabbedEditor->getEditor();

    // Set up the lua state and the extension
    LuaExtension::Instance().Initialise(app->getLuaState()->L, this->editor);

    setupStatusBar();

#ifdef QT_DEBUG
    // Print some debug messages: connect these first in case something bad happens during a later slot
    connect(bufferManager, &BufferManager::bufferCreated, [](ScintillaBuffer *buffer) { qInfo("BufferManager::bufferCreated(%s)", buffer->getName().toUtf8().constData());});
    connect(bufferManager, &BufferManager::bufferSaved, [](ScintillaBuffer *buffer) { qInfo("BufferManager::bufferSaved(%s)", buffer->getName().toUtf8().constData());});
    connect(bufferManager, &BufferManager::bufferClosed, [](ScintillaBuffer *buffer) { qInfo("BufferManager::bufferClosed(%s)", buffer->getName().toUtf8().constData());});
    connect(bufferManager, &BufferManager::bufferRenamed, [](ScintillaBuffer *buffer) { qInfo("BufferManager::bufferRenamed(%s)", buffer->getName().toUtf8().constData());});

    connect(tabbedEditor, &TabbedEditor::bufferSwitched, [](ScintillaBuffer *buffer) { qInfo("bufferSwitched(%s)", buffer->getName().toUtf8().constData());});
    connect(tabbedEditor, &TabbedEditor::allBuffersClosed, []() { qInfo("allBuffersClosed()");});
    connect(tabbedEditor->getTabBar(), &QTabBar::tabMoved, [](int from, int to) { qInfo("tabMoved(from %d, to %d)", from, to);});

    connect(editor, &ScintillaNext::savePointChanged, [](bool b) { qInfo("savePointChanged(%s)", b ? "true" : "false");});
    connect(editor, &ScintillaNext::updateUi, [](int updated) { qInfo("updateUi(%d)", updated);});
    connect(editor, &ScintillaNext::uriDropped, [](const QString &uri) { qInfo("uriDropped(%s)", qUtf8Printable(uri));});
    connect(editor, &ScintillaNext::marginClicked, [](int position, int modifiers, int margin) { qInfo("marginClicked(%d, %d, %d)", position, modifiers, margin);});
    //connect(editor, &ScintillaNext::modified, [](int type, int position, int length, int linesAdded, const QByteArray &text, int line, int foldNow, int foldPrev) {
    //    qInfo("linesAdded %d", linesAdded);
    //});
    connect(editor, &ScintillaNext::styleNeeded, [](int pos)  { qInfo("styleNeeded(%d)", pos);});
#endif

    // Get some events from the tab bar
    connect(tabbedEditor->getTabBar(), &QTabBar::tabBarDoubleClicked, this, [=](int index) {
        if (index == TabbedEditor::INVALID_INDEX) {
            newFile();
        }
    });
    connect(tabbedEditor->getTabBar(), &QTabBar::tabCloseRequested, this, &MainWindow::closeFile);
    connect(tabbedEditor->getTabBar(), &QWidget::customContextMenuRequested, this, &MainWindow::tabBarRightClicked);

    // We need to be notified when the tabbed editor switches to a new file (e.g. via the user clicking on a tab)
    connect(tabbedEditor, &TabbedEditor::bufferSwitched, this, &MainWindow::bufferActivated);

    // Create an empty buffer if all have closed. TODO: handle future option to close the application on last file being closed
    connect(tabbedEditor, &TabbedEditor::allBuffersClosed, this, &MainWindow::newFile);

    // TODO: this might not be the best way but works for now
    // Set up the menus
    connect(ui->actionNew, &QAction::triggered, this, &MainWindow::newFile);
    connect(ui->actionOpen, &QAction::triggered, this, &MainWindow::openFileDialog);
    connect(ui->actionReload, &QAction::triggered, this, &MainWindow::reloadFile);
    connect(ui->actionClose, &QAction::triggered, this, &MainWindow::closeCurrentFile);
    connect(ui->actionCloseAll, &QAction::triggered, this, &MainWindow::closeAllFiles);
    connect(ui->actionExit, &QAction::triggered, this, &MainWindow::close);

    connect(ui->actionCloseAllExceptActive, &QAction::triggered, this, &MainWindow::closeAllExceptActive);
    connect(ui->actionCloseAllToLeft, &QAction::triggered, this, &MainWindow::closeAllToLeft);
    connect(ui->actionCloseAllToRight, &QAction::triggered, this, &MainWindow::closeAllToRight);

    connect(ui->actionSave, &QAction::triggered, this, &MainWindow::saveCurrentFile);
    connect(ui->actionSaveAs, &QAction::triggered, this, &MainWindow::saveCurrentFileAsDialog);
    connect(ui->actionSaveCopyAs, &QAction::triggered, this, &MainWindow::saveCopyAsDialog);
    connect(ui->actionSaveAll, &QAction::triggered, this, &MainWindow::saveAll);
    connect(ui->actionRename, &QAction::triggered, this, &MainWindow::renameFile);

    connect(ui->actionClearRecentFilesList, &QAction::triggered, recentFilesListManager, &RecentFilesListManager::clear);
    connect(bufferManager, &BufferManager::bufferClosed, [=](ScintillaBuffer *buffer) {
        if (buffer->isFile()) {
            recentFilesListManager->addFile(buffer->fileInfo.canonicalFilePath());
        }
    });
    connect(bufferManager, &BufferManager::bufferCreated, [=](ScintillaBuffer *buffer) {
        if (buffer->isFile()) {
            recentFilesListManager->removeFile(buffer->fileInfo.canonicalFilePath());
        }
    });
    connect(bufferManager, &BufferManager::bufferRenamed, [=](ScintillaBuffer *buffer) {
        recentFilesListManager->removeFile(buffer->fileInfo.filePath());
    });
    connect(ui->actionRestoreRecentlyClosedFile, &QAction::triggered, [=]() {
        if (recentFilesListManager->count() > 0) {
            openFileList(QStringList() << recentFilesListManager->mostRecentFile());
        }
    });
    connect(ui->actionOpenAllRecentFiles, &QAction::triggered, [=]() {
        openFileList(recentFilesListManager->fileList());
    });
    connect(recentFilesListManager, &RecentFilesListManager::fileOpenRequest, this, &MainWindow::openFile);

    QActionGroup *eolActionGroup = new QActionGroup(this);
    eolActionGroup->addAction(ui->actionWindows);
    eolActionGroup->addAction(ui->actionUnix);
    eolActionGroup->addAction(ui->actionMacintosh);

    connect(ui->actionWindows, &QAction::triggered, [=]() { convertEOLs(SC_EOL_CRLF); });
    connect(ui->actionUnix, &QAction::triggered, [=]() { convertEOLs(SC_EOL_LF); });
    connect(ui->actionMacintosh, &QAction::triggered, [=]() { convertEOLs(SC_EOL_CR); });

    connect(ui->actionUpperCase, &QAction::triggered, [=]() { editor->upperCase();});
    connect(ui->actionLowerCase, &QAction::triggered, [=]() { editor->lowerCase();});

    connect(ui->actionDuplicateCurrentLine, &QAction::triggered, [=]() { editor->lineDuplicate();});
    connect(ui->actionMoveCurrentLineUp, &QAction::triggered, [=]() { editor->moveSelectedLinesUp();});
    connect(ui->actionMoveCurrentLineDown, &QAction::triggered, [=]() { editor->moveSelectedLinesDown();});
    connect(ui->actionSplitLines, &QAction::triggered, [=]() {
        editor->targetFromSelection();
        editor->linesSplit(0);
    });
    connect(ui->actionJoinLines, &QAction::triggered, [=]()  {
        editor->targetFromSelection();
        editor->linesJoin();
    });

    connect(ui->actionUndo, &QAction::triggered, editor, &ScintillaNext::undo);
    connect(ui->actionRedo, &QAction::triggered, editor, &ScintillaNext::redo);
    connect(ui->actionCut, &QAction::triggered, editor, &ScintillaNext::cut);
    connect(ui->actionCopy, &QAction::triggered, editor, &ScintillaNext::copy);
    connect(ui->actionPaste, &QAction::triggered, editor, &ScintillaNext::paste);
    connect(ui->actionSelect_All, &QAction::triggered, editor, &ScintillaNext::selectAll);
    connect(ui->actionCopyFullPath, &QAction::triggered, [=]() {
        auto buffer = tabbedEditor->getCurrentBuffer();
        if (buffer->isFile()) {
            QApplication::clipboard()->setText(buffer->fileInfo.canonicalFilePath());
        }
    });
    connect(ui->actionCopyFileName, &QAction::triggered, [=]() { QApplication::clipboard()->setText(tabbedEditor->getCurrentBuffer()->getName());});
    connect(ui->actionCopyFileDirectory, &QAction::triggered, [=]() {
        auto buffer = tabbedEditor->getCurrentBuffer();
        if (buffer->isFile()) {
            QApplication::clipboard()->setText(buffer->fileInfo.canonicalFilePath());
        }
    });
    connect(ui->actionIncrease_Indent, &QAction::triggered, editor, &ScintillaNext::tab);
    connect(ui->actionDecrease_Indent, &QAction::triggered, editor, &ScintillaNext::backTab);

    connect(ui->actionFind, &QAction::triggered, [=]() {
        // Create it if it doesn't exist
        if (frd == Q_NULLPTR) {
            frd = new FindReplaceDialog(this);
            frd->setEditor(editor);
        }

        // Get any selected text
        if (!editor->selectionEmpty()) {
            int selection = editor->mainSelection();
            int start = editor->selectionNStart(selection);
            int end = editor->selectionNEnd(selection);
            if (end > start) {
                auto selText = editor->get_text_range(start, end);
                frd->setFindText(QString::fromUtf8(selText));
            }
        }
        else {
            int start = editor->wordStartPosition(editor->currentPos(), true);
            int end = editor->wordEndPosition(editor->currentPos(), true);
            if (end > start) {
                editor->setSelectionStart(start);
                editor->setSelectionEnd(end);
                auto selText = editor->get_text_range(start, end);
                frd->setFindText(QString::fromUtf8(selText));
            }
        }

        frd->setTab(FindReplaceDialog::FIND_TAB);
        frd->show();
        frd->raise();
        frd->activateWindow();
    });

    connect(ui->actionFindNext, &QAction::triggered, [=]() {
        if (frd != Q_NULLPTR) {
            frd->performLastSearch();
        }
    });

    connect(ui->actionReplace, &QAction::triggered, [=]() {
        // Create it if it doesn't exist
        if (frd == Q_NULLPTR) {
            frd = new FindReplaceDialog(this);
            frd->setEditor(editor);
        }

        // Get any selected text
        if (!editor->selectionEmpty()) {
            int selection = editor->mainSelection();
            int start = editor->selectionNStart(selection);
            int end = editor->selectionNEnd(selection);
            if (end > start) {
                auto selText = editor->get_text_range(start, end);
                frd->setFindText(QString::fromUtf8(selText));
            }
        }
        else {
            int start = editor->wordStartPosition(editor->currentPos(), true);
            int end = editor->wordEndPosition(editor->currentPos(), true);
            if (end > start) {
                editor->setSelectionStart(start);
                editor->setSelectionEnd(end);
                auto selText = editor->get_text_range(start, end);
                frd->setFindText(QString::fromUtf8(selText));
            }
        }

        frd->setTab(FindReplaceDialog::REPLACE_TAB);
        frd->show();
        frd->raise();
        frd->activateWindow();
    });


    connect(ui->actionAlwaysOnTop, &QAction::triggered, [=](bool b) {
        const Qt::WindowFlags flags = this->windowFlags();
        if (b)
            this->setWindowFlags(flags | Qt::WindowStaysOnTopHint);
        else
            this->setWindowFlags(flags & ~Qt::WindowStaysOnTopHint);
        this->show();
    });

    ui->pushExitFullScreen->setParent(this); // This is important
    ui->pushExitFullScreen->setVisible(false);
    connect(ui->pushExitFullScreen, &QPushButton::clicked, ui->actionFullScreen, &QAction::trigger);
    connect(ui->actionFullScreen, &QAction::triggered, [=](bool b) {
        if (b) {
            // NOTE: don't hide() these as it will cancel their actions they hold
            ui->menuBar->setMaximumHeight(0);
            ui->mainToolBar->setMaximumHeight(0);

            this->showFullScreen();
            ui->pushExitFullScreen->setGeometry(width() - 20, 0, 20, 20);
            ui->pushExitFullScreen->show();
            ui->pushExitFullScreen->raise();
        }
        else {
            ui->menuBar->setMaximumHeight(QWIDGETSIZE_MAX);
            ui->mainToolBar->setMaximumHeight(QWIDGETSIZE_MAX);
            this->showNormal();

            ui->pushExitFullScreen->hide();
        }
    });

    // The default behavior of QActionGroup cannot be used since it *must* have one set
    // but Notepad++ allows none to also be set.
    QActionGroup *showSymbolActionGroup = new QActionGroup(this);
    showSymbolActionGroup->addAction(ui->actionShowWhitespaceandTab);
    showSymbolActionGroup->addAction(ui->actionShowEndofLine);
    showSymbolActionGroup->addAction(ui->actionShowAllCharacters);
    showSymbolActionGroup->setExclusive(false);

    connect(showSymbolActionGroup, &QActionGroup::triggered, [=](QAction *action) {
        if (!action->isChecked()) {
            editor->setViewWS(SCWS_INVISIBLE);
            editor->setViewEOL(false);
        }
        else {
            // Uncheck all other actions
            foreach (QAction *otherAction, showSymbolActionGroup->actions()) {
                if (otherAction != action) {
                    otherAction->setChecked(false);
                }
            }

            if (action == ui->actionShowWhitespaceandTab) {
                editor->setViewWS(SCWS_VISIBLEALWAYS);
                editor->setViewEOL(false);
            }
            else if (action == ui->actionShowEndofLine) {
                editor->setViewWS(SCWS_INVISIBLE);
                editor->setViewEOL(true);
            }
            else if (action == ui->actionShowAllCharacters) {
                editor->setViewWS(SCWS_VISIBLEALWAYS);
                editor->setViewEOL(true);
            }
        }
    });
    // The first time it is triggered it doesn't see it as checked for some reason
    ui->actionShowWhitespaceandTab->setChecked(true);
    ui->actionShowWhitespaceandTab->trigger();
    ui->actionShowWhitespaceandTab->trigger();

    connect(ui->actionShowWrapSymbol, &QAction::triggered, [=](bool b) {
        editor->setWrapVisualFlags(b ? SC_WRAPVISUALFLAG_END : SC_WRAPVISUALFLAG_NONE);
    });
    connect(ui->actionShowIndentGuide, &QAction::triggered, [=](bool b) {
        editor->setIndentationGuides(b ? SC_IV_LOOKBOTH : SC_IV_NONE);
    });
    ui->actionShowIndentGuide->setChecked(true);

    connect(ui->actionWordWrap, &QAction::triggered, [=](bool b) {
        if (b) {
            editor->setWrapMode(SC_WRAP_WORD);
        }
        else {
            // Store the top line and restore it after the lines have been unwrapped
            int topLine = editor->docLineFromVisible(editor->firstVisibleLine());
            editor->setWrapMode(SC_WRAP_NONE);
            editor->setFirstVisibleLine(topLine);
        }
    });

    connect(ui->actionZoomIn, &QAction::triggered, editor, &ScintillaNext::zoomIn);
    connect(ui->actionZoomOut, &QAction::triggered, editor, &ScintillaNext::zoomOut);
    connect(ui->actionZoomReset, &QAction::triggered, [=]() { editor->setZoom(0); });

    languageActionGroup = new QActionGroup(this);
    languageActionGroup->setExclusive(true);

    connect(ui->actionPreferences, &QAction::triggered, [=] {
        if (pd == Q_NULLPTR)
            pd = new PreferencesDialog(this->settings, this);
        pd->show();
        pd->raise();
        pd->activateWindow();
    });

    recorder = new MacroRecorder(editor, this);
    connect(ui->actionMacroRecording, &QAction::triggered, [=](bool b) {
        if (b) {
            ui->actionMacroRecording->setText("Stop Recording");
            recorder->startRecording();

            // A macro is being recorded so disable some macro options
            ui->actionPlayback->setEnabled(false);
            ui->actionRunMacroMultipleTimes->setEnabled(false);
            ui->actionSaveCurrentRecordedMacro->setEnabled(false);
        }
        else {
            ui->actionMacroRecording->setText("Start Recording");
            Macro *m = recorder->stopRecording();

            if (m->size() > 0) {
                currentMacro = m;
                ui->actionPlayback->setEnabled(true);
                ui->actionRunMacroMultipleTimes->setEnabled(true);
                ui->actionSaveCurrentRecordedMacro->setEnabled(true);
            }
            else {
                // Don't need the macro returned from the recorder
                delete m;

                // A previously recorded one may still be valid
                if (currentMacro) {
                    ui->actionPlayback->setEnabled(true);
                    ui->actionRunMacroMultipleTimes->setEnabled(true);

                    // It may or may not be "saved" in the list
                    if (!macros.contains(currentMacro))
                        ui->actionSaveCurrentRecordedMacro->setEnabled(true);
                }
            }
        }
    });

    connect(ui->actionPlayback, &QAction::triggered, [=](bool b) {
        currentMacro->replay(editor);
    });

    connect(ui->actionRunMacroMultipleTimes, &QAction::triggered, [=](bool b) {
        if (mrd == Q_NULLPTR) {
            mrd = new MacroRunDialog(this);
            connect(mrd, &MacroRunDialog::execute, [=](Macro *macro, int times) {
                if (times > 0)
                    macro->replay(editor, times);
                else if (times == -1)
                    macro->replayTillEndOfFile(editor);
            });
        }

        if (!macros.contains(currentMacro))
            mrd->setMacros(QVector<Macro *>(macros) << currentMacro);
        else
            mrd->setMacros(macros);

        mrd->show();
        mrd->raise();
        mrd->activateWindow();
    });

    connect(ui->actionSaveCurrentRecordedMacro, &QAction::triggered, [=](bool b) {
        MacroSaveDialog msd;

        msd.show();
        msd.raise();
        msd.activateWindow();

        if (msd.exec() == QDialog::Accepted) {
            // The macro has been saved so disable save option
            ui->actionSaveCurrentRecordedMacro->setEnabled(false);

            // TODO: does the macro name already exist?

            currentMacro->setName(msd.getName());
            if (!msd.getShortcut().isEmpty()) {
                // do something with msd.getShortcut().isEmpty()
            }

            macros.append(currentMacro);
        }

    });

    connect(ui->menuWindows, &QMenu::aboutToShow, [=]() {
        auto actions = ui->menuWindows->actions();

        // Keep the original 2 in the list (seperator and Window command)
        // delete the rest since nobody has ownership of them
        while (actions.size() > 2) {
            delete actions.takeFirst();
        }

        const int current = tabbedEditor->currentIndex();
        for (int i = 0; i < tabbedEditor->count(); ++i) {
            // NOTE: the menu does not take ownership when using insertAction
            QAction *action = new QAction(tabbedEditor->getBufferFromIndex(i)->getName());

            if (i == current) {
                action->setCheckable(true);
                action->setChecked(true);
            }

            connect(action, &QAction::triggered, [=]() {
                tabbedEditor->switchToIndex(i);
            });

            ui->menuWindows->insertAction(actions.at(0), action);
        }
    });

    connect(ui->actionWindowsList, &QAction::triggered, [=]() {
        QList<ScintillaBuffer *> buffers;
        for (int i = 0; i < tabbedEditor->count(); ++i) {
            buffers << tabbedEditor->getBufferFromIndex(i);
        }

        WindowListDialog wld(this, buffers, this);
        wld.show();
        wld.raise();
        wld.activateWindow();
        wld.exec();
    });

    connect(ui->actionAboutQt, &QAction::triggered, &QApplication::aboutQt);
    connect(ui->actionAboutNotepadNext, &QAction::triggered, [=]() {
        QMessageBox::about(this, "About Notepad Next",
                            QString("<h3>Notepad Next v%1</h3><p>This program does stuff.</p><p>%2</p>")
                                .arg(APP_VERSION)
                                .arg(APP_COPYRIGHT));
    });

    // Connect the editor to the UI
    connect(editor, &ScintillaNext::savePointChanged, this, &MainWindow::updateSaveStatusBasedUi);
    connect(editor, &ScintillaNext::updateUi, this, &MainWindow::updateSelectionBasedUi);
    connect(tabbedEditor->getTabBar(), &QTabBar::tabMoved, this, &MainWindow::updateBufferPositionBasedUi);

    connect(editor, &ScintillaNext::marginClicked, [=](int position, int modifiers, int margin) {
        if (margin == 1) {
            int line = editor->lineFromPosition(position);

            if (editor->markerGet(line) & (1 << 24)) {
                while (editor->markerGet(line) & (1 << 24)) {
                    editor->markerDelete(line, 24);
                }
            }
            else {
                editor->markerAdd(line, 24);
            }
        }
    });

    // Send some events to the tabbed editor to automatcially manage the tabs
    connect(bufferManager, &BufferManager::bufferCreated, tabbedEditor, &TabbedEditor::addBuffer);
    connect(bufferManager, &BufferManager::bufferClosed, tabbedEditor, &TabbedEditor::removeBuffer);
    connect(bufferManager, &BufferManager::bufferRenamed, tabbedEditor, &TabbedEditor::renamedBuffer);

    // If the current file is saved update the window title incase the file was renamed
    connect(bufferManager, &BufferManager::bufferCreated, this, &MainWindow::detectLanguageFromExtension);
    connect(bufferManager, &BufferManager::bufferClosed, this, &MainWindow::updateBufferPositionBasedUi);
    connect(bufferManager, &BufferManager::bufferRenamed, [=] (ScintillaBuffer *buffer) {
        updateBufferFileStatusBasedUi(buffer);
        detectLanguageFromExtension(buffer);
        updateGui(buffer);
    });

    ui->actionAboutQt->setIcon(QPixmap(QLatin1String(":/qt-project.org/qmessagebox/images/qtlogo-64.png")));
    ui->actionAboutNotepadNext->setIcon(this->windowIcon());
    ui->actionAboutNotepadNext->setShortcut(QKeySequence::HelpContents);

    QSettings settings;
    restoreGeometry(settings.value("MainWindow/geometry").toByteArray());
    restoreState(settings.value("MainWindow/windowState").toByteArray());
    recentFilesListManager->setFileList(settings.value("App/RecentFilesList").toStringList());

    if (luaConsoleDock == Q_NULLPTR) {
        luaConsoleDock = new LuaConsoleDock(app->getLuaState(), this);
        luaConsoleDock->hide();
        addDockWidget(Qt::BottomDockWidgetArea, luaConsoleDock);
        ui->menuHelp->addSeparator()->setText("pickles");
        ui->menuHelp->addAction(luaConsoleDock->toggleViewAction());
    }
}

MainWindow::~MainWindow()
{
    Q_ASSERT(tabbedEditor->count() == 0);
}

void MainWindow::initialize(Settings *settings)
{
    qInfo(Q_FUNC_INFO);

    this->settings = settings;
    connect(settings, &Settings::showMenuBarChanged, [=](bool showMenuBar) {
        // Don't hide it else the actions won't be enabled
        ui->menuBar->setMaximumHeight(showMenuBar ? QWIDGETSIZE_MAX : 0);
    });
    connect(settings, &Settings::showToolBarChanged, ui->mainToolBar, &QToolBar::setVisible);
    connect(settings, &Settings::showTabBarChanged, tabbedEditor->getTabBar(), &QTabBar::setVisible);
    connect(settings, &Settings::showStatusBarChanged, ui->statusBar, &QStatusBar::setVisible);

    connect(settings, &Settings::tabsClosableChanged, tabbedEditor->getTabBar(), &QTabBar::setTabsClosable);

    setupEditor(editor);


    // Plugins
    SmartHighlighter *s = new SmartHighlighter(editor);
    s->setEnabled(true);

    HighlightedScrollBarPlugin *h = new HighlightedScrollBarPlugin(editor);
    h->setEnabled(true);

    BraceMatch *b = new BraceMatch(editor);
    b->setEnabled(true);

    LineNumbers *l = new LineNumbers(editor);
    l->setEnabled(true);


    setupLanguageMenu();

    if (tabbedEditor->count() == 0)
        newFile();

    editor->grabFocus();

    // Put the style sheet here for now
    QFile f(":/stylesheets/npp.css");
    f.open(QFile::ReadOnly);
    setStyleSheet(f.readAll());
    f.close();
}

void MainWindow::setupLanguageMenu()
{
    qInfo(Q_FUNC_INFO);

    QStringList language_names = app->getLuaState()->executeAndReturn<QStringList>(
                R"(
                local names = {}
                for _, L in pairs(languages) do
                    names[#names + 1] = L.name
                end
                table.sort(names)
                return names
                )");

    int i = 0;
    while (i < language_names.size()) {
        QList<QAction *> actions;
        int j = i;

        // Get all consecutive names that start with the same letter
        // NOTE: this loop always runs once since i == j the first time
        while (j < language_names.size() &&
               language_names[i][0].toUpper() == language_names[j][0].toUpper()) {
            const QString &key = language_names[j];
            QAction *action = new QAction(key);
            action->setCheckable(true);
            action->setData(key);
            connect(action, &QAction::triggered, this, &MainWindow::languageMenuTriggered);
            languageActionGroup->addAction(action);
            actions.append(action);

            if (key == "normal")
                action->setChecked(true);
            ++j;
        }

        if (actions.size() == 1) {
            ui->menuLanguage->addActions(actions);
        }
        else {
            // Create a sub menu with the actions
            QMenu *compactMenu = new QMenu(actions[0]->text().at(0).toUpper());
            compactMenu->addActions(actions);
            ui->menuLanguage->addMenu(compactMenu);
        }
        i = j;
    }
}

const int MARK_BOOKMARK = 24;
const int MARK_HIDELINESBEGIN = 23;
const int MARK_HIDELINESEND = 22;
const int MARK_HIDELINESUNDERLINE = 21;

void MainWindow::setupEditor(ScintillaNext *editor)
{
    qInfo(Q_FUNC_INFO);

    setFoldMarkers(editor, "circle");
    editor->setIdleStyling(SC_IDLESTYLING_TOVISIBLE);
    editor->setEndAtLastLine(false);

    editor->setCodePage(SC_CP_UTF8);

    editor->setMultipleSelection(true);
    editor->setAdditionalSelectionTyping(true);
    editor->setMultiPaste(SC_MULTIPASTE_EACH);
    editor->setVirtualSpaceOptions(SCVS_RECTANGULARSELECTION);

    editor->setMarginWidthN(0, 30);
    editor->setMarginMaskN(1, (1<<MARK_BOOKMARK) | (1<<MARK_HIDELINESBEGIN) | (1<<MARK_HIDELINESEND) | (1<<MARK_HIDELINESUNDERLINE));
    editor->setMarginMaskN(2, SC_MASK_FOLDERS);
    editor->setMarginWidthN(2, 14);

    editor->markerSetAlpha(MARK_BOOKMARK, 70);
    editor->markerDefine(MARK_HIDELINESUNDERLINE, SC_MARK_UNDERLINE);
    editor->markerSetBack(MARK_HIDELINESUNDERLINE, 0x77CC77);

    editor->markerDefine(MARK_BOOKMARK, SC_MARK_BOOKMARK);
    editor->markerDefine(MARK_HIDELINESBEGIN, SC_MARK_ARROW);
    editor->markerDefine(MARK_HIDELINESEND, SC_MARK_ARROWDOWN);

    editor->setMarginSensitiveN(1, true);
    editor->setMarginSensitiveN(2, true);

    editor->setFoldFlags(SC_FOLDFLAG_LINEAFTER_CONTRACTED);
    editor->setScrollWidthTracking(true);
    editor->setScrollWidth(1);

    //editor->clearAllCmdKeys();
    editor->setTabDrawMode(SCTD_STRIKEOUT);

    editor->assignCmdKey(SCK_RETURN, SCI_NEWLINE);

    editor->setCaretLineBack(0xFFE8E8);
    editor->setCaretLineVisible(true);
    editor->setCaretLineVisibleAlways(true);
    editor->setCaretFore(0xFF0080);
    editor->setEdgeColour(0x80FFFF);
    editor->setSelBack(true, 0xC0C0C0);

    editor->setWhitespaceFore(true, 0x6AB5FF);

    editor->setFoldMarginColour(true, 0xFFFFFF);
    editor->setFoldMarginHiColour(true, 0xE9E9E9);

    editor->setIndentationGuides(SC_IV_LOOKBOTH);

    editor->setAutomaticFold(SC_AUTOMATICFOLD_SHOW | SC_AUTOMATICFOLD_CLICK | SC_AUTOMATICFOLD_CHANGE);
    editor->markerEnableHighlight(true);

    // Indicators
    // Find Mark Style
    editor->indicSetFore(31, 0x0000FF);
    // Smart HighLighting
    editor->indicSetFore(29, 0x00FF00);
    // Incremental highlight all
    editor->indicSetFore(28, 0xFF8000);
    // Tags match highlighting
    editor->indicSetFore(27, 0xFF0080);
    // Tags attribute
    editor->indicSetFore(26, 0x00FFFF);

    /*
    -- Mark Style 1
    editor.IndicFore[25] = rgb(0x00FFFF)
    -- Mark Style 2
    editor.IndicFore[24] = rgb(0xFF8000)
    -- Mark Style 3
    editor.IndicFore[23] = rgb(0xFFFF00)
    -- Mark Style 4
    editor.IndicFore[22] = rgb(0x8000FF)
    -- Mark Style 5
    editor.IndicFore[21] = rgb(0x008000)

    SetFolderMarkers("box")
    */

    // -- reset everything
    editor->clearDocumentStyle();
    editor->styleResetDefault();

    editor->styleSetFore(STYLE_DEFAULT, 0x000000);
    editor->styleSetBack(STYLE_DEFAULT, 0xFFFFFF);
    editor->styleSetSize(STYLE_DEFAULT, 10);
    editor->styleSetFont(STYLE_DEFAULT, "Courier New");

    editor->styleClearAll();

    editor->styleSetFore(STYLE_LINENUMBER, 0x808080);
    editor->styleSetBack(STYLE_LINENUMBER, 0xE4E4E4);
    editor->styleSetBold(STYLE_LINENUMBER, true);

    editor->styleSetFore(STYLE_BRACELIGHT, 0x0000FF);
    editor->styleSetBack(STYLE_BRACELIGHT, 0xFFFFFF);

    editor->styleSetFore(STYLE_BRACEBAD, 0x000080);
    editor->styleSetBack(STYLE_BRACEBAD, 0xFFFFFF);

    editor->styleSetFore(STYLE_INDENTGUIDE, 0xC0C0C0);
    editor->styleSetBack(STYLE_INDENTGUIDE, 0xFFFFFF);

    // STYLE_CONTROLCHAR
    // STYLE_CALLTIP
    // STYLE_FOLDDISPLAYTEXT
}

void MainWindow::newFile()
{
    qInfo(Q_FUNC_INFO);

    static int count = 1;

    bufferManager->createEmtpyBuffer(QString("New %1").arg(count++));

    tabbedEditor->switchToIndex(tabbedEditor->count() - 1);
}

// One unedited, new blank document
bool MainWindow::isInInitialState()
{
    if (tabbedEditor->count() == 1) {
        ScintillaBuffer *buffer = tabbedEditor->getBufferFromIndex(0);
        return !buffer->isFile() && buffer->is_save_point();
    }

    return false;
}

void MainWindow::openFileList(const QStringList &fileNames)
{
    qInfo(Q_FUNC_INFO);

    if (fileNames.size() == 0)
        return;

    bool wasInitialState = isInInitialState();
    const ScintillaBuffer *mostRecentBuffer = Q_NULLPTR;

    foreach (const QString &filePath , fileNames) {
        qInfo(qUtf8Printable(filePath));

        // Search currently open buffers to see if it is already open
        ScintillaBuffer *buffer = bufferManager->getBufferByFilePath(filePath);
        if (buffer != Q_NULLPTR) {
            // The file has already been opened
            mostRecentBuffer = buffer;
            continue;
        }

        QFileInfo fileInfo(filePath);
        if (!fileInfo.isFile()) {
            auto reply = QMessageBox::question(this, "Create File", QString("<b>%1</b> does not exist. Do you want to create it?").arg(filePath));
            if (reply == QMessageBox::Yes) {
                buffer = bufferManager->createBufferFromFile(filePath);
                mostRecentBuffer = buffer;
            }
            else {
                // Make sure it is not still in the recent files list
                recentFilesListManager->removeFile(filePath);
            }
        }
        else {
            buffer = bufferManager->createBufferFromFile(filePath);
            mostRecentBuffer = buffer;
        }
    }

    // If any were successful, switch to the last one
    if (mostRecentBuffer) {
        tabbedEditor->switchToBuffer(mostRecentBuffer);

        if (wasInitialState) {
            closeFile(0);
        }
    }
}

bool MainWindow::checkBuffersBeforeClose(int startIndex, int endIndex)
{
    // Check if any need saved
    for(int i = startIndex; i < endIndex; ++i) {
        auto const buffer = tabbedEditor->getBufferFromIndex(i);
        if (!buffer->is_save_point()) {
            // Switch to it
            tabbedEditor->switchToIndex(i);
            // Ask the user what to do
            QString message = QString("Save file <b>%1</b>?").arg(buffer->getName());
            auto reply = QMessageBox::question(this, "Save File", message, QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);

            if (reply == QMessageBox::Cancel) {
                // Stop checking and let the caller know
                return false;
            }
            else if (reply == QMessageBox::Save) {
                bool didFileGetSaved = saveFile(buffer);

                // The user might have canceled the save file dialog so just stop now
                if (didFileGetSaved == false) {
                    // Stop checking and let the caller know
                    return false;
                }
            }
        }
    }

    // Everything is fine
    return true;
}

void MainWindow::setupStatusBar()
{
    // Set up the status bar
    docType = new StatusLabel();
    ui->statusBar->addWidget(docType, 1);
    docSize = new StatusLabel(200);
    ui->statusBar->addPermanentWidget(docSize, 0);
    docPos = new StatusLabel(250);
    ui->statusBar->addPermanentWidget(docPos, 0);
    eolFormat = new StatusLabel(100);
    ui->statusBar->addPermanentWidget(eolFormat, 0);
    unicodeType = new StatusLabel(125);
    ui->statusBar->addPermanentWidget(unicodeType, 0);
    overType = new StatusLabel(25);
    ui->statusBar->addPermanentWidget(overType, 0);

    docType->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(docType, &QLabel::customContextMenuRequested, [=](const QPoint &pos) {
        ui->menuLanguage->popup(docType->mapToGlobal(pos));
    });

    connect(qobject_cast<StatusLabel*>(overType), &StatusLabel::clicked, [=]() {
        bool ot = editor->overtype();
        if (ot)
            overType->setText("INS");
        else
            overType->setText("OVR");
        editor->setOvertype(!ot);
    });

    eolFormat->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(eolFormat, &QLabel::customContextMenuRequested, [=](const QPoint &pos) {
        ui->menuEOLConversion->popup(eolFormat->mapToGlobal(pos));
    });
}

void MainWindow::openFileDialog()
{
    QString filter = getFileDialogFilter();

    QStringList fileNames = QFileDialog::getOpenFileNames(
        this, // parent
        Q_NULLPTR, // caption
        Q_NULLPTR, // dir
        filter, // filter
        Q_NULLPTR, // selected filter
        nullptr // Option flags
        );

    openFileList(fileNames);
}

void MainWindow::openFile(const QString &filePath)
{
    openFileList(QStringList() << filePath);
}

void MainWindow::reloadFile()
{
    auto buffer = tabbedEditor->getCurrentBuffer();

    if (!buffer->isFile() && !buffer->is_save_point()) {
        return;
    }

    auto reply = QMessageBox::question(this, "Reload File", QString("Are you sure you want to reload <b>%1</b>? Any unsaved changes will be lost.").arg(buffer->getName()));

    if (reply == QMessageBox::Yes) {
        bufferManager->reloadBuffer(buffer);
    }
}

void MainWindow::closeCurrentFile()
{
    closeFile(tabbedEditor->currentIndex());
}

void MainWindow::closeFile(int index)
{
    if (isInInitialState()) {
        // Don't close the last file
        return;
    }

    auto buffer = tabbedEditor->getBufferFromIndex(index);
    if(buffer->is_save_point()) {
        bufferManager->closeBuffer(buffer);
    }
    else {
        // The user needs be asked what to do about this file, so switch to it
        tabbedEditor->switchToIndex(index);

        QString message = QString("Save file <b>%1</b>?").arg(buffer->getName());
        auto reply = QMessageBox::question(this, "Save File", message, QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);

        if (reply == QMessageBox::Cancel) {
            return;
        }

        if (reply == QMessageBox::Save) {
            bool didFileGetSaved = saveFile(buffer);

            // The user might have canceled the save file dialog so just stop now
            if (didFileGetSaved == false)
                return;
        }

        bufferManager->closeBuffer(buffer);
    }

}

void MainWindow::closeAllFiles(bool forceClose = false)
{
    if (!forceClose) {
        if (!checkBuffersBeforeClose(0, tabbedEditor->count())) {
            return;
        }
    }

    tabbedEditor->switchToIndex(tabbedEditor->count() - 1);
    for (int index = tabbedEditor->count() - 1; index >= 0; --index) {
        auto buffer = tabbedEditor->getBufferFromIndex(index);
        bufferManager->closeBuffer(buffer);
    }
}

void MainWindow::closeAllExceptActive()
{
    const ScintillaBuffer *currentBuffer = tabbedEditor->getCurrentBuffer();

    int curIndex = tabbedEditor->currentIndex();
    if (!checkBuffersBeforeClose(0, curIndex)) {
        return;
    }
    if (!checkBuffersBeforeClose(curIndex + 1, tabbedEditor->count())) {
        return;
    }

    // Iterate the buffers but skip the one we want to keep
    int i = 0;
    while (tabbedEditor->count() > 1) {
        ScintillaBuffer *buffer = tabbedEditor->getBufferFromIndex(i);
        if (buffer == currentBuffer) {
            i++;
        }
        else {
            bufferManager->closeBuffer(buffer);
        }
    }
}

void MainWindow::closeAllToLeft()
{
    // Save the current index since checkBuffersBeforeClose() might change it
    int curIndex = tabbedEditor->currentIndex();

    if (!checkBuffersBeforeClose(0, curIndex)) {
        return;
    }

    // Restore it now that everything is ready to be closed
    tabbedEditor->switchToIndex(curIndex);

    while (tabbedEditor->currentIndex() > 0) {
        auto buffer = tabbedEditor->getBufferFromIndex(0);
        bufferManager->closeBuffer(buffer);
    }
}

void MainWindow::closeAllToRight()
{
    // Save the current index since checkBuffersBeforeClose() might change it
    int curIndex = tabbedEditor->currentIndex();

    if (!checkBuffersBeforeClose(curIndex, tabbedEditor->count())) {
        return;
    }

    // Restore it now that everything is ready to be closed
    tabbedEditor->switchToIndex(curIndex);

    while (curIndex < tabbedEditor->count() - 1) {
        auto buffer = tabbedEditor->getBufferFromIndex(curIndex + 1);
        bufferManager->closeBuffer(buffer);
    }
}

bool MainWindow::saveCurrentFile()
{
    return saveFile(tabbedEditor->currentIndex());
}

bool MainWindow::saveFile(int index)
{
    auto buffer = tabbedEditor->getBufferFromIndex(index);
    return saveFile(buffer);
}

bool MainWindow::saveFile(ScintillaBuffer *buffer)
{
    if (buffer->is_save_point())
        return true;

    if (!buffer->isFile()) {
        // Switch to the buffer and show the saveas dialog
        tabbedEditor->switchToBuffer(buffer);
        return saveCurrentFileAsDialog();
    }
    else {
        bool didItGetSaved = bufferManager->saveBuffer(buffer);
        if (didItGetSaved) {
            return true;
        }
    }

    return false;
}

QString MainWindow::getFileDialogFilter()
{
    // TODO: cache the result?

    QString filter = app->getLuaState()->executeAndReturn<QString>(
                R"=(
                local filter = {"All files (*)"}
                for name, L in pairs(languages) do
                    local extensions = {}
                    for _, ext in ipairs(L.extensions) do
                        extensions[#extensions + 1] = "*." .. ext
                    end
                    filter[#filter + 1] = L.name .. " Files (" .. table.concat(extensions, " ") .. ")"
                end
                return table.concat(filter, ";;")
                )=");

    return filter;
}

bool MainWindow::saveCurrentFileAsDialog()
{
    QString dialogDir = QString();
    QString filter = getFileDialogFilter();
    auto buffer = tabbedEditor->getCurrentBuffer();

    // Use the file path if possible
    if (buffer->isFile()) {
        dialogDir = buffer->fileInfo.canonicalFilePath();
    }

    QString fileName = QFileDialog::getSaveFileName(
        this, // parent
        Q_NULLPTR, // caption
        dialogDir, // dir
        filter, // filter
        Q_NULLPTR, // selected filter
        nullptr // Option flags
        );

    if (fileName.size() == 0) {
        return false;
    }

    // TODO: distinguish between the above case (i.e. the user cancels the dialog) and a failure
    // calling bufferManager->saveBufferAs() as it might fail.

    return saveFileAs(buffer, fileName);
}

bool MainWindow::saveCurrentFileAs(const QString &fileName)
{
    return saveFileAs(tabbedEditor->currentIndex(), fileName);
}

bool MainWindow::saveFileAs(int index, const QString &fileName)
{
    auto buffer = tabbedEditor->getBufferFromIndex(index);
    return saveFileAs(buffer, fileName);
}

bool MainWindow::saveFileAs(ScintillaBuffer *buffer, const QString &fileName)
{
    qInfo("saveFileAs(%s)", qUtf8Printable(fileName));

    bool didItGetSaved = bufferManager->saveBufferAs(buffer, fileName);

    return didItGetSaved;
}

void MainWindow::saveCopyAsDialog()
{
    QString dialogDir = QString();
    QString filter = getFileDialogFilter();
    auto buffer = tabbedEditor->getCurrentBuffer();

    // Use the file path if possible
    if (buffer->isFile()) {
        dialogDir = buffer->fileInfo.canonicalFilePath();
    }

    QString fileName = QFileDialog::getSaveFileName(
        this, // parent
        Q_NULLPTR, // caption
        dialogDir, // dir
        filter, // filter
        Q_NULLPTR, // selected filter
        nullptr // Option flags
        );

    saveCopyAs(fileName);
}

void MainWindow::saveCopyAs(const QString &fileName)
{
    auto buffer = tabbedEditor->getCurrentBuffer();
    bufferManager->saveBufferCopyAs(buffer, fileName);
}

void MainWindow::saveAll()
{
    for(int i = 0; i < tabbedEditor->count(); ++i) {
        saveFile(i);
    }
}

void MainWindow::renameFile()
{
    auto buffer = tabbedEditor->getCurrentBuffer();

    Q_ASSERT(buffer->isFile());

    QString fileName = QFileDialog::getSaveFileName(this, "", buffer->fileInfo.canonicalFilePath());

    if (fileName.size() == 0) {
        return;
    }

    // The new fileName might be to one of the existing buffers.
    auto otherBuffer = bufferManager->getBufferByFilePath(fileName);

    bool renameSuccessful = bufferManager->renameBuffer(buffer, fileName);

    if (renameSuccessful && otherBuffer) {
        bufferManager->closeBuffer(otherBuffer);
    }
}

void MainWindow::convertEOLs(int eolMode)
{
    editor->convertEOLs(eolMode);
    editor->setEOLMode(eolMode);
    updateEOLBasedUi();
}

void MainWindow::updateBufferFileStatusBasedUi(ScintillaBuffer *buffer)
{
    QString title = QString("[*]%1 - Notepad Next").arg(buffer->getName());
    setWindowTitle(title);

    ui->actionReload->setEnabled(buffer->type() == ScintillaBuffer::File);

    ui->actionRename->setEnabled(buffer->isFile());
    ui->actionCopyFullPath->setEnabled(buffer->isFile());
    ui->actionCopyFileDirectory->setEnabled(buffer->isFile());
}

bool MainWindow::isAnyUnsaved()
{
    for(int i = 0; i < tabbedEditor->count(); ++i) {
        if (!tabbedEditor->getBufferFromIndex(i)->is_save_point()) {
            return true;
        }
    }

    return false;
}

void MainWindow::updateEOLBasedUi()
{
    switch(editor->eOLMode()) {
    case SC_EOL_CR:
        eolFormat->setText(ui->actionMacintosh->text());
        ui->actionMacintosh->setChecked(true);
        break;
    case SC_EOL_CRLF:
        eolFormat->setText(ui->actionWindows->text());
        ui->actionWindows->setChecked(true);
        break;
    case SC_EOL_LF:
        eolFormat->setText(ui->actionUnix->text());
        ui->actionUnix->setChecked(true);
        break;
    }
}

void MainWindow::updateEncodingBasedUi()
{
    switch(editor->codePage()) {
    case 0:
        unicodeType->setText("ANSI");
        break;
    case SC_CP_UTF8:
        unicodeType->setText("UTF-8");
        break;
    default:
        unicodeType->setText(QString::number(editor->codePage()));
        break;
    }
}

void MainWindow::updateSaveStatusBasedUi(bool isDirty)
{
    setWindowModified(isDirty);

    ui->actionSave->setEnabled(isDirty);
    ui->actionSaveAll->setEnabled(isDirty || isAnyUnsaved());

    if (tabbedEditor->count() == 1) {
        ScintillaBuffer *buffer = tabbedEditor->getCurrentBuffer();
        bool ableToClose = buffer->isFile() || isDirty;
        ui->actionClose->setEnabled(ableToClose);
        ui->actionCloseAll->setEnabled(ableToClose);
    }
    else {
        ui->actionClose->setEnabled(true);
        ui->actionCloseAll->setEnabled(true);
    }
}

void MainWindow::updateBufferPositionBasedUi()
{
    int curIndex = tabbedEditor->currentIndex();
    ui->actionCloseAllToLeft->setEnabled(curIndex != 0);
    ui->actionCloseAllToRight->setEnabled(curIndex != tabbedEditor->count() - 1);
    ui->actionCloseAllExceptActive->setEnabled(tabbedEditor->count() > 1);
}

void MainWindow::updateGui(ScintillaBuffer *buffer)
{
    // TODO: Does buffer need passed in? It should only ever operate on the current buffer!
    // buffer = tabbedEditor->getCurrentBuffer();
    editor->setEOLMode(buffer->get_eol_mode());

    updateBufferFileStatusBasedUi(buffer);
    updateSaveStatusBasedUi(!buffer->is_save_point());
    updateEOLBasedUi();
    updateEncodingBasedUi();
    updateBufferPositionBasedUi();

    setLanguage(buffer);

    foreach (QAction *action, languageActionGroup->actions()) {
        if (action->data().toString() == buffer->lexer) {
            action->setChecked(true);
            break;
        }
    }
}

void MainWindow::updateSelectionBasedUi(int updated)
{
    if (updated & SC_UPDATE_CONTENT) {
        ui->actionUndo->setEnabled(editor->canUndo());
        ui->actionRedo->setEnabled(editor->canRedo());

        QString sizeText = QString("Length: %1    Lines: %2").arg(
                QLocale::system().toString(editor->length()),
                QLocale::system().toString(editor->lineCount()));
        docSize->setText(sizeText);
    }

    if (updated & (SC_UPDATE_CONTENT | SC_UPDATE_SELECTION)) {
        bool hasAnySelections = !editor->selectionEmpty();

        ui->actionCut->setEnabled(hasAnySelections);
        ui->actionCopy->setEnabled(hasAnySelections);
        ui->actionDelete->setEnabled(hasAnySelections);
        ui->actionPaste->setEnabled(editor->canPaste());

        ui->actionLowerCase->setEnabled(hasAnySelections);
        ui->actionUpperCase->setEnabled(hasAnySelections);

        QString selectionText;
        if (editor->selections() > 1) {
            selectionText = "Sel: N/A";
        }
        else {
            int start = editor->selectionStart();
            int end = editor->selectionEnd();
            int lines = editor->lineFromPosition(end) - editor->lineFromPosition(start);

            if (end > start)
                lines++;

            selectionText = QString("Sel: %1 | %2").arg(
                    QLocale::system().toString(editor->countCharacters(start, end)),
                    QLocale::system().toString(lines));
        }
        int pos = editor->currentPos();
        QString positionText = QString("Ln: %1    Col: %2    ").arg(
                QLocale::system().toString(editor->lineFromPosition(pos) + 1),
                QLocale::system().toString(editor->column(pos) + 1));
        docPos->setText(positionText + selectionText);
    }
}

void MainWindow::detectLanguageFromExtension(ScintillaBuffer *buffer)
{
    qInfo(Q_FUNC_INFO);

    // Only real files have extensions
    if (!buffer->isFile()) {
        buffer->lexer = "null";
        return;
    }

    // See if it already has a language
    if (buffer->lexer != "null")
        return;

    const QString ext = buffer->fileInfo.suffix();

    QString lexer = app->getLuaState()->executeAndReturn<QString>(QString(R"(
local ext = "%1"
for name, L in pairs(languages) do
    for _, v in ipairs(L.extensions) do
        if v == ext then
            return L.name
        end
    end
end
return "null"
)").arg(ext).toLatin1().constData());

    buffer->lexer = lexer;

    return;
}

void MainWindow::bufferActivated(ScintillaBuffer *buffer)
{
    checkBufferForModification(buffer);
    updateGui(buffer);
}

void MainWindow::setLanguage(ScintillaBuffer *buffer)
{
    qInfo(Q_FUNC_INFO);

    if (true) {
        docType->setText(buffer->lexer);
        if (buffer->lexer == "null") {
            editor->setLexer(SCLEX_NULL);
        }
        else {
            setupEditor(editor);

            app->getLuaState()->execute(QString("lexer = \"%1\"").arg(buffer->lexer).toLatin1().constData());
            app->getLuaState()->execute(R"(
                local L = languages[lexer]
                -- this resets the style definitions but keeps
                -- the "wanted" stuff, such as line numbers, etc
                -- resetEditorStyle()
                editor.LexerLanguage = L.lexer

                editor.UseTabs = (L.tabSettings or "tabs") == "tabs"
                editor.TabWidth = L.tabSize or 4
                if L.styles then
                    for name, style in pairs(L.styles) do
                        editor.StyleFore[style.id] = style.fgColor
                        editor.StyleBack[style.id] = style.bgColor
                        if style.fontStyle then
                            if style.fontStyle & 1 == 1 then
                                editor.StyleBold[style.id] = true
                            end
                            if style.fontStyle & 2 == 2 then
                                editor.StyleItalic[style.id] = true
                            end
                        end
                    end
                end
                if L.keywords then
                    for id, kw in pairs(L.keywords) do
                        editor.KeyWords[id] = kw
                    end
                end
                if L.properties then
                    for p,v in pairs(L.properties) do
                        editor.Property[p] = v
                    end
                end
                editor.Property["fold"] = "1"
                editor.Property["fold.compact"] = "0"

                -- The document needs redone, but don't force it to do the whole thing
                -- since SC_IDLESTYLING_TOVISIBLE is used
                editor:Colourise(0, 1);
                )");
        }
    }
    else {
        //buffer->lexer = language.name;
        //docType->setText(language.longDescription);
        //editor->setLexer(language.lexer_id); // TODO: get this from somewhere
        //foreach (LexerStyle ls, language.lexer.styles) {
        //    editor->styleSetFore(ls.id, ls.fgColor);
        //    editor->styleSetBack(ls.id, ls.bgColor);
        //    if (ls.keywordClass != -1) {
        //        editor->setKeyWords(ls.keywordClass, language.keywords[ls.keywordClass].toLocal8Bit().constData());
        //    }
        //}
        //editor->setProperty("fold", "1");
        //editor->setProperty("fold.compact", "0");
        //editor->setProperty("lexer.cpp.track.preprocessor", "0");
    }
}

void MainWindow::bringWindowToForeground()
{
    qInfo(Q_FUNC_INFO);

    //setWindowState(windowState() & ~Qt::WindowMinimized);
    //raise();
    //activateWindow();

    // Make sure the window isn't minimized
    // TODO: this always puts it in the "normal" state but it might have been maximized
    // before minimized...so either a flag needs stored or find a Qt call to do it appropriately
    if (isMinimized())
        showNormal();

#ifdef Q_OS_WIN
    // TODO: there doesn't seem to be a cross platform way to force the window
    // to the foreground. So this will need moved to a platform specific file

    HWND hWnd = reinterpret_cast<HWND>(effectiveWinId());
    if (hWnd) {
        // I have no idea what this does but it works mostly
        // https://www.codeproject.com/Articles/1724/Some-handy-dialog-box-tricks-tips-and-workarounds

        AttachThreadInput(GetWindowThreadProcessId(GetForegroundWindow(), nullptr), GetCurrentThreadId(), TRUE);

        SetForegroundWindow(hWnd);
        SetFocus(hWnd);

        AttachThreadInput(GetWindowThreadProcessId(GetForegroundWindow(), nullptr), GetCurrentThreadId(), FALSE);
    }
#endif
}

bool MainWindow::checkBufferForModification(ScintillaBuffer *buffer)
{
    qInfo(Q_FUNC_INFO);

    auto state = buffer->checkForBufferStateChange();

    if (state == ScintillaBuffer::NoChange) {
        return false;
    }
    else if (state == ScintillaBuffer::Modified) {
        qInfo("ScintillaBuffer::Modified");
        bufferManager->reloadBuffer(buffer);
    }
    else if (state == ScintillaBuffer::Deleted) {
        qInfo("ScintillaBuffer::Deleted");
    }
    else if (state == ScintillaBuffer::Restored) {
        qInfo("ScintillaBuffer::Restored");
    }

    return true;
}

void MainWindow::focusIn()
{
    qInfo(Q_FUNC_INFO);

    if (checkBufferForModification(tabbedEditor->getCurrentBuffer())) {
        updateGui(tabbedEditor->getCurrentBuffer());
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!checkBuffersBeforeClose(0, tabbedEditor->count())) {
        event->ignore();
        return;
    }

    // While tabs are being closed, turn off UI updates so the main window doesn't continuously refresh.
    disconnect(tabbedEditor, &TabbedEditor::bufferSwitched, this, &MainWindow::bufferActivated);
    // Also disable creating a new file.
    disconnect(tabbedEditor, &TabbedEditor::allBuffersClosed, this, &MainWindow::newFile);

    closeAllFiles(true);

    QSettings settings;
    settings.setValue("MainWindow/geometry", saveGeometry());
    settings.setValue("MainWindow/windowState", saveState());
    settings.setValue("App/RecentFilesList", recentFilesListManager->fileList());

    event->accept();

    QMainWindow::closeEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    qInfo(Q_FUNC_INFO);

    // NOTE: for urls these can be dragged anywhere in the application, editor, tabs, menu
    // because the ScintillaNext editor ignores urls so they can be handled by the main
    // application
    // Text dragging within the editor object itself is handled by Scintilla, but if the text
    // is dragged to other parts (tabs, menu, etc) it will be handled by the application to
    // create a new buffer from the text.

    // Accept urls and text
    if (event->mimeData()->hasUrls() || event->mimeData()->hasText()) {
        event->acceptProposedAction();
    }
    else {
        event->ignore();
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    qInfo(Q_FUNC_INFO);

    if (event->mimeData()->hasUrls()) {
        // Get the urls into a stringlist
        QStringList fileNames;
        auto urls = event->mimeData()->urls();
        foreach(QUrl url, urls) {
            if (url.isLocalFile()) {
                fileNames.append(url.toLocalFile());
            }
        }

        openFileList(fileNames);
        bringWindowToForeground();
        event->acceptProposedAction();
    }
    else if (event->mimeData()->hasText()) {
        if (event->source()) {
            // if it is from an editor, remove the text
            ScintillaNext *sn = qobject_cast<ScintillaNext *>(event->source());
            if (sn) {
                sn->replaceSel("");
            }
        }

        newFile();
        editor->setText(event->mimeData()->text().toLocal8Bit().constData());
        bringWindowToForeground();
        event->acceptProposedAction();
    }
    else {
        event->ignore();
    }
}

// TODO: Move this into the editor eventually
void MainWindow::setFoldMarkers(ScintillaNext *editor, const QString &type)
{
    QMap<QString, QList<int>> map{
        {"simple", {SC_MARK_MINUS, SC_MARK_PLUS, SC_MARK_EMPTY, SC_MARK_EMPTY, SC_MARK_EMPTY, SC_MARK_EMPTY, SC_MARK_EMPTY}},
        {"arrow",  {SC_MARK_ARROWDOWN, SC_MARK_ARROW, SC_MARK_EMPTY, SC_MARK_EMPTY, SC_MARK_EMPTY, SC_MARK_EMPTY, SC_MARK_EMPTY}},
        {"circle", {SC_MARK_CIRCLEMINUS, SC_MARK_CIRCLEPLUS, SC_MARK_VLINE, SC_MARK_LCORNERCURVE, SC_MARK_CIRCLEPLUSCONNECTED, SC_MARK_CIRCLEMINUSCONNECTED, SC_MARK_TCORNERCURVE }},
        {"box",    {SC_MARK_BOXMINUS, SC_MARK_BOXPLUS, SC_MARK_VLINE, SC_MARK_LCORNER, SC_MARK_BOXPLUSCONNECTED, SC_MARK_BOXMINUSCONNECTED, SC_MARK_TCORNER }},
    };

    if (!map.contains(type))
        return;

    const auto types = map[type];
    editor->markerDefine(SC_MARKNUM_FOLDEROPEN, types[0]);
    editor->markerDefine(SC_MARKNUM_FOLDER, types[1]);
    editor->markerDefine(SC_MARKNUM_FOLDERSUB, types[2]);
    editor->markerDefine(SC_MARKNUM_FOLDERTAIL, types[3]);
    editor->markerDefine(SC_MARKNUM_FOLDEREND, types[4]);
    editor->markerDefine(SC_MARKNUM_FOLDEROPENMID, types[5]);
    editor->markerDefine(SC_MARKNUM_FOLDERMIDTAIL, types[6]);

    for (int i = SC_MARKNUM_FOLDEREND; i <= SC_MARKNUM_FOLDEROPEN; ++i) {
        editor->markerSetFore(i, 0xF3F3F3);
        editor->markerSetBack(i, 0x808080);
        editor->markerSetBackSelected(i, 0x0000FF);
    }
}

void MainWindow::tabBarRightClicked(const QPoint &pos)
{
    // Check to see if an actual tab was clicked
    int index = tabbedEditor->getTabBar()->tabAt(pos);
    if (index == TabbedEditor::INVALID_INDEX) {
        return;
    }

    // Focus on the correct tab
    tabbedEditor->switchToIndex(index);

    // Create the menu and show it
    QMenu *menu = new QMenu(this);
    menu->addAction(ui->actionClose);
    menu->addAction(ui->actionCloseAllExceptActive);
    menu->addAction(ui->actionCloseAllToLeft);
    menu->addAction(ui->actionCloseAllToRight);
    menu->addAction(ui->actionSave);
    menu->addAction(ui->actionSaveAs);
    menu->addAction(ui->actionRename);
    menu->addAction(ui->actionReload);
    menu->addSeparator();
    menu->addAction(ui->actionCopyFullPath);
    menu->addAction(ui->actionCopyFileName);
    menu->addAction(ui->actionCopyFileDirectory);
    menu->popup(tabbedEditor->mapToGlobal(pos));
}

void MainWindow::languageMenuTriggered()
{
    const QAction *act = qobject_cast<QAction *>(sender());
    QVariant v = act->data();

    auto buffer = tabbedEditor->getCurrentBuffer();
    buffer->lexer = v.toString();

    setLanguage(buffer);

    qInfo(qUtf8Printable(buffer->lexer));
}