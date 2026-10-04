// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "MainWindow.h"
#include "AudioPlayer.h"
#include "AppSettings.h"
#include "AutoCaptions.h"
#include "CardDelegate.h"
#include "ExportDialog.h"
#include "HelpWindow.h"
#include "Icons.h"
#include "SettingsDialog.h"
#include "SubtitleFile.h"
#include "SubtitlePanel.h"
#include "Theme.h"
#include "UpdateChecker.h"
#include "WelcomeScreen.h"
#include "MediaBin.h"
#include "PreviewRenderer.h"
#include "PreviewWidget.h"
#include "TimelineWidget.h"
#include "ClipPresets.h"
#include "LibraryPanel.h"
#include "TitleRenderer.h"
#include "ClipInspector.h"
#include "ProjectFile.h"

#include <ve/engine.h>

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QListWidgetItem>
#include <QLockFile>
#include <QUuid>
#include <QMimeData>
#include <QPointer>
#include <QThreadPool>
#if defined(Q_OS_LINUX)
#include <QDBusConnection>
#include <QDBusMessage>
#endif
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QPropertyAnimation>
#include <QProgressDialog>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QSettings>
#include <QSplitter>
#include <QTabWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <atomic>
#include <tuple>
#include <memory>
#include <vector>

namespace {

constexpr int ThumbW = 144;
constexpr int ThumbH = 81;

QString formatDuration(double seconds)
{
    int total = int(seconds + 0.5);
    int h = total / 3600;
    int m = (total / 60) % 60;
    int s = total % 60;
    if (h > 0)
        return QString::asprintf("%d:%02d:%02d", h, m, s);
    return QString::asprintf("%d:%02d", m, s);
}

// 01:02:03.45 style, for the clock under the preview
// hours = show the hours too (only worth it for videos an hour or longer)
QString formatClock(double seconds, bool hours = true)
{
    int cs = int(seconds * 100 + 0.5);
    if (!hours)
        return QString::asprintf("%02d:%02d.%02d", cs / 6000, (cs / 100) % 60, cs % 100);
    return QString::asprintf("%02d:%02d:%02d.%02d", cs / 360000, (cs / 6000) % 60, (cs / 100) % 60, cs % 100);
}

// Opens the file manager with the file highlighted.
void showInFileManager(const QString& file)
{
#if defined(Q_OS_WIN)
    if (QProcess::startDetached("explorer.exe", { "/select,", QDir::toNativeSeparators(file) }))
        return;
#elif defined(Q_OS_LINUX)
    // Ask over D-Bus: inside the toolbox there's no xdg-open, but the desktop's file manager still listens
    QDBusMessage call = QDBusMessage::createMethodCall(
        "org.freedesktop.FileManager1", "/org/freedesktop/FileManager1",
        "org.freedesktop.FileManager1", "ShowItems");
    call << QStringList { QUrl::fromLocalFile(file).toString() } << QString();
    QDBusMessage reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 5000);
    if (reply.type() != QDBusMessage::ErrorMessage)
        return;
#endif
    // Plan B: just open the folder
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(file).absolutePath()));
}

// Draws the thumbnail (or a music note for audio files) centred on a black card.
// withPicture = false makes a quick placeholder; true decodes a frame (do that off the main thread).
QImage makeThumbnail(const QString& path, const ve_media_info& info, bool withPicture)
{
    QImage card(ThumbW, ThumbH, QImage::Format_RGB32);
    card.fill(QColor(0x0e, 0x0f, 0x10));
    QPainter p(&card);

    if (info.has_video && !withPicture)
        return card; // the real picture is on its way
    if (info.has_video) {
        std::vector<uint8_t> pixels(ThumbW * ThumbH * 4);
        int w = 0, h = 0;
        if (ve_thumbnail(path.toUtf8().constData(), ThumbW, ThumbH, pixels.data(), &w, &h) == VE_OK) {
            QImage img(pixels.data(), w, h, w * 4, QImage::Format_RGBA8888);
            p.drawImage((ThumbW - w) / 2, (ThumbH - h) / 2, img);
            return card;
        }
    }

    p.setPen(QColor(0x2f, 0xc6, 0xb4));
    QFont font = p.font();
    font.setPixelSize(40);
    p.setFont(font);
    p.drawText(card.rect(), Qt::AlignCenter, info.has_audio ? QStringLiteral("♪") : QStringLiteral("?"));
    return card;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_renderer(new PreviewRenderer(this))
    , m_audio(new AudioPlayer(this))
    , m_playTimer(new QTimer(this))
{
    m_timeline = new TimelineWidget;
    m_inspector = new ClipInspector;
    m_inspector->setMinimumWidth(350);

    auto* top = m_top = new QSplitter(Qt::Horizontal);
    // Left side: tabs for media and transitions
    auto* library = m_library = new QTabWidget;
    library->setMinimumWidth(220);
    library->setDocumentMode(true);
    library->addTab(buildMediaPanel(), "Media");
    // Ready-made blocks for the FX tracks: drag them on, or double-click to drop one at the playhead
    struct Tab {
        const char* name;
        QList<TimelineClip> items;
    };
    for (const Tab& tab : { Tab { "Titles", titlePresets() }, Tab { "Effects", effectPresets() },
                            Tab { "Transitions", transitionPresets() } }) {
        auto* panel = new LibraryPanel(tab.items);
        library->addTab(panel, tab.name);
        connect(panel, &LibraryPanel::addAtPlayhead, m_timeline, &TimelineWidget::addAtPlayhead);
    }
    m_subtitles = new SubtitlePanel(m_timeline);
    library->addTab(m_subtitles, "Subtitles");
    top->addWidget(library);
    m_previewPanel = buildPreviewPanel();
    top->addWidget(m_previewPanel);
    top->setCollapsible(1, false); // (the preview never disappears)
    top->addWidget(m_inspector);
    top->setStretchFactor(0, 2);
    top->setStretchFactor(1, 4);
    top->setStretchFactor(2, 1);

    auto* main = m_main = new QSplitter(Qt::Vertical);
    main->addWidget(top);
    main->addWidget(m_timeline);
    main->setStretchFactor(0, 3);
    main->setStretchFactor(1, 2);
    // A little breathing room between the panels
    top->setHandleWidth(6);
    main->setHandleWidth(6);
    main->setContentsMargins(6, 6, 6, 0);
    setCentralWidget(main);

    buildActions();

    connect(m_timeline, &TimelineWidget::clipsChanged, this, &MainWindow::onClipsChanged);
    connect(m_timeline, &TimelineWidget::selectionChanged, this, &MainWindow::refreshInspector);
    connect(m_inspector, &ClipInspector::edited, m_timeline, &TimelineWidget::updateClip);
    connect(m_inspector, &ClipInspector::speedChanged, m_timeline, &TimelineWidget::setClipSpeed);
    connect(m_inspector, &ClipInspector::detachAudioClicked, m_timeline, &TimelineWidget::detachAudio);
    connect(m_inspector, &ClipInspector::seekRequested, this, &MainWindow::seekTo);
    connect(m_subtitles, &SubtitlePanel::seekRequested, this, &MainWindow::seekTo);
    connect(m_subtitles, &SubtitlePanel::importClicked, this, &MainWindow::importSubtitles);
    connect(m_subtitles, &SubtitlePanel::exportClicked, this, &MainWindow::exportSubtitles);
    connect(m_subtitles, &SubtitlePanel::autoCaptionsClicked, this, &MainWindow::autoCaptions);
    connect(m_inspector, &ClipInspector::pickSpotRequested, this, [this] {
        stopPlayback();
        m_preview->pickSpot();
    });
    connect(m_preview, &PreviewWidget::picked, m_inspector, &ClipInspector::setSpot);
    connect(m_inspector, &ClipInspector::pickColorRequested, this, [this] {
        stopPlayback();
        m_preview->pickColor();
    });
    connect(m_preview, &PreviewWidget::colorPicked, m_inspector, &ClipInspector::setKeyColor);
    connect(m_inspector, &ClipInspector::reverseChanged, m_timeline, &TimelineWidget::setReverse);
    connect(m_inspector, &ClipInspector::evenOutVolumeClicked, this, &MainWindow::evenOutVolume);
    connect(m_inspector, &ClipInspector::freezeFrameClicked, m_timeline, [this] { m_timeline->freezeFrame(); });
    connect(m_timeline, &TimelineWidget::filesDropped, this, &MainWindow::onFilesDroppedOnTimeline);
    setAcceptDrops(true); // drag files in from the file manager
    connect(m_timeline, &TimelineWidget::playheadMoved, this, &MainWindow::onPlayheadMoved);
    connect(m_renderer, &PreviewRenderer::frameReady, m_preview, [this](const QImage& frame, double) {
        m_preview->setFrame(frame);
    });
    connect(m_preview, &PreviewWidget::resized, this, &MainWindow::requestPreview);

    m_playTimer->setTimerType(Qt::PreciseTimer);
    m_playTimer->setInterval(16);
    connect(m_playTimer, &QTimer::timeout, this, &MainWindow::onTick);

    // Auto-save every minute (MIXMEDIA_AUTOSAVE_SECONDS changes that, handy for testing)
    m_autoSaveId = QUuid::createUuid().toString(QUuid::Id128).left(12);
    QDir().mkpath(autoSaveFolder());
    m_autoSaveLock = std::make_unique<QLockFile>(QDir(autoSaveFolder()).filePath(m_autoSaveId + ".lock"));
    m_autoSaveLock->tryLock(0);
    m_autoSaveTimer = new QTimer(this);
    int every = qEnvironmentVariableIntValue("MIXMEDIA_AUTOSAVE_SECONDS");
    m_autoSaveTimer->setInterval((every > 0 ? every : 60) * 1000);
    connect(m_autoSaveTimer, &QTimer::timeout, this, &MainWindow::autoSave);
    m_autoSaveTimer->start();

    updateTimeLabel();
    updateWindowTitle();
    statusBar()->showMessage(QString("Engine v%1 · Ready").arg(ve_version()));
}

MainWindow::~MainWindow()
{
    stopPlayback();
    clearAutoSave(); // closing normally: nothing to recover
}

// ---- Auto-save ----

QString MainWindow::autoSaveFolder()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("autosave");
}

