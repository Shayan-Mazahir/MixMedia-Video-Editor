// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "MainWindow.h"
#include "AudioPlayer.h"
#include "ExportDialog.h"
#include "MediaBin.h"
#include "PreviewRenderer.h"
#include "PreviewWidget.h"
#include "TimelineWidget.h"
#include "TitleRenderer.h"
#include "ClipInspector.h"
#include "ProjectFile.h"

#include <ve/engine.h>

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QListWidgetItem>
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
#include <QProgressDialog>
#include <QPushButton>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <atomic>
#include <memory>
#include <vector>

namespace {

constexpr int ThumbW = 160;
constexpr int ThumbH = 90;

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
QString formatClock(double seconds)
{
    int cs = int(seconds * 100 + 0.5);
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
    m_inspector->setMinimumWidth(310);

    auto* top = new QSplitter(Qt::Horizontal);
    top->addWidget(buildMediaPanel());
    top->addWidget(buildPreviewPanel());
    top->addWidget(m_inspector);
    top->setStretchFactor(0, 2);
    top->setStretchFactor(1, 4);
    top->setStretchFactor(2, 1);

    auto* main = new QSplitter(Qt::Vertical);
    main->addWidget(top);
    main->addWidget(m_timeline);
    main->setStretchFactor(0, 3);
    main->setStretchFactor(1, 2);
    setCentralWidget(main);

    buildActions();

    connect(m_timeline, &TimelineWidget::clipsChanged, this, &MainWindow::onClipsChanged);
    connect(m_timeline, &TimelineWidget::selectionChanged, this, &MainWindow::refreshInspector);
    connect(m_inspector, &ClipInspector::edited, m_timeline, &TimelineWidget::updateClip);
    connect(m_inspector, &ClipInspector::speedChanged, m_timeline, &TimelineWidget::setClipSpeed);
    connect(m_inspector, &ClipInspector::detachAudioClicked, m_timeline, &TimelineWidget::detachAudio);
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

    updateTimeLabel();
    updateWindowTitle();
    statusBar()->showMessage(QString("Engine v%1 · Ready").arg(ve_version()));
}

MainWindow::~MainWindow()
{
    stopPlayback();
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

    QAction* undo = make("&Undo", { QKeySequence::Undo }, "Undo", [this] { m_timeline->undo(); });
    QAction* redo = make("&Redo", { QKeySequence("Ctrl+Shift+Z"), QKeySequence("Ctrl+Y") }, "Redo", [this] { m_timeline->redo(); });
    QAction* split = make("S&plit", { QKeySequence("S"), QKeySequence("Ctrl+B") }, "Split at the playhead", [this] { m_timeline->splitAtPlayhead(); });
    QAction* del = make("&Delete", {}, "Delete the selected clip and close the gap (Delete key)", [this] { m_timeline->deleteSelected(); });
    QAction* delGap = make("Delete, &leaving a gap", {}, "Delete the selected clip but leave the space empty (Shift+Delete)", [this] { m_timeline->deleteSelectedKeepGap(); });
    QAction* detach = make("Detach &audio", { QKeySequence("Ctrl+D") }, "Put the selected clip's sound on its own track", [this] { m_timeline->detachAudio(); });
    QAction* title = make("Add &title", { QKeySequence("Ctrl+T") }, "Add a title at the playhead", [this] { m_timeline->addTitle(); });

    QAction* zoomOut = make("Zoom −", { QKeySequence("Ctrl+-") }, "Zoom out", [this] { m_timeline->zoomBy(0.8); });
    QAction* zoomIn = make("Zoom +", { QKeySequence("Ctrl+=") }, "Zoom in", [this] { m_timeline->zoomBy(1.25); });
    QAction* fit = make("Fit", { QKeySequence("Ctrl+0") }, "Fit the whole timeline", [this] { m_timeline->zoomToFit(); });

    QMenu* file = menuBar()->addMenu("&File");
    file->addActions({ newProject, open, save, saveAs });
    file->addSeparator();
    file->addActions({ import, exportAction });
    file->addSeparator();
    file->addAction(quit);
    QMenu* edit = menuBar()->addMenu("&Edit");
    edit->addActions({ undo, redo });
    edit->addSeparator();
    edit->addActions({ split, del, delGap, detach, title });
    QMenu* view = menuBar()->addMenu("&View");
    view->addActions({ zoomIn, zoomOut, fit });
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
    help->addAction(make("&About MixMedia", {}, "Who made this, and the licence", &MainWindow::showAbout));
    help->addAction(make("About &Qt", {}, "About the Qt toolkit", [] { QApplication::aboutQt(); }));

    QToolBar* bar = addToolBar("Main");
    bar->setMovable(false);
    bar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    bar->addAction(import);
    bar->addSeparator();
    bar->addActions({ undo, redo });
    bar->addSeparator();
    bar->addActions({ split, del, detach, title });
    bar->addSeparator();
    bar->addActions({ zoomOut, zoomIn, fit });
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
        button->setStyleSheet("QToolButton { background: #2fc6b4; color: black; font-weight: bold;"
                              " padding: 4px 14px; border-radius: 4px; }");

}

QWidget* MainWindow::buildMediaPanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 8, 4, 8);

    auto* title = new QLabel("Media");
    title->setStyleSheet("font-weight: bold;");
    layout->addWidget(title);

    m_mediaBin = new MediaBin;
    m_mediaBin->setIconSize(QSize(ThumbW, ThumbH));
    m_mediaBin->setGridSize(QSize(ThumbW + 20, ThumbH + 44));
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
    layout->setContentsMargins(4, 8, 8, 8);

    auto* title = new QLabel("Preview");
    title->setStyleSheet("font-weight: bold;");
    layout->addWidget(title);

    m_preview = new PreviewWidget;
    layout->addWidget(m_preview, 1);

    // Little control strip under the picture
    auto* controls = new QHBoxLayout;
    auto makeButton = [](const QString& text, const QString& tip) {
        auto* b = new QToolButton;
        b->setText(text);
        b->setToolTip(tip);
        b->setFocusPolicy(Qt::NoFocus);
        b->setMinimumWidth(36);
        return b;
    };
    QToolButton* startButton = makeButton("⏮", "Go to start");
    m_playButton = makeButton("▶", "Play / pause (Space)");
    connect(startButton, &QToolButton::clicked, this, &MainWindow::goToStart);
    connect(m_playButton, &QToolButton::clicked, this, &MainWindow::togglePlay);

    m_timeLabel = new QLabel;
    m_timeLabel->setStyleSheet("font-family: monospace; color: #aaa;");

    controls->addWidget(startButton);
    controls->addWidget(m_playButton);
    controls->addSpacing(8);
    controls->addWidget(m_timeLabel);
    controls->addStretch();
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
    QString label = info.duration_sec > 0 ? QString("%1\n%2").arg(name, formatDuration(info.duration_sec)) : name;

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

    auto* item = new QListWidgetItem(QIcon(QPixmap::fromImage(makeThumbnail(path, info, false))), label);
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

