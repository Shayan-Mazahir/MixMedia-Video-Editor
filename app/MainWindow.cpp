#include "MainWindow.h"
#include "AudioPlayer.h"
#include "ExportDialog.h"
#include "MediaBin.h"
#include "PreviewRenderer.h"
#include "PreviewWidget.h"
#include "TimelineWidget.h"

#include <ve/engine.h>

#include <QAction>
#include <QApplication>
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
QPixmap makeThumbnail(const QString& path, const ve_media_info& info)
{
    QPixmap card(ThumbW, ThumbH);
    card.fill(QColor(0x0e, 0x0f, 0x10));
    QPainter p(&card);

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
    setWindowTitle("MixMedia Video Editor");

    m_timeline = new TimelineWidget;

    auto* top = new QSplitter(Qt::Horizontal);
    top->addWidget(buildMediaPanel());
    top->addWidget(buildPreviewPanel());
    top->setStretchFactor(0, 2);
    top->setStretchFactor(1, 3);

    auto* main = new QSplitter(Qt::Vertical);
    main->addWidget(top);
    main->addWidget(m_timeline);
    main->setStretchFactor(0, 3);
    main->setStretchFactor(1, 2);
    setCentralWidget(main);

    buildToolbar();

    connect(m_timeline, &TimelineWidget::clipsChanged, this, &MainWindow::onClipsChanged);
    connect(m_timeline, &TimelineWidget::playheadMoved, this, &MainWindow::onPlayheadMoved);
    connect(m_renderer, &PreviewRenderer::frameReady, m_preview, [this](const QImage& frame, double) {
        m_preview->setFrame(frame);
    });
    connect(m_preview, &PreviewWidget::resized, this, &MainWindow::requestPreview);

    m_playTimer->setTimerType(Qt::PreciseTimer);
    m_playTimer->setInterval(16);
    connect(m_playTimer, &QTimer::timeout, this, &MainWindow::onTick);

    updateTimeLabel();
    statusBar()->showMessage(QString("Engine v%1 · Ready").arg(ve_version()));
}

MainWindow::~MainWindow()
{
    stopPlayback();
}

// ---- Building the window ----