void MainWindow::autoSave()
{
    if (!m_dirty || m_timeline->clips().isEmpty())
        return;
    QDir dir(autoSaveFolder());
    QString error;
    if (!ProjectFile::save(dir.filePath(m_autoSaveId + ".mixmedia"), projectData(), &error))
        return; // not worth interrupting anyone over, we'll try again next time
    // Remember which project it belongs to, so a recovered copy saves back to the right place
    QFile where(dir.filePath(m_autoSaveId + ".txt"));
    if (where.open(QIODevice::WriteOnly | QIODevice::Truncate))
        where.write(m_projectPath.toUtf8());
}

void MainWindow::clearAutoSave()
{
    QDir dir(autoSaveFolder());
    QFile::remove(dir.filePath(m_autoSaveId + ".mixmedia"));
    QFile::remove(dir.filePath(m_autoSaveId + ".txt"));
}

bool MainWindow::offerRecovery()
{
    // Auto-saves whose lock nobody holds any more belong to a MixMedia that didn't close properly
    QDir dir(autoSaveFolder());
    QFileInfo newest;
    QStringList orphans;
    for (const QFileInfo& f : dir.entryInfoList({ "*.mixmedia" }, QDir::Files, QDir::Time)) {
        QString id = f.completeBaseName();
        if (id == m_autoSaveId)
            continue;
        QLockFile lock(dir.filePath(id + ".lock"));
        if (!lock.tryLock(0))
            continue; // another MixMedia that's still open
        lock.unlock();
        orphans << id;
        if (!newest.exists())
            newest = f;
    }
    if (orphans.isEmpty())
        return false;

    auto forget = [&] {
        for (const QString& id : orphans)
            for (const char* ext : { ".mixmedia", ".txt", ".lock" })
                QFile::remove(dir.filePath(id + ext));
    };
    QString original;
    {
        // (closed again straight away: Windows won't delete a file that's still open)
        QFile where(dir.filePath(newest.completeBaseName() + ".txt"));
        if (where.open(QIODevice::ReadOnly))
            original = QString::fromUtf8(where.readAll());
    }
    QString name = original.isEmpty() ? QStringLiteral("an unsaved project") : QFileInfo(original).fileName();

    auto answer = QMessageBox::question(
        this, "Get your work back?",
        QString("MixMedia didn't close properly last time. There's an auto-saved copy of %1 from %2.\n\n"
                "Open it?")
            .arg(name, QLocale().toString(newest.lastModified(), QLocale::ShortFormat)),
        QMessageBox::Open | QMessageBox::Discard, QMessageBox::Open);
    bool recovered = false;
    if (answer == QMessageBox::Open && loadProject(newest.filePath())) {
        // It's still unsaved work: point it back at the real project and leave it marked as changed
        m_projectPath = original;
        setDirty(true);
        statusBar()->showMessage("Recovered your auto-saved work. Save it to keep it!", 8000);
        recovered = true;
    }
    forget();
    return recovered;
}

// ---- Welcome & recent projects ----

QStringList MainWindow::recentProjects()
{
    return QSettings().value("recent/projects").toStringList();
}

void MainWindow::rememberRecent(const QString& path)
{
    if (path.isEmpty() || path.startsWith(autoSaveFolder()))
        return; // (auto-saves aren't projects you'd pick)
    QString full = QFileInfo(path).absoluteFilePath();
    QStringList list = recentProjects();
    list.removeAll(full);
    list.prepend(full);
    QSettings().setValue("recent/projects", list.mid(0, 10));
}

void MainWindow::showWelcome(bool always)
{
    if (!always && !WelcomeScreen::showAtStartup())
        return;
    WelcomeScreen welcome(recentProjects(), this);
    // A gentle fade in
    auto* fade = new QPropertyAnimation(&welcome, "windowOpacity", &welcome);
    welcome.setWindowOpacity(0.0);
    fade->setDuration(220);
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    fade->setEasingCurve(QEasingCurve::OutCubic);
    QTimer::singleShot(0, fade, [fade] { fade->start(); });
    welcome.exec();
    switch (welcome.choice()) {
    case WelcomeScreen::Choice::Open: openProject(); break;
    case WelcomeScreen::Choice::Import: importMedia(); break;
    case WelcomeScreen::Choice::Recent: loadProject(welcome.recentPath()); break;
    default: break; // new (or closed it): the empty project's already there
    }
}

// ---- Building the window ----