MainWindow::Project MainWindow::project() const
{
    // The project takes its size from the first video clip on the timeline
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
    return p;
}

QList<RenderClip> MainWindow::renderClips(QSize titleSize) const
{
    // Titles become see-through pictures made at the size they'll be shown at
    return m_timeline->renderClips([titleSize](const TimelineClip& c) {
        return TitleRenderer::imageFile(c.title, titleSize);
    });
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
    setDirty(true);
}

void MainWindow::refreshInspector()
{
    int i = m_timeline->selectedIndex();
    if (i >= 0 && i < m_timeline->clips().size())
        m_inspector->showClip(i, m_timeline->clips().at(i));
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

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (maybeSave())
        event->accept();
    else
        event->ignore();
}

void MainWindow::newProject()
{
    if (!maybeSave())
        return;
    stopPlayback();
    m_loadingProject = true;
    m_mediaBin->clear();
    m_timeline->setClips({});
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

    m_timeline->setClips(data.clips);
    m_timeline->setPlayhead(data.playhead);
    onPlayheadMoved(data.playhead);
    m_loadingProject = false;

    m_projectPath = path;
    setDirty(false);
    if (!missing.isEmpty())
        QMessageBox::warning(this, "Some files are missing",
                             "These files couldn't be found, so their clips will show up black:\n\n" + missing.join('\n'));
    statusBar()->showMessage("Opened " + QFileInfo(path).fileName(), 5000);
    return true;
}

bool MainWindow::saveProject()
{
    if (m_projectPath.isEmpty())
        return saveProjectAs();

    ProjectFile::Data data;
    for (int i = 0; i < m_mediaBin->count(); ++i)
        data.media << m_mediaBin->item(i)->data(MediaBin::PathRole).toString();
    data.clips = m_timeline->clips();
    data.playhead = m_timeline->playhead();

    QString error;
    if (!ProjectFile::save(m_projectPath, data, &error)) {
        QMessageBox::warning(this, "Couldn't save", error);
        return false;
    }
    setDirty(false);
    statusBar()->showMessage("Saved " + QFileInfo(m_projectPath).fileName(), 4000);
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
    m_timeLabel->setText(formatClock(m_timeline->playhead()) + "  /  " + formatClock(m_timeline->duration()));
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
    m_playButton->setText("⏸");
}

void MainWindow::stopPlayback()
{
    if (!m_playing)
        return;
    m_playTimer->stop();
    m_audio->stop();
    m_playing = false;
    m_playButton->setText("▶");
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

    ExportDialog dialog(QSize(p.width, p.height), p.fps, suggested, canCopy, QString::fromUtf8(why), this);
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
    QList<RenderClip> clips = renderClips(dialog.size()); // titles drawn sharp at the export size
    QByteArray path = dialog.path().toUtf8();
    ve_export_settings settings {};
    settings.width = dialog.size().width();
    settings.height = dialog.size().height();
    settings.fps = dialog.fps();
    settings.crf = dialog.crf();
    settings.force_software = dialog.useGraphicsCard() ? 0 : 1;
    settings.copy_only = dialog.instant() ? 1 : 0;

    QThread* worker = QThread::create([job, clips, path, settings]() mutable {
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

    if (job->result == VE_OK) {
        QMessageBox box(QMessageBox::Information, "Export done",
                        QString("Saved %1\n(took %2, using %3)")
                            .arg(QFileInfo(dialog.path()).fileName(),
                                 formatDuration(clock.elapsed() / 1000.0),
                                 job->encoder == "copy"      ? QStringLiteral("instant copy")
                                 : job->encoder == "libx264" ? QStringLiteral("the CPU")
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
    m_timeline->selectClip(0);

    // Let things load, play for 2 seconds, then take the picture
    // MIXMEDIA_DEMO_WAIT=ms adds extra time before playing (to let slow background work finish)
    int wait = qEnvironmentVariableIntValue("MIXMEDIA_DEMO_WAIT");
    QTimer::singleShot(1500 + wait, this, [this] {
        qInfo("demo: playing from %.3f", m_timeline->playhead());
        startPlayback();
    });
    QTimer::singleShot(3500 + wait, this, [this, screenshotPath] {
        stopPlayback();
        qInfo("demo: stopped at %.3f", m_timeline->playhead());
    });
    QTimer::singleShot(4000 + wait, this, [this, screenshotPath] {
        grab().save(screenshotPath);
        m_dirty = false; // it's only a demo, don't ask to save on the way out
        qApp->quit();
    });
}