void MainWindow::buildToolbar()
{
    QToolBar* bar = addToolBar("Main");
    bar->setMovable(false);
    bar->setToolButtonStyle(Qt::ToolButtonTextOnly);

    auto add = [&](const QString& text, const QList<QKeySequence>& keys, const QString& tip, auto slot) {
        QAction* a = bar->addAction(text);
        a->setShortcuts(keys);
        QString keysText = keys.isEmpty() ? QString() : QString(" (%1)").arg(keys.first().toString(QKeySequence::NativeText));
        a->setToolTip(tip + keysText);
        connect(a, &QAction::triggered, this, slot);
        return a;
    };

    add("Import", { QKeySequence("Ctrl+I") }, "Import media", &MainWindow::importMedia);
    bar->addSeparator();
    add("Undo", { QKeySequence::Undo }, "Undo", [this] { m_timeline->undo(); });
    add("Redo", { QKeySequence("Ctrl+Shift+Z"), QKeySequence("Ctrl+Y") }, "Redo", [this] { m_timeline->redo(); });
    bar->addSeparator();
    add("Split", { QKeySequence("S"), QKeySequence("Ctrl+B") }, "Split at the playhead", [this] { m_timeline->splitAtPlayhead(); });
    add("Delete", {}, "Delete the selected clip (Delete key)", [this] { m_timeline->deleteSelected(); });
    bar->addSeparator();
    add("Zoom −", { QKeySequence("Ctrl+-") }, "Zoom out", [this] { m_timeline->zoomBy(0.8); });
    add("Zoom +", { QKeySequence("Ctrl+=") }, "Zoom in", [this] { m_timeline->zoomBy(1.25); });
    add("Fit", { QKeySequence("Ctrl+0") }, "Fit the whole timeline", [this] { m_timeline->zoomToFit(); });

    auto* spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    bar->addWidget(spacer);

    QAction* exportAction = add("Export", { QKeySequence("Ctrl+E") }, "Export to MP4", &MainWindow::exportVideo);
    if (auto* button = qobject_cast<QToolButton*>(bar->widgetForAction(exportAction)))
        button->setStyleSheet("QToolButton { background: #2fc6b4; color: black; font-weight: bold;"
                              " padding: 4px 14px; border-radius: 4px; }");

    // Space works from anywhere in the window
    auto* play = new QAction(this);
    play->setShortcut(QKeySequence(Qt::Key_Space));
    connect(play, &QAction::triggered, this, &MainWindow::togglePlay);
    addAction(play);
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

void MainWindow::importFiles(const QStringList& paths)
{
    if (paths.isEmpty())
        return;

    int added = 0;
    QStringList failures;
    for (const QString& path : paths) {
        QString error;
        if (addMediaItem(path, &error))
            ++added;
        else
            failures << QString("%1 — %2").arg(QFileInfo(path).fileName(), error);
    }

    statusBar()->showMessage(QString("Imported %1 file(s)").arg(added), 5000);
    if (!failures.isEmpty())
        QMessageBox::warning(this, "Some files didn't import", failures.join('\n'));
}

bool MainWindow::addMediaItem(const QString& path, QString* error)
{
    ve_media_info info;
    int rc = ve_probe(path.toUtf8().constData(), &info);
    if (rc != VE_OK) {
        *error = ve_error_string(rc);
        return false;
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

    auto* item = new QListWidgetItem(QIcon(makeThumbnail(path, info)), label);
    item->setToolTip(name + "\n" + details.join('\n') + "\n\nDrag onto the timeline, or double-click to add it to the end");
    item->setData(MediaBin::PathRole, path);
    item->setData(MediaBin::DurationRole, info.duration_sec);
    item->setData(MediaBin::HasVideoRole, info.has_video != 0);
    item->setData(MediaBin::HasAudioRole, info.has_audio != 0);
    item->setData(MediaBin::WidthRole, info.width);
    item->setData(MediaBin::HeightRole, info.height);
    item->setData(MediaBin::FpsRole, info.fps);
    m_mediaBin->addItem(item);
    return true;
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

void MainWindow::onClipsChanged()
{
    QList<RenderClip> clips = m_timeline->renderClips();
    m_renderer->setClips(clips);
    m_audio->setClips(clips);

    Project p = project();
    m_preview->setAspect(double(p.width) / p.height);
    if (m_timeline->clips().isEmpty())
        m_preview->setFrame({});
    requestPreview();
    updateTimeLabel();
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

    ExportDialog dialog(QSize(p.width, p.height), p.fps, suggested, this);
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
    QList<RenderClip> clips = m_timeline->renderClips();
    QByteArray path = dialog.path().toUtf8();
    ve_export_settings settings {};
    settings.width = dialog.size().width();
    settings.height = dialog.size().height();
    settings.fps = dialog.fps();
    settings.crf = dialog.crf();
    settings.force_software = dialog.useGraphicsCard() ? 0 : 1;

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
                                 job->encoder == "libx264" ? QStringLiteral("the CPU") : QStringLiteral("the graphics card")),
                        QMessageBox::Ok, this);
        QPushButton* show = box.addButton("Show in folder", QMessageBox::ActionRole);
        box.exec();
        if (box.clickedButton() == show)
            showInFileManager(dialog.path());
    } else if (job->result != VE_ERR_CANCELLED) {
        QMessageBox::warning(this, "Export failed", QString("Export failed: %1").arg(ve_error_string(job->result)));
    }
}

// ---- Testing helper ----

void MainWindow::runDemo(const QStringList& paths, const QString& screenshotPath)
{
    importFiles(paths);
    QList<TimelineClip> clips;
    for (int i = 0; i < m_mediaBin->count(); ++i)
        clips << m_mediaBin->clipFor(m_mediaBin->item(i));
    m_timeline->appendClips(clips);
    m_timeline->setPlayhead(m_timeline->duration() * 0.4);
    onPlayheadMoved(m_timeline->playhead());

    // Let things load, play for 2 seconds, then take the picture
    QTimer::singleShot(1500, this, [this] {
        qInfo("demo: playing from %.3f", m_timeline->playhead());
        startPlayback();
    });
    QTimer::singleShot(3500, this, [this, screenshotPath] {
        stopPlayback();
        qInfo("demo: stopped at %.3f", m_timeline->playhead());
    });
    QTimer::singleShot(4000, this, [this, screenshotPath] {
        grab().save(screenshotPath);
        qApp->quit();
    });
}