void MainWindow::buildActions()
{
    // Each action lives in both the menu bar and (some of them) the toolbar
    auto make = [&](const QString& text, const QList<QKeySequence>& keys, const QString& tip, auto slot) {
        auto* a = new QAction(text, this);
        a->setShortcuts(keys);
        QString keysText = keys.isEmpty() ? QString() : QString(" (%1)").arg(keys.first().toString(QKeySequence::NativeText));
        a->setToolTip(tip + keysText);
        connect(a, &QAction::triggered, this, slot);
        return a;
    };

    QAction* newProject = make("&New project", { QKeySequence::New }, "Start a fresh project", &MainWindow::newProject);
    QAction* open = make("&Open project…", { QKeySequence::Open }, "Open a saved project", &MainWindow::openProject);
    QAction* save = make("&Save project", { QKeySequence::Save }, "Save the project", &MainWindow::saveProject);
    QAction* saveAs = make("Save project &as…", { QKeySequence("Ctrl+Shift+S") }, "Save the project somewhere new", &MainWindow::saveProjectAs);
    QAction* import = make("&Import media…", { QKeySequence("Ctrl+I") }, "Import media", &MainWindow::importMedia);
    QAction* exportAction = make("&Export…", { QKeySequence("Ctrl+E") }, "Export to MP4", &MainWindow::exportVideo);
    QAction* quit = make("&Quit", { QKeySequence::Quit }, "Quit", &QWidget::close);
    QAction* importSubs = make("Import s&ubtitles (.srt, .vtt)…", {}, "Bring in a subtitle file", &MainWindow::importSubtitles);
    QAction* exportSubs = make("Export subtitles (.s&rt)…", {}, "Save the subtitles as an .srt file", &MainWindow::exportSubtitles);
    QAction* settings = make("Project se&ttings…", {}, "The finished video's shape, size and frame rate", &MainWindow::editProjectSettings);

    QAction* undo = make("&Undo", { QKeySequence::Undo }, "Undo", [this] { m_timeline->undo(); });
    QAction* redo = make("&Redo", { QKeySequence("Ctrl+Shift+Z"), QKeySequence("Ctrl+Y") }, "Redo", [this] { m_timeline->redo(); });
    QAction* copy = make("&Copy", { QKeySequence::Copy }, "Copy the selected clips", [this] { m_timeline->copySelected(); });
    QAction* cut = make("Cu&t", { QKeySequence::Cut }, "Cut the selected clips", [this] { m_timeline->cutSelected(); });
    QAction* paste = make("&Paste", { QKeySequence::Paste }, "Paste at the playhead", [this] { m_timeline->paste(); });
    QAction* settingsWindow = make("Se&ttings…", { QKeySequence::Preferences }, "How much of the computer MixMedia may use",
                                   [this] { SettingsDialog(this).exec(); });
    QAction* selectAll = make("Select &all", { QKeySequence::SelectAll }, "Select every clip", [this] { m_timeline->selectAll(); });
    QAction* captions = make("Auto-&captions…", {}, "Listen to the video and write the subtitles", &MainWindow::autoCaptions);
    QAction* freeze = make("&Freeze frame", { QKeySequence("Ctrl+Shift+F") }, "Hold the frame under the playhead for 2 seconds",
                           [this] { m_timeline->freezeFrame(); });
    QAction* split = make("S&plit", { QKeySequence("S"), QKeySequence("Ctrl+B") }, "Split at the playhead", [this] { m_timeline->splitAtPlayhead(); });
    QAction* del = make("&Delete", {}, "Delete the selected clip and close the gap (Delete key)", [this] { m_timeline->deleteSelected(); });
    QAction* delGap = make("Delete, &leaving a gap", {}, "Delete the selected clip but leave the space empty (Shift+Delete)", [this] { m_timeline->deleteSelectedKeepGap(); });
    QAction* detach = make("Detach &audio", { QKeySequence("Ctrl+D") }, "Put the selected clip's sound on its own track", [this] { m_timeline->detachAudio(); });
    QAction* title = make("Add &title", { QKeySequence("Ctrl+T") }, "Add a title at the playhead", [this] { m_timeline->addTitle(); });

    QAction* zoomOut = make("Zoom −", { QKeySequence("Ctrl+-") }, "Zoom out", [this] { m_timeline->zoomBy(0.8); });
    QAction* zoomIn = make("Zoom +", { QKeySequence("Ctrl+=") }, "Zoom in", [this] { m_timeline->zoomBy(1.25); });
    QAction* fit = make("Fit", { QKeySequence("Ctrl+0") }, "Fit the whole timeline", [this] { m_timeline->zoomToFit(); });

    QMenu* file = menuBar()->addMenu("&File");
    file->addActions({ newProject, open });
    // Open recent: filled in fresh each time it's opened
    m_recentMenu = file->addMenu("Open &recent");
    connect(m_recentMenu, &QMenu::aboutToShow, this, [this] {
        m_recentMenu->clear();
        for (const QString& path : recentProjects()) {
            if (!QFileInfo::exists(path))
                continue;
            m_recentMenu->addAction(QFileInfo(path).completeBaseName(), this, [this, path] {
                if (maybeSave())
                    loadProject(path);
            })->setToolTip(path);
        }
        if (m_recentMenu->isEmpty())
            m_recentMenu->addAction("(nothing yet)")->setEnabled(false);
        m_recentMenu->addSeparator();
        m_recentMenu->addAction("Show the welcome screen", this, [this] {
            if (maybeSave())
                showWelcome(true);
        });
    });
    file->addActions({ save, saveAs });
    file->addSeparator();
    file->addActions({ import, exportAction });
    file->addActions({ importSubs, exportSubs });
    file->addAction(settings);
    file->addSeparator();
    file->addAction(quit);
    QMenu* edit = menuBar()->addMenu("&Edit");
    edit->addActions({ undo, redo });
    edit->addSeparator();
    edit->addActions({ cut, copy, paste, selectAll });
    edit->addSeparator();
    edit->addActions({ split, del, delGap, detach, freeze, title, captions });
    edit->addSeparator();
    edit->addAction(settingsWindow);
    QMenu* view = menuBar()->addMenu("&View");
    view->addActions({ zoomIn, zoomOut, fit });
    view->addSeparator();
    // The panels: hide the ones you don't need for a bigger preview and timeline
    m_showLibrary = view->addAction("Show the &library", this, [this](bool on) { m_library->setVisible(on); });
    m_showProperties = view->addAction("Show &properties", this, [this](bool on) { m_inspector->setVisible(on); });
    for (QAction* a : { m_showLibrary, m_showProperties }) {
        a->setCheckable(true);
        a->setChecked(true);
    }
    m_focusPreview = view->addAction("&Focus on the preview", this, [this](bool on) {
        // Both side panels away (and back again)
        m_showLibrary->setChecked(!on);
        m_showProperties->setChecked(!on);
        m_library->setVisible(!on);
        m_inspector->setVisible(!on);
    });
    m_focusPreview->setCheckable(true);
    m_fullScreen = view->addAction("F&ull screen", this, [this](bool on) {
        if (on) {
            m_wasMaximized = isMaximized();
            showFullScreen();
        } else if (m_wasMaximized) {
            showMaximized();
        } else {
            showNormal();
        }
    });
    m_fullScreen->setCheckable(true);
    m_fullScreen->setShortcut(Qt::Key_F11); // (the standard one is Ctrl+Shift+F on KDE, which is freeze frame)
    view->addSeparator();
    view->addAction("&Reset the layout", this, &MainWindow::resetLayout);
    // Getting around. These work from anywhere in the window.
    QMenu* playback = menuBar()->addMenu("&Playback");
    playback->addAction(make("&Play / pause", { QKeySequence(Qt::Key_Space) }, "Play or pause", &MainWindow::togglePlay));
    playback->addSeparator();
    playback->addAction(make("Back one frame", { QKeySequence(Qt::Key_Left) }, "Step back a frame", [this] { stepFrames(-1); }));
    playback->addAction(make("Forward one frame", { QKeySequence(Qt::Key_Right) }, "Step forward a frame", [this] { stepFrames(1); }));
    playback->addAction(make("Back one second", { QKeySequence("Shift+Left") }, "Jump back a second", [this] { seekBy(-1.0); }));
    playback->addAction(make("Forward one second", { QKeySequence("Shift+Right") }, "Jump forward a second", [this] { seekBy(1.0); }));
    playback->addSeparator();
    playback->addAction(make("Previous cut", { QKeySequence(Qt::Key_Up) }, "Jump to the previous cut", [this] { jumpToCut(-1); }));
    playback->addAction(make("Next cut", { QKeySequence(Qt::Key_Down) }, "Jump to the next cut", [this] { jumpToCut(1); }));
    playback->addAction(make("Go to start", { QKeySequence(Qt::Key_Home) }, "Go to the start", &MainWindow::goToStart));
    playback->addAction(make("Go to end", { QKeySequence(Qt::Key_End) }, "Go to the end", [this] { seekTo(m_timeline->duration()); }));

    QMenu* help = menuBar()->addMenu("&Help");
    QAction* helpPages = make("MixMedia &help", { QKeySequence::HelpContents }, "How everything works",
                              [] { HelpWindow::open(); });
    help->addAction(helpPages);
    help->addAction(make("&Keyboard shortcuts", {}, "Every shortcut in one place", [] { HelpWindow::open("shortcuts.md"); }));
    help->addAction(make("Check for &updates…", {}, "See if there's a newer MixMedia", [this] { UpdateChecker::checkNow(this); }));
    help->addSeparator();
    help->addAction(make("&About MixMedia", {}, "Who made this, and the licence", &MainWindow::showAbout));
    help->addAction(make("About &Qt", {}, "About the Qt toolkit", [] { QApplication::aboutQt(); }));

    QToolBar* bar = m_toolbar = addToolBar("Main");
    bar->setMovable(false);
    bar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    bar->addAction(import);
    bar->addSeparator();
    bar->addActions({ undo, redo });
    bar->addSeparator();
    bar->addActions({ split, del, detach, title });
    bar->addSeparator();
    bar->addActions({ zoomOut, zoomIn, fit });
    // Icons beside the words
    bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    // Icons as big as the text (bigger system fonts get bigger icons)
    const int iconSize = std::max(16, int(QFontMetrics(Theme::font()).height() * 0.95));
    bar->setIconSize(QSize(iconSize, iconSize));
    const QList<QPair<QAction*, const char*>> icons = {
        { import, "import" }, { undo, "undo" }, { redo, "redo" }, { split, "split" }, { del, "delete" },
        { detach, "detach" }, { title, "title" }, { zoomOut, "zoom-out" }, { zoomIn, "zoom-in" }, { fit, "fit" },
        { captions, "captions" },
    };
    for (const auto& [action, icon] : icons)
        m_actionIcons << qMakePair(action, QString(icon));
    refreshIcons();
    exportAction->setIcon(Icons::get("export", QColor(0x07, 0x13, 0x12)));
    // Zooming is clear enough from its icons alone, and it leaves room for everything else
    for (QAction* a : { zoomOut, zoomIn, fit })
        if (auto* b = qobject_cast<QToolButton*>(bar->widgetForAction(a)))
            b->setToolButtonStyle(Qt::ToolButtonIconOnly);
    // Toolbar buttons can use shorter names than the menu
    split->setIconText("Split");
    del->setIconText("Delete");
    detach->setIconText("Detach audio");
    title->setIconText("Title");
    import->setIconText("Import");
    exportAction->setIconText("Export");

    auto* spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    bar->addWidget(spacer);
    bar->addAction(exportAction);
    if (auto* button = qobject_cast<QToolButton*>(bar->widgetForAction(exportAction)))
        button->setObjectName("primary"); // (the theme makes it the big bright one)
    // The way into the help, always in the corner
    auto* helpCorner = new QWidget;
    auto* helpLayout = new QHBoxLayout(helpCorner);
    helpLayout->setContentsMargins(8, 0, 4, 0);
    QToolButton* helpButton = HelpWindow::button("README.md");
    helpButton->setToolTip("Help (F1)");
    helpLayout->addWidget(helpButton);
    bar->addWidget(helpCorner);

}

QWidget* MainWindow::buildMediaPanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);

    m_mediaBin = new MediaBin;
    m_mediaBin->setIconSize(QSize(ThumbW, ThumbH));
    m_mediaBin->setGridSize(QSize(ThumbW + 20, ThumbH + 48));
    connect(m_mediaBin, &MediaBin::importRequested, this, &MainWindow::importMedia);
    m_mediaBin->setResizeMode(QListView::Adjust);
    m_mediaBin->setWordWrap(true);
    m_mediaBin->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_mediaBin->setFocusPolicy(Qt::ClickFocus); // don't steal the space bar
    layout->addWidget(m_mediaBin);

    connect(m_mediaBin, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        m_timeline->appendClips({ m_mediaBin->clipFor(item) });
    });

    // Right-click menu on media
    m_mediaBin->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_mediaBin, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(this);
        QList<QListWidgetItem*> picked = m_mediaBin->selectedItems();
        if (!picked.isEmpty()) {
            menu.addAction("Add to the end of the timeline", this, [this, picked] {
                QList<TimelineClip> clips;
                for (QListWidgetItem* item : picked)
                    clips << m_mediaBin->clipFor(item);
                m_timeline->appendClips(clips);
            });
            menu.addAction("Remove from project", this, [this, picked] {
                // Clips already on the timeline stay put, this just tidies the media panel
                for (QListWidgetItem* item : picked)
                    delete item;
                setDirty(true);
            });
            menu.addSeparator();
        }
        menu.addAction("Import media…", this, &MainWindow::importMedia);
        menu.exec(m_mediaBin->viewport()->mapToGlobal(pos));
    });

    return panel;
}

QWidget* MainWindow::buildPreviewPanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(10, 8, 10, 8);
    panel->setObjectName("card"); // (a rounded panel, like the library's)
    panel->setAttribute(Qt::WA_StyledBackground);

    auto* title = new QLabel("Preview");
    title->setProperty("role", "heading");
    layout->addWidget(title);

    m_preview = new PreviewWidget;
    layout->addWidget(m_preview, 1);

    // Little control strip under the picture
    auto* controls = new QHBoxLayout;
    auto makeButton = [](const QString& icon, const QString& tip) {
        auto* b = new QToolButton;
        b->setIcon(Icons::get(icon));
        b->setIconSize(QSize(18, 18));
        b->setToolTip(tip);
        b->setFocusPolicy(Qt::NoFocus);
        b->setFixedSize(34, 30);
        return b;
    };
    QToolButton* startButton = makeButton("start", "Go to start");
    m_startButton = startButton;
    m_playButton = makeButton("play", "Play / pause (Space)");
    m_playButton->setObjectName("play");
    m_playButton->setIcon(Icons::get("play", QColor(0x07, 0x13, 0x12))); // (dark, on the bright button)
    connect(startButton, &QToolButton::clicked, this, &MainWindow::goToStart);
    connect(m_playButton, &QToolButton::clicked, this, &MainWindow::togglePlay);

    m_timeLabel = new QLabel;
    m_timeLabel->setProperty("role", "timecode");
    m_timeLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred); // (never cut short)

    controls->addWidget(startButton);
    controls->addWidget(m_playButton);
    controls->addSpacing(8);
    controls->addWidget(m_timeLabel);
    controls->addStretch();

    // The finished video's shape, right where you can see what it does
    m_shapeBox = new QComboBox;
    m_shapeBox->setFocusPolicy(Qt::NoFocus);
    m_shapeBox->setToolTip("The shape of the finished video. File → Project settings for its size and frame rate.");
    for (const ProjectSettings::Shape& s : ProjectSettings::shapes()) {
        m_shapeBox->addItem(s.shortName, s.id);
        m_shapeBox->setItemData(m_shapeBox->count() - 1, s.name, Qt::ToolTipRole);
    }
    connect(m_shapeBox, &QComboBox::activated, this, [this] {
        ProjectSettings s = m_settings;
        s.shape = m_shapeBox->currentData().toString();
        setProjectSettings(s);
    });
    auto* shapeLabel = new QLabel("Shape");
    shapeLabel->setProperty("role", "dim");
    controls->addWidget(shapeLabel);
    controls->addWidget(m_shapeBox);
    layout->addLayout(controls);

    return panel;
}

// ---- Importing ----

void MainWindow::importMedia()
{
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, "Import media", QString(),
        "Media (*.mp4 *.mov *.mkv *.webm *.avi *.m4v *.mp3 *.wav *.m4a *.flac *.ogg *.png *.jpg *.jpeg);;"
        "All files (*)");
    importFiles(paths);
}

QList<QListWidgetItem*> MainWindow::importFiles(const QStringList& paths, bool quiet)
{
    QList<QListWidgetItem*> added;
    if (paths.isEmpty())
        return added;

    QStringList failures;
    for (const QString& path : paths) {
        QString error;
        if (QListWidgetItem* item = addMediaItem(path, &error))
            added << item;
        else
            failures << QString("%1 — %2").arg(QFileInfo(path).fileName(), error);
    }

    if (!added.isEmpty())
        setDirty(true);
    statusBar()->showMessage(QString("Imported %1 file(s)").arg(added.size()), 5000);
    if (!failures.isEmpty() && !quiet)
        QMessageBox::warning(this, "Some files didn't import", failures.join('\n'));
    return added;
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    // Files dropped anywhere outside the timeline go into the media panel
    QStringList files;
    for (const QUrl& url : event->mimeData()->urls())
        if (url.isLocalFile())
            files << url.toLocalFile();
    event->acceptProposedAction();
    // Let the drop finish before we open any dialogs (like "save changes?")
    QTimer::singleShot(0, this, [this, files] { openFiles(files); });
}

void MainWindow::onFilesDroppedOnTimeline(const QStringList& files, const QPoint& pos)
{
    QList<TimelineClip> clips;
    for (QListWidgetItem* item : importFiles(files))
        clips << m_mediaBin->clipFor(item);
    m_timeline->dropClips(clips, pos);
}

void MainWindow::openFiles(const QStringList& paths)
{
    // A project file opens as a project, anything else gets imported
    QStringList media;
    for (const QString& path : paths) {
        if (path.endsWith(QString(".") + ProjectFile::Extension, Qt::CaseInsensitive))
            loadProject(path);
        else
            media << path;
    }
    importFiles(media);
}

QListWidgetItem* MainWindow::addMediaItem(const QString& path, QString* error)
{
    ve_media_info info;
    int rc = ve_probe(path.toUtf8().constData(), &info);
    if (rc != VE_OK) {
        *error = ve_error_string(rc);
        return nullptr;
    }

    QString name = QFileInfo(path).fileName();

    QStringList details;
    if (info.has_video)
        details << QString("Video: %1×%2 · %3 fps · %4")
                       .arg(info.width).arg(info.height)
                       .arg(info.fps, 0, 'f', 2)
                       .arg(info.video_codec);
    if (info.has_audio)
        details << QString("Audio: %1 Hz · %2 ch · %3")
                       .arg(info.sample_rate).arg(info.channels)
                       .arg(info.audio_codec);

    auto* item = new QListWidgetItem(QIcon(QPixmap::fromImage(makeThumbnail(path, info, false))), name);
    // (shown as badges on its card)
    item->setData(CardDelegate::DurationRole, info.duration_sec);
    item->setData(CardDelegate::KindRole, !info.has_video ? "audio" : info.duration_sec > 0 ? "video" : "picture");
    item->setToolTip(name + "\n" + details.join('\n') + "\n\nDrag onto the timeline, or double-click to add it to the end");
    item->setData(MediaBin::PathRole, path);
    item->setData(MediaBin::DurationRole, info.duration_sec);
    item->setData(MediaBin::HasVideoRole, info.has_video != 0);
    item->setData(MediaBin::HasAudioRole, info.has_audio != 0);
    item->setData(MediaBin::WidthRole, info.width);
    item->setData(MediaBin::HeightRole, info.height);
    item->setData(MediaBin::FpsRole, info.fps);
    m_mediaBin->addItem(item);

    // Decoding a frame for the thumbnail takes a moment, so do it in the background
    if (info.has_video) {
        QPointer<MainWindow> self(this);
        QThreadPool::globalInstance()->start([self, path, info] {
            QImage thumb = makeThumbnail(path, info, true);
            QMetaObject::invokeMethod(qApp, [self, path, thumb] {
                if (!self)
                    return;
                QIcon icon(QPixmap::fromImage(thumb));
                for (int i = 0; i < self->m_mediaBin->count(); ++i)
                    if (self->m_mediaBin->item(i)->data(MediaBin::PathRole).toString() == path)
                        self->m_mediaBin->item(i)->setIcon(icon);
            });
        });
    }
    return item;
}

// ---- Timeline & preview ----

MainWindow::Project MainWindow::project(const ProjectSettings& settings) const
{
    // Starts from the first video clip on the timeline, then the project settings get their say
    Project p;
    double earliest = -1;
    for (const TimelineClip& c : m_timeline->clips()) {
        if (!c.hasVideo || c.width <= 0 || c.height <= 0)
            continue;
        if (earliest < 0 || c.start < earliest) {
            earliest = c.start;
            p.width = c.width;
            p.height = c.height;
            p.fps = c.fps > 0 ? c.fps : 30.0;
        }
    }
    QSize size = settings.sizeFor(QSize(p.width, p.height));
    p.width = size.width();
    p.height = size.height();
    if (settings.fps > 0)
        p.fps = settings.fps;
    return p;
}

void MainWindow::setProjectSettings(const ProjectSettings& settings)
{
    bool changed = !(settings == m_settings);
    m_settings = settings;
    int i = m_shapeBox->findData(settings.shape);
    m_shapeBox->setCurrentIndex(i >= 0 ? i : 0);
    if (changed)
        onClipsChanged(); // new shape: redo the preview (and it counts as an unsaved change)
}

void MainWindow::editProjectSettings()
{
    QDialog dialog(this);
    dialog.setWindowTitle("Project settings");
    auto* shape = new QComboBox;
    for (const ProjectSettings::Shape& s : ProjectSettings::shapes())
        shape->addItem(s.name, s.id);
    shape->setCurrentIndex(std::max(0, shape->findData(m_settings.shape)));
    auto* resolution = new QComboBox;
    for (int r : ProjectSettings::resolutions())
        resolution->addItem(r == 2160 ? QStringLiteral("4K (2160)") : QString("%1p").arg(r), r);
    resolution->setCurrentIndex(std::max(0, resolution->findData(m_settings.resolution)));
    auto* fps = new QComboBox;
    fps->addItem("Match the first video", 0.0);
    for (double f : { 24.0, 25.0, 30.0, 50.0, 60.0 })
        fps->addItem(QString("%1 fps").arg(f), f);
    for (int i = 0; i < fps->count(); ++i)
        if (std::abs(fps->itemData(i).toDouble() - m_settings.fps) < 0.01)
            fps->setCurrentIndex(i);
    auto* result = new QLabel;
    result->setProperty("role", "dim");

    auto chosen = [=, this] {
        ProjectSettings s = m_settings;
        s.shape = shape->currentData().toString();
        s.resolution = resolution->currentData().toInt();
        s.fps = fps->currentData().toDouble();
        return s;
    };
    auto refresh = [=, this] {
        bool automatic = shape->currentData().toString() == "auto";
        resolution->setEnabled(!automatic);
        Project p = project(chosen());
        result->setText(QString("%1 × %2 at %3 fps").arg(p.width).arg(p.height).arg(p.fps, 0, 'g', 4));
    };
    for (QComboBox* box : { shape, resolution, fps })
        connect(box, &QComboBox::currentIndexChanged, &dialog, refresh);
    refresh();

    auto* form = new QFormLayout;
    form->addRow("Shape", shape);
    form->addRow("Size", resolution);
    form->addRow("Frame rate", fps);
    form->addRow("Comes out at", result);
    auto* note = new QLabel("Clips that are a different shape get black bars. To fill the frame instead, "
                            "select a clip and tick \"Fill the frame\" under Crop & rotate.");
    note->setWordWrap(true);
    note->setProperty("role", "hint");
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Help);
    connect(buttons, &QDialogButtonBox::helpRequested, [] { HelpWindow::open("settings.md"); });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(note);
    layout->addWidget(buttons);
    dialog.setMinimumWidth(420);
    Theme::fit(dialog);
    if (dialog.exec() == QDialog::Accepted)
        setProjectSettings(chosen());
}

QList<RenderClip> MainWindow::renderClips(QSize titleSize, bool subtitles) const
{
    // Titles become see-through pictures made at the size they'll be shown at
    return m_timeline->renderClips([titleSize](const TitleStyle& style, int highlight) {
        return TitleRenderer::imageFile(style, titleSize, highlight);
    }, subtitles);
}

void MainWindow::onClipsChanged()
{
    Project p = project();
    QList<RenderClip> clips = renderClips(QSize(p.width, p.height));
    m_renderer->setClips(clips);
    m_audio->setClips(clips);

    m_preview->setAspect(double(p.width) / p.height);
    if (m_timeline->clips().isEmpty())
        m_preview->setFrame({});
    requestPreview();
    updateTimeLabel();
    refreshInspector();
    m_subtitles->refresh();
    m_subtitles->setPlayhead(m_timeline->playhead());
    setDirty(true);
}

void MainWindow::refreshInspector()
{
    int i = m_timeline->selectedIndex();
    if (i >= 0 && i < m_timeline->clips().size()) {
        TimelineClip shown = m_timeline->clips().at(i);
        if (shown.isSubtitle())
            shown.title = m_timeline->subtitleLook(shown); // (the track's look, unless it has its own)
        m_inspector->showClip(i, shown, m_timeline->transitionPart(shown));
    }
    else
        m_inspector->showClip(-1, {});
}

// ---- Projects ----

void MainWindow::setDirty(bool dirty)
{
    if (m_loadingProject)
        return;
    m_dirty = dirty;
    updateWindowTitle();
}

void MainWindow::updateWindowTitle()
{
    QString name = m_projectPath.isEmpty() ? QStringLiteral("Untitled") : QFileInfo(m_projectPath).completeBaseName();
    setWindowTitle(QString("%1%2 — MixMedia Video Editor").arg(name, m_dirty ? "*" : ""));
}

bool MainWindow::maybeSave()
{
    if (!m_dirty || m_timeline->clips().isEmpty())
        return true;
    auto answer = QMessageBox::question(this, "Save changes?",
                                        "You've got unsaved changes. Save them first?",
                                        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Save)
        return saveProject();
    return answer == QMessageBox::Discard;
}

void MainWindow::refreshIcons()
{
    for (const auto& [action, icon] : m_actionIcons)
        action->setIcon(Icons::get(icon));
    if (m_startButton)
        m_startButton->setIcon(Icons::get("start"));
}

void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::PaletteChange)
        refreshIcons(); // the theme changed: icons in its colours
    else if (event->type() == QEvent::WindowStateChange && m_fullScreen)
        m_fullScreen->setChecked(isFullScreen()); // (in case it left full screen some other way)
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (maybeSave()) {
        saveLayout();
        event->accept();
    } else {
        event->ignore();
    }
}

// ---- Fitting the window ----

void MainWindow::restoreLayout()
{
    QSettings s;
    if (restoreGeometry(s.value("window/geometry").toByteArray())) {
        m_top->restoreState(s.value("window/panels").toByteArray());
        m_main->restoreState(s.value("window/timeline").toByteArray());
        m_showLibrary->setChecked(s.value("window/library", true).toBool());
        m_showProperties->setChecked(s.value("window/properties", true).toBool());
        m_library->setVisible(m_showLibrary->isChecked());
        m_inspector->setVisible(m_showProperties->isChecked());
        return;
    }
    // First time: a good size for this screen. Small screens get the whole thing.
    const QRect screen = this->screen() ? this->screen()->availableGeometry() : QRect(0, 0, 1600, 900);
    if (screen.width() < 1500 || screen.height() < 900) {
        setWindowState(windowState() | Qt::WindowMaximized);
        resize(screen.size());
    } else {
        QSize size(std::min(1600, int(screen.width() * 0.85)), std::min(980, int(screen.height() * 0.85)));
        resize(size);
        move(screen.center() - QPoint(size.width() / 2, size.height() / 2));
    }
}

void MainWindow::saveLayout()
{
    QSettings s;
    s.setValue("window/geometry", saveGeometry());
    s.setValue("window/panels", m_top->saveState());
    s.setValue("window/timeline", m_main->saveState());
    s.setValue("window/library", m_showLibrary->isChecked());
    s.setValue("window/properties", m_showProperties->isChecked());
}

void MainWindow::resetLayout()
{
    for (QAction* a : { m_showLibrary, m_showProperties })
        a->setChecked(true);
    m_focusPreview->setChecked(false);
    m_library->show();
    m_inspector->show();
    // Back to the usual shares: library 2, preview 4, properties 2; picture 3, timeline 2
    const int w = m_top->width(), h = m_main->height();
    m_top->setSizes({ w * 2 / 8, w * 4 / 8, w * 2 / 8 });
    m_main->setSizes({ h * 3 / 5, h * 2 / 5 });
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    adaptToSize();
}

void MainWindow::adaptToSize()
{
    if (!m_toolbar)
        return;
    // Narrow window: toolbar buttons lose their words (hover still says what they are)...
    if (m_toolbarTextWidth == 0 && !m_iconsOnly)
        m_toolbarTextWidth = m_toolbar->sizeHint().width();
    const bool iconsOnly = m_toolbarTextWidth > 0 && width() < m_toolbarTextWidth + 16;
    if (iconsOnly != m_iconsOnly) {
        m_iconsOnly = iconsOnly;
        for (const auto& [action, icon] : m_actionIcons)
            if (auto* b = qobject_cast<QToolButton*>(m_toolbar->widgetForAction(action)))
                if (!icon.startsWith("zoom") && icon != "fit")
                    b->setToolButtonStyle(iconsOnly ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
    }
    // ...and the side panels slim down a bit
    const bool tiny = width() < 1000, compact = width() < 1280;
    m_inspector->setMinimumWidth(tiny ? 230 : compact ? 290 : 350);
    m_library->setMinimumWidth(tiny ? 150 : compact ? 190 : 220);
}

void MainWindow::newProject()
{
    if (!maybeSave())
        return;
    stopPlayback();
    m_loadingProject = true;
    m_mediaBin->clear();
    setProjectSettings({});
    m_timeline->setClips({}, TimelineWidget::defaultTracks());
    m_timeline->setPlayhead(0);
    m_loadingProject = false;
    m_projectPath.clear();
    setDirty(false);
}

void MainWindow::openProject()
{
    if (!maybeSave())
        return;
    QString path = QFileDialog::getOpenFileName(this, "Open project", projectFolder(),
                                                QString("MixMedia projects (*.%1)").arg(ProjectFile::Extension));
    if (!path.isEmpty())
        loadProject(path);
}

bool MainWindow::loadProject(const QString& path)
{
    ProjectFile::Data data;
    QStringList missing;
    QString error;
    if (!ProjectFile::load(path, &data, &missing, &error)) {
        QMessageBox::warning(this, "Couldn't open project", error);
        return false;
    }

    stopPlayback();
    m_loadingProject = true;
    m_mediaBin->clear();
    importFiles(data.media, /*quiet*/ true);

    // Thumbnails aren't saved, so borrow them from the media panel
    QHash<QString, QPixmap> thumbs;
    for (int i = 0; i < m_mediaBin->count(); ++i)
        thumbs.insert(m_mediaBin->item(i)->data(MediaBin::PathRole).toString(), m_mediaBin->item(i)->icon().pixmap(m_mediaBin->iconSize()));
    for (TimelineClip& c : data.clips)
        c.thumb = thumbs.value(c.path);

    setProjectSettings(data.settings);
    m_timeline->setClips(data.clips, data.tracks);
    m_timeline->setPlayhead(data.playhead);
    onPlayheadMoved(data.playhead);
    m_loadingProject = false;

    m_projectPath = path;
    setDirty(false);
    if (!missing.isEmpty())
        QMessageBox::warning(this, "Some files are missing",
                             "These files couldn't be found, so their clips will show up black:\n\n" + missing.join('\n'));
    statusBar()->showMessage("Opened " + QFileInfo(path).fileName(), 5000);
    rememberRecent(path);
    return true;
}

ProjectFile::Data MainWindow::projectData() const
{
    ProjectFile::Data data;
    for (int i = 0; i < m_mediaBin->count(); ++i)
        data.media << m_mediaBin->item(i)->data(MediaBin::PathRole).toString();
    data.clips = m_timeline->clips();
    data.tracks = m_timeline->tracks();
    data.playhead = m_timeline->playhead();
    data.settings = m_settings;
    return data;
}

bool MainWindow::saveProject()
{
    if (m_projectPath.isEmpty())
        return saveProjectAs();

    QString error;
    if (!ProjectFile::save(m_projectPath, projectData(), &error)) {
        QMessageBox::warning(this, "Couldn't save", error);
        return false;
    }
    setDirty(false);
    clearAutoSave();
    statusBar()->showMessage("Saved " + QFileInfo(m_projectPath).fileName(), 4000);
    rememberRecent(m_projectPath);
    return true;
}

bool MainWindow::saveProjectAs()
{
    QString suggested = m_projectPath.isEmpty() ? QDir(projectFolder()).filePath("My Project.mixmedia") : m_projectPath;
    QString path = QFileDialog::getSaveFileName(this, "Save project", suggested,
                                                QString("MixMedia projects (*.%1)").arg(ProjectFile::Extension));
    if (path.isEmpty())
        return false;
    if (!path.endsWith(QString(".") + ProjectFile::Extension))
        path += QString(".") + ProjectFile::Extension;
    m_projectPath = path;
    return saveProject();
}

QString MainWindow::projectFolder() const
{
    if (!m_projectPath.isEmpty())
        return QFileInfo(m_projectPath).absolutePath();
    QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return docs.isEmpty() ? QDir::homePath() : docs;
}

void MainWindow::requestPreview()
{
    if (m_timeline->clips().isEmpty())
        return;
    // No point rendering bigger than the project itself
    Project p = project();
    QSize size = m_preview->renderSize().boundedTo(QSize(p.width, p.height));
    m_renderer->request(m_timeline->playhead(), size);
}

void MainWindow::updateTimeLabel()
{
    const bool hours = m_timeline->duration() >= 3600;
    m_timeLabel->setText(formatClock(m_timeline->playhead(), hours) + "  /  " + formatClock(m_timeline->duration(), hours));
}

void MainWindow::onPlayheadMoved(double sec)
{
    if (m_playing) {
        // Scrubbing while playing: carry on playing from the new spot
        m_playFrom = sec;
        m_clock.restart();
        m_audio->play(sec);
    }
    requestPreview();
    updateTimeLabel();
    m_inspector->setPlayhead(sec);
    m_subtitles->setPlayhead(sec);
}

// ---- Getting around ----

void MainWindow::seekTo(double sec)
{
    sec = std::clamp(sec, 0.0, std::max(0.0, m_timeline->duration()));
    m_timeline->setPlayhead(sec);
    onPlayheadMoved(sec);
}

void MainWindow::seekBy(double seconds)
{
    seekTo(m_timeline->playhead() + seconds);
}

void MainWindow::stepFrames(int frames)
{
    stopPlayback(); // stepping frame by frame only makes sense when paused
    seekTo(m_timeline->playhead() + frames / project().fps);
}

void MainWindow::jumpToCut(int direction)
{
    const double here = m_timeline->playhead();
    const QList<double> cuts = m_timeline->cutPoints();
    if (direction > 0) {
        for (double c : cuts)
            if (c > here + 1e-3)
                return seekTo(c);
    } else {
        for (auto it = cuts.rbegin(); it != cuts.rend(); ++it)
            if (*it < here - 1e-3)
                return seekTo(*it);
    }
}

// ---- Playback ----

void MainWindow::togglePlay()
{
    if (m_playing)
        stopPlayback();
    else
        startPlayback();
}

void MainWindow::startPlayback()
{
    double end = m_timeline->duration();
    if (end <= 0)
        return;
    double from = m_timeline->playhead();
    if (from >= end - 0.05)
        from = 0; // at the end? start again from the top

    m_playFrom = from;
    m_timeline->setPlayhead(from);
    m_clock.start();
    m_audio->play(from);
    m_playTimer->start();
    m_playing = true;
    m_playButton->setIcon(Icons::get("pause", QColor(0x07, 0x13, 0x12)));
}

void MainWindow::stopPlayback()
{
    if (!m_playing)
        return;
    m_playTimer->stop();
    m_audio->stop();
    m_playing = false;
    m_playButton->setIcon(Icons::get("play", QColor(0x07, 0x13, 0x12)));
}

void MainWindow::onTick()
{
    double t = m_playFrom + m_clock.nsecsElapsed() / 1e9;
    double end = m_timeline->duration();
    if (t >= end) {
        t = end;
        stopPlayback();
    }
    m_timeline->setPlayhead(t);
    requestPreview();
    updateTimeLabel();
    m_inspector->setPlayhead(t); // keyframed settings show what they are right now
    m_subtitles->setPlayhead(t);
}

void MainWindow::goToStart()
{
    m_timeline->setPlayhead(0);
    onPlayheadMoved(0);
}

// ---- Export ----

void MainWindow::exportVideo()
{
    if (m_timeline->clips().isEmpty()) {
        QMessageBox::information(this, "Nothing to export", "Put some clips on the timeline first!");
        return;
    }
    stopPlayback();

    Project p = project();
    QString folder = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (folder.isEmpty())
        folder = QDir::homePath();
    QString suggested = QDir(folder).filePath("My Video.mp4");

    // Ask the engine whether this edit can be copied straight across (instant export)
    char why[256] = {};
    ve_timeline* check = ve_timeline_create();
    applyClips(check, renderClips(QSize(p.width, p.height)));
    bool canCopy = ve_timeline_can_copy(check, why, sizeof why) != 0;
    ve_timeline_destroy(check);

    const QList<int> subtitleLines = m_timeline->subtitleLines();
    ExportDialog dialog(QSize(p.width, p.height), p.fps, suggested, canCopy, QString::fromUtf8(why),
                        !subtitleLines.isEmpty(), this);
    if (dialog.exec() != QDialog::Accepted || dialog.path().isEmpty())
        return;

    // The export runs on its own thread with its own copy of everything
    struct Job {
        std::atomic<double> done { 0.0 };
        std::atomic<bool> cancel { false };
        int result = VE_OK;
        QString encoder;
    };
    auto job = std::make_shared<Job>();
    QList<RenderClip> clips = renderClips(dialog.size(), dialog.burnSubtitles()); // titles drawn sharp at the export size
    // Subtitles as their own track and/or file
    QList<SubtitleFile::Line> subtitleText;
    for (int i : subtitleLines) {
        const TimelineClip& c = m_timeline->clips().at(i);
        subtitleText << SubtitleFile::Line { c.start, c.end(), c.title.text };
    }
    auto subtitleBytes = std::make_shared<std::vector<QByteArray>>();
    auto subtitleList = std::make_shared<std::vector<ve_subtitle>>();
    if (dialog.subtitleTrack()) {
        for (const SubtitleFile::Line& l : subtitleText)
            subtitleBytes->push_back(l.text.toUtf8());
        for (size_t k = 0; k < subtitleBytes->size(); ++k)
            subtitleList->push_back(ve_subtitle { subtitleText[int(k)].start, subtitleText[int(k)].end, (*subtitleBytes)[k].constData() });
    }
    QByteArray path = dialog.path().toUtf8();
    ve_export_settings settings {};
    settings.width = dialog.size().width();
    settings.height = dialog.size().height();
    settings.fps = dialog.fps();
    settings.crf = dialog.crf();
    settings.force_software = dialog.useGraphicsCard() ? 0 : 1;
    settings.copy_only = dialog.instant() ? 1 : 0;
    settings.format = dialog.format();
    settings.subtitles = subtitleList->empty() ? nullptr : subtitleList->data();
    settings.subtitle_count = int(subtitleList->size());
    settings.subtitle_language = "eng";

    QThread* worker = QThread::create([job, clips, path, settings, subtitleBytes, subtitleList]() mutable {
        settings.path = path.constData();
        ve_timeline* tl = ve_timeline_create();
        applyClips(tl, clips);
        job->result = ve_export(tl, &settings, [](double done, void* user) {
            auto* j = static_cast<Job*>(user);
            j->done = done;
            return j->cancel ? 1 : 0;
        }, job.get());
        job->encoder = QString::fromUtf8(settings.encoder_used);
        ve_timeline_destroy(tl);
    });

    QProgressDialog progress("Exporting…", "Cancel", 0, 1000, this);
    progress.setWindowTitle("Export");
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setMinimumWidth(380);
    connect(&progress, &QProgressDialog::canceled, this, [job] { job->cancel = true; });

    QElapsedTimer clock;
    clock.start();
    QTimer poll;
    connect(&poll, &QTimer::timeout, this, [&] {
        double done = job->done;
        progress.setValue(int(done * 1000));
        QString eta;
        if (done > 0.02) {
            double left = clock.elapsed() / 1000.0 * (1.0 - done) / done;
            eta = QString(" · about %1 left").arg(formatDuration(left));
        }
        progress.setLabelText(QString("Exporting… %1%%2").arg(int(done * 100)).arg(eta));
    });

    QEventLoop loop;
    connect(worker, &QThread::finished, &loop, &QEventLoop::quit);
    worker->start();
    poll.start(100);
    loop.exec();
    poll.stop();
    progress.reset();
    delete worker;

    if (job->result == VE_OK && dialog.subtitleFile()) {
        // The .srt goes right next to the video, with the same name
        QFileInfo video(dialog.path());
        QString error;
        if (!SubtitleFile::save(video.path() + "/" + video.completeBaseName() + ".srt", subtitleText, &error))
            QMessageBox::warning(this, "Couldn't save the subtitle file", error);
    }
    if (job->result == VE_OK) {
        QMessageBox box(QMessageBox::Information, "Export done",
                        QString("Saved %1\n(took %2, using %3)")
                            .arg(QFileInfo(dialog.path()).fileName(),
                                 formatDuration(clock.elapsed() / 1000.0),
                                 job->encoder == "copy"      ? QStringLiteral("instant copy")
                                 : job->encoder == "libx264" || job->encoder == "gif" || job->encoder == "sound"
                                     ? QStringLiteral("the CPU")
                                                             : QStringLiteral("the graphics card")),
                        QMessageBox::Ok, this);
        QPushButton* show = box.addButton("Show in folder", QMessageBox::ActionRole);
        box.exec();
        if (box.clickedButton() == show)
            showInFileManager(dialog.path());
    } else if (job->result != VE_ERR_CANCELLED) {
        QMessageBox::warning(this, "Export failed", QString("Export failed: %1").arg(ve_error_string(job->result)));
    }
}

// ---- About ----

void MainWindow::importSubtitles()
{
    QString path = QFileDialog::getOpenFileName(this, "Import subtitles", projectFolder(),
                                                "Subtitles (*.srt *.vtt);;All files (*)");
    if (path.isEmpty())
        return;
    QList<SubtitleFile::Line> lines;
    QString error;
    if (!SubtitleFile::load(path, &lines, &error)) {
        QMessageBox::warning(this, "Couldn't import subtitles", error);
        return;
    }
    // Already got some? Swap them out, or add these as well
    bool replace = false;
    if (!m_timeline->subtitleLines().isEmpty()) {
        QMessageBox ask(QMessageBox::Question, "Import subtitles",
                        "There are subtitles here already. Replace them, or add these as well?", QMessageBox::Cancel, this);
        QPushButton* swap = ask.addButton("Replace", QMessageBox::AcceptRole);
        QPushButton* both = ask.addButton("Add", QMessageBox::AcceptRole);
        ask.exec();
        if (ask.clickedButton() != swap && ask.clickedButton() != both)
            return;
        replace = ask.clickedButton() == swap;
    }
    QList<TimelineClip> clips;
    for (const SubtitleFile::Line& l : lines) {
        TimelineClip c;
        c.kind = TimelineClip::Kind::Subtitle;
        c.title = TitleStyle::subtitles();
        c.title.text = l.text;
        c.start = l.start;
        c.duration = l.end - l.start;
        clips << c;
    }
    m_timeline->addSubtitles(clips, replace);
    statusBar()->showMessage(QString("Imported %1 subtitle lines").arg(lines.size()), 5000);
}

void MainWindow::autoCaptions()
{
    if (m_timeline->clips().isEmpty()) {
        QMessageBox::information(this, "Auto-captions", "Put something with talking in it on the timeline first!");
        return;
    }
    stopPlayback();
    bool replace = true;
    // (just the sound matters here, so no title pictures needed)
    QList<TimelineClip> lines = AutoCaptions::run(this, m_timeline->renderClips(), !m_timeline->subtitleLines().isEmpty(), &replace);
    if (lines.isEmpty())
        return;
    m_timeline->addSubtitles(lines, replace);
    if (auto* tabs = findChild<QTabWidget*>())
        tabs->setCurrentWidget(m_subtitles);
    statusBar()->showMessage(QString("Wrote %1 subtitle lines. Have a read through them in the Subtitles tab.").arg(lines.size()), 8000);
}

void MainWindow::exportSubtitles()
{
    QList<SubtitleFile::Line> lines;
    for (int i : m_timeline->subtitleLines()) {
        const TimelineClip& c = m_timeline->clips().at(i);
        lines << SubtitleFile::Line { c.start, c.end(), c.title.text };
    }
    if (lines.isEmpty()) {
        QMessageBox::information(this, "No subtitles", "There aren't any subtitles to export yet.");
        return;
    }
    QString suggested = m_projectPath.isEmpty() ? QDir(projectFolder()).filePath("Subtitles.srt")
                                                : QFileInfo(m_projectPath).path() + "/" + QFileInfo(m_projectPath).completeBaseName() + ".srt";
    QString path = QFileDialog::getSaveFileName(this, "Export subtitles", suggested, "SubRip subtitles (*.srt)");
    if (path.isEmpty())
        return;
    if (!path.endsWith(".srt", Qt::CaseInsensitive))
        path += ".srt";
    QString error;
    if (!SubtitleFile::save(path, lines, &error))
        QMessageBox::warning(this, "Couldn't save subtitles", error);
    else
        statusBar()->showMessage("Saved " + QFileInfo(path).fileName(), 5000);
}

void MainWindow::evenOutVolume(int index)
{
    // Aim for a comfortable loudness (about where speech usually sits), but never clip
    if (index < 0 || index >= m_timeline->clips().size())
        return;
    TimelineClip c = m_timeline->clips().at(index);
    float rms = 0, peak = 0;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    int rc = ve_measure_loudness(c.path.toUtf8().constData(), c.in, c.sourceSpan(), &rms, &peak);
    QApplication::restoreOverrideCursor();
    if (rc != VE_OK || peak < -80) {
        statusBar()->showMessage("Couldn't hear anything in that clip to even out", 5000);
        return;
    }
    constexpr float Target = -18.0f, Ceiling = -1.0f;
    float gainDb = std::min(Target - rms, Ceiling - peak);
    c.volume = std::clamp(std::pow(10.0f, gainDb / 20.0f), 0.0f, 4.0f);
    m_timeline->updateClip(index, c, "evenOut");
    statusBar()->showMessage(QString("Volume set to %1%").arg(int(std::lround(c.volume * 100))), 5000);
}

void MainWindow::showAbout()
{
    // Keep this credit if you share a modified version, see NOTICE
    QMessageBox box(this);
    box.setWindowTitle("About MixMedia");
    box.setIconPixmap(QPixmap(":/mixmedia.png").scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    box.setTextFormat(Qt::RichText);
    box.setText(QString(
        "<h2>MixMedia Video Editor</h2>"
        "<p>Version %1</p>"
        "<p>Made by <b>Shayan Mazahir</b><br>"
        "<a href='https://github.com/Shayan-Mazahir/MixMedia-Video-Editor'>github.com/Shayan-Mazahir/MixMedia-Video-Editor</a></p>"
        "<p>Free and open source under the "
        "<a href='https://www.gnu.org/licenses/gpl-3.0.html'>GNU GPL v3</a>.<br>"
        "You can use, change and share it, as long as you keep it open<br>"
        "and keep the credit to the original author.</p>"
        "<p style='color:#808286'>Built with Qt %2 and FFmpeg (engine v%3)</p>")
                    .arg(MIXMEDIA_VERSION, qVersion(), ve_version()));
    box.setTextInteractionFlags(Qt::TextBrowserInteraction);
    if (auto* label = box.findChild<QLabel*>("qt_msgbox_label"))
        label->setOpenExternalLinks(true);
    box.exec();
}

// ---- Testing helper ----

void MainWindow::runDemo(const QStringList& paths, const QString& screenshotPath)
{
    importFiles(paths);
    QList<TimelineClip> clips;
    for (int i = 0; i < m_mediaBin->count(); ++i)
        clips << m_mediaBin->clipFor(m_mediaBin->item(i));
    m_timeline->appendClips(clips);
    m_timeline->detachAudio();
    m_timeline->setPlayhead(m_timeline->duration() * 0.4);
    onPlayheadMoved(m_timeline->playhead());

    // Show off a title too, with a little fade in
    m_timeline->addTitle();
    int t = m_timeline->selectedIndex();
    TimelineClip title = m_timeline->clips().at(t);
    title.title.text = "MixMedia says hi";
    title.fadeIn = 0.5;
    m_timeline->updateClip(t, title, "demo");

    // And some effects on the video, shown in the properties panel
    TimelineClip video = m_timeline->clips().at(0);
    video.look = 4; // vivid
    video.vignette = 0.4f;
    m_timeline->updateClip(0, video, "demoFx");

    // A cut with a dissolve across it, and the title sliding up into place
    double cut = m_timeline->playhead() - 1.0;
    m_timeline->selectClip(0);
    m_timeline->setPlayhead(cut);
    m_timeline->splitAtPlayhead();
    int after = m_timeline->selectedIndex();
    m_timeline->setTransition(after, VE_TRANSITION_DISSOLVE); // a transition block on the cut
    int block = m_timeline->selectedIndex();
    TimelineClip dissolve = m_timeline->clips().at(block);
    dissolve.duration = 2.0; // the clips slide to make a 2 second overlap
    m_timeline->updateClip(block, dissolve, "demoTransition");
    // ...and a Vintage effect block over a stretch of it, on the FX track above
    TimelineClip vintage = effectPresets().at(2);
    vintage.duration = 30.0;
    m_timeline->setPlayhead(cut - 10.0);
    m_timeline->addAtPlayhead(vintage);
    // ...and a zoom with no cut at all, playing on the spot
    m_timeline->setPlayhead(cut + 4.0);
    m_timeline->addAtPlayhead(transitionPresets().last());
    title = m_timeline->clips().at(t);
    title.animIn = VE_ANIM_SLIDE_UP;
    title.animInDuration = 1.0;
    m_timeline->updateClip(t, title, "demoAnim");
    // ...and a zoom into the top-left of the clip after the cut (keyframes)
    {
        TimelineClip zoomed = m_timeline->clips().at(after);
        double t0 = 3.0;
        for (auto [t, size, pos] : { std::tuple { t0, 1.0, 0.0 }, { t0 + 0.6, 2.0, 0.5 }, { t0 + 2.6, 2.0, 0.5 }, { t0 + 3.2, 1.0, 0.0 } }) {
            zoomed.setKey(VE_KEY_SIZE, t, float(size));
            zoomed.setKey(VE_KEY_POS_X, t, float(pos));
            zoomed.setKey(VE_KEY_POS_Y, t, float(pos));
        }
        m_timeline->updateClip(after, zoomed, "demoZoom");
    }
    // MIXMEDIA_DEMO_SUBS=1 adds some subtitles (word by word) and shows the Subtitles tab
    const bool demoSubs = qEnvironmentVariableIsSet("MIXMEDIA_DEMO_SUBS");
    if (demoSubs) {
        QList<TimelineClip> lines;
        const char* text[] = { "So today we're styling a table", "First the borders", "Then a splash of colour" };
        for (int k = 0; k < 3; ++k) {
            TimelineClip c;
            c.kind = TimelineClip::Kind::Subtitle;
            c.title.text = text[k];
            c.start = cut - 3.0 + k * 2.5;
            c.duration = 2.3;
            lines << c;
        }
        m_timeline->addSubtitles(lines, false);
        int first = m_timeline->subtitleLines().first();
        TimelineClip look = m_timeline->clips().at(first);
        look.title = m_timeline->subtitleLook(look);
        look.title.wordByWord = true;
        look.title.size = 7;
        look.title.box = false;
        look.title.outline = 10;
        m_timeline->updateClip(first, look, "demoSubs");
    }
    // MIXMEDIA_DEMO_SHAPE=9:16 (or any shape) tries a different project shape, with the clip filling it
    if (qEnvironmentVariableIsSet("MIXMEDIA_DEMO_SHAPE")) {
        ProjectSettings shape;
        shape.shape = qEnvironmentVariable("MIXMEDIA_DEMO_SHAPE");
        setProjectSettings(shape);
        TimelineClip filled = m_timeline->clips().at(after);
        filled.fill = true;
        filled.rotation = 4;
        m_timeline->updateClip(after, filled, "demoFill");
    }
    m_timeline->selectClip(after);
    // Halfway through the overlap (MIXMEDIA_DEMO_AT=seconds after the cut picks another moment)
    double at = qEnvironmentVariableIsSet("MIXMEDIA_DEMO_AT") ? cut + qEnvironmentVariable("MIXMEDIA_DEMO_AT").toDouble() : cut - 1.0;
    m_timeline->setPlayhead(at);
    onPlayheadMoved(at);

    // Let things load, play for 2 seconds, then take the picture
    // MIXMEDIA_DEMO_WAIT=ms adds extra time before playing (to let slow background work finish)
    int wait = qEnvironmentVariableIntValue("MIXMEDIA_DEMO_WAIT");
    QTimer::singleShot(1500 + wait, this, [this] {
        if (qEnvironmentVariableIsSet("MIXMEDIA_DEMO_STILL")) {
            // Stay put (mid-transition) and show the Transitions tab instead of playing
            if (auto* tabs = findChild<QTabWidget*>())
                tabs->setCurrentIndex(qEnvironmentVariableIsSet("MIXMEDIA_DEMO_TAB") ? qEnvironmentVariableIntValue("MIXMEDIA_DEMO_TAB")
                                      : qEnvironmentVariableIsSet("MIXMEDIA_DEMO_SUBS") ? tabs->count() - 1 : 2); // (MIXMEDIA_DEMO_TAB=n picks one)
            m_timeline->zoomBy(60); // close up around the playhead
            return;
        }
        qInfo("demo: playing from %.3f", m_timeline->playhead());
        startPlayback();
    });
    QTimer::singleShot(3500 + wait, this, [this, screenshotPath] {
        stopPlayback();
        qInfo("demo: stopped at %.3f", m_timeline->playhead());
    });
    QTimer::singleShot(4000 + wait, this, [this, screenshotPath] {
        // MIXMEDIA_DEMO_HELP=page.md screenshots that help page instead
        if (qEnvironmentVariableIsSet("MIXMEDIA_DEMO_HELP")) {
            HelpWindow::open(qEnvironmentVariable("MIXMEDIA_DEMO_HELP"));
            for (QWidget* w : QApplication::topLevelWidgets())
                if (qobject_cast<HelpWindow*>(w))
                    w->grab().save(screenshotPath);
        } else {
            grab().save(screenshotPath);
        }
        m_dirty = false; // it's only a demo, don't ask to save on the way out
        qApp->quit();
    });
}
