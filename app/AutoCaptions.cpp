// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "AppPaths.h"
#include "AutoCaptions.h"
#include "AppSettings.h"
#include "HelpWindow.h"
#include "Theme.h"

#include <ve/engine.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <atomic>
#include <memory>
#include <mutex>

namespace AutoCaptions {

QList<Model> models()
{
    return {
        { "base-q5_1", "Fast (quick and small, the usual pick)", 57 },
        { "small-q5_1", "Good (fewer mistakes)", 182 },
        { "large-v3-turbo-q5_0", "Great (needs a decent computer)", 548 },
        { "large-v3-q5_0", "Best (most accurate, slowest)", 1031 },
    };
}

QString modelFolder()
{
    return AppPaths::models();
}

QString modelPath(const QString& id)
{
    return QDir(modelFolder()).filePath("ggml-" + id + ".bin");
}

QString detectorPath()
{
    return QDir(modelFolder()).filePath("ggml-silero-v5.1.2.bin");
}

QStringList ownModels()
{
    QStringList ours;
    for (const Model& m : models())
        ours << QFileInfo(modelPath(m.id)).fileName();
    QStringList found;
    for (const QFileInfo& f : QDir(modelFolder()).entryInfoList({ "ggml-*.bin" }, QDir::Files, QDir::Name))
        if (!ours.contains(f.fileName()) && !f.fileName().startsWith("ggml-silero"))
            found << f.absoluteFilePath();
    for (const QString& path : AppPaths::settings().value("captions/ownModels").toStringList())
        if (QFileInfo::exists(path) && !found.contains(path))
            found << path;
    return found;
}

void addOwnModel(const QString& path)
{
    QSettings s = AppPaths::settings();
    QStringList list = s.value("captions/ownModels").toStringList();
    list.removeAll(path);
    list.prepend(path);
    s.setValue("captions/ownModels", list);
}

QList<TimelineClip> linesFromWords(const QList<Word>& words, int maxChars, double maxSeconds)
{
    QList<TimelineClip> lines;
    QList<Word> current;
    auto finish = [&] {
        if (current.isEmpty())
            return;
        TimelineClip c;
        c.kind = TimelineClip::Kind::Subtitle;
        c.title = TitleStyle::subtitles();
        QStringList text;
        c.start = current.first().start;
        for (const Word& w : current) {
            text << w.text;
            c.words << WordTime { w.text, w.start - c.start, w.end - c.start };
        }
        c.title.text = text.join(' ');
        c.name = c.title.text;
        c.duration = std::max(0.3, current.last().end - c.start);
        lines << c;
        current.clear();
    };
    auto length = [&] {
        int n = 0;
        for (const Word& w : current)
            n += int(w.text.size()) + 1;
        return n;
    };
    for (const Word& w : words) {
        if (!current.isEmpty()) {
            const Word& last = current.last();
            // (۔ and ؟ end sentences in Urdu, । in Hindi)
            static const QString enders = QString::fromUtf8(".?!۔؟।");
            bool sentenceEnded = !last.text.isEmpty() && enders.contains(last.text.back());
            bool pause = w.start - last.end > 0.7;
            bool tooLong = length() + w.text.size() > maxChars || w.end - current.first().start > maxSeconds;
            // (a comma is a nice place to break a long line)
            bool niceBreak = (last.text.endsWith(',') || last.text.endsWith(QChar(0x060C))) && length() > maxChars * 0.6;
            if (w.startsSentence || sentenceEnded || pause || tooLong || niceBreak)
                finish();
        }
        current << w;
    }
    finish();
    // Linger a moment after the last word (easier to read), and stay up at least a second so a
    // short line like "Yes." doesn't just flash past. Never into the next line, though.
    const double MinOnScreen = 1.0;
    for (int i = 0; i < lines.size(); ++i) {
        const double untilNext = i + 1 < lines.size() ? lines[i + 1].start - lines[i].start : 1e9;
        double room = i + 1 < lines.size() ? lines[i + 1].start - lines[i].end() : 1.0;
        lines[i].duration += std::clamp(room, 0.0, 0.4);
        lines[i].duration = std::max(lines[i].duration, std::min(MinOnScreen, untilNext));
    }
    // Whisper's timing for a line's last word can run a touch past where the next line starts:
    // one subtitle at a time, so it stops where the next begins
    for (int i = 0; i + 1 < lines.size(); ++i)
        if (lines[i].end() > lines[i + 1].start)
            lines[i].duration = std::max(0.3, lines[i + 1].start - lines[i].start);
    return lines;
}

namespace {

// Gets a file from Hugging Face (where whisper's models live), showing how it's going.
// False = cancelled or failed (and says why, unless `quiet`).
bool download(QWidget* parent, const QString& url, const QString& target, const QString& what, bool quiet = false)
{
    QDir().mkpath(modelFolder());
    const QString partial = target + ".part";
    QFile file(partial);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (!quiet)
            QMessageBox::warning(parent, "Couldn't download", "Couldn't write to " + modelFolder());
        return false;
    }
    QNetworkAccessManager network;
    QNetworkRequest request { QUrl(url) };
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = network.get(request);

    QProgressDialog progress("Downloading " + what + "…", "Cancel", 0, 1000, parent);
    progress.setWindowTitle("Auto-captions");
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    QObject::connect(reply, &QNetworkReply::readyRead, &file, [&] { file.write(reply->readAll()); });
    QObject::connect(reply, &QNetworkReply::downloadProgress, &progress, [&](qint64 got, qint64 total) {
        if (total > 0)
            progress.setValue(int(got * 1000 / total));
    });
    QObject::connect(&progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    file.write(reply->readAll());
    file.close();
    const bool ok = reply->error() == QNetworkReply::NoError && file.size() > 100000;
    const QString why = reply->errorString();
    reply->deleteLater();
    progress.reset();
    if (!ok) {
        QFile::remove(partial);
        if (!progress.wasCanceled() && !quiet)
            QMessageBox::warning(parent, "Couldn't download", "The download didn't work:\n" + why);
        return false;
    }
    QFile::remove(target);
    return QFile::rename(partial, target);
}

} // namespace

QList<TimelineClip> run(QWidget* parent, const QList<RenderClip>& clips, bool haveSubtitles, bool* replace)
{
    if (!ve_captions_available()) {
        QMessageBox::information(parent, "Auto-captions", "This copy of MixMedia was built without auto-captions.");
        return {};
    }

    // ---- What to do ----
    QDialog dialog(parent);
    dialog.setWindowTitle("Auto-captions");
    dialog.setMinimumWidth(460);
    QSettings remembered = AppPaths::settings();
    auto* language = new QComboBox;
    const QList<QPair<QString, QString>> languages = {
        { "en", "English" }, { "auto", "Work it out (any language)" }, { "ar", "Arabic" }, { "bn", "Bengali" },
        { "zh", "Chinese" }, { "nl", "Dutch" }, { "fr", "French" }, { "de", "German" }, { "hi", "Hindi" },
        { "id", "Indonesian" }, { "it", "Italian" }, { "ja", "Japanese" }, { "ko", "Korean" }, { "fa", "Persian" },
        { "pl", "Polish" }, { "pt", "Portuguese" }, { "pa", "Punjabi" }, { "ru", "Russian" }, { "es", "Spanish" },
        { "sv", "Swedish" }, { "tr", "Turkish" }, { "uk", "Ukrainian" }, { "ur", "Urdu" }, { "vi", "Vietnamese" },
    };
    for (const auto& [code, name] : languages)
        language->addItem(name, code);
    language->setCurrentIndex(std::max(0, language->findData(remembered.value("captions/language", "en"))));
    auto* translate = new QCheckBox("Write the subtitles in English (translate)");
    translate->setChecked(remembered.value("captions/translate", false).toBool());
    // Quality: our models, then your own, then a way to add one
    auto* quality = new QComboBox;
    auto fillQuality = [=](const QString& select) {
        QSignalBlocker hush(quality);
        quality->clear();
        const QList<Model> all = models();
        for (int i = 0; i < all.size(); ++i) {
            const Model& m = all[i];
            bool here = QFileInfo::exists(modelPath(m.id));
            quality->addItem(QString("%1 · %2 MB%3").arg(m.name).arg(m.megabytes).arg(here ? "  ✓ downloaded" : ""),
                             QString("model:%1").arg(i));
        }
        const QStringList own = ownModels();
        if (!own.isEmpty())
            quality->insertSeparator(quality->count());
        for (const QString& path : own)
            quality->addItem("Your model: " + QFileInfo(path).fileName(), "file:" + path);
        quality->insertSeparator(quality->count());
        quality->addItem("Add a model file…", "add");
        quality->setCurrentIndex(std::max(0, quality->findData(select)));
    };
    QString lastModel = remembered.value("captions/model", "model:0").toString(); // (Fast: small and quick)
    if (!lastModel.contains(':')) // (picked before 0.7: the old names)
        lastModel = lastModel == "large-v3-turbo" ? "model:2" : lastModel == "small" ? "model:1" : "model:0";
    fillQuality(lastModel);
    auto lastPick = std::make_shared<QString>(quality->currentData().toString());
    QObject::connect(quality, &QComboBox::activated, &dialog, [=, &dialog] {
        if (quality->currentData() != "add") {
            *lastPick = quality->currentData().toString();
            return;
        }
        const QString path = QFileDialog::getOpenFileName(&dialog, "Pick a whisper model", QDir::homePath(),
                                                          "Whisper models (*.bin);;All files (*)");
        if (!path.isEmpty()) {
            addOwnModel(path);
            *lastPick = "file:" + path;
        }
        fillQuality(*lastPick);
    });

    // Turbo was trained without translating, so it makes a mess of it: say so
    auto* turboWarning = new QLabel("“Great” can't translate (it was never taught to). For subtitles in "
                                    "English, pick “Best”, or untick translate to get them in the language spoken.");
    turboWarning->setWordWrap(true);
    turboWarning->setStyleSheet(QString("color: %1;").arg(QColor(0xf0, 0xa0, 0x30).name()));
    auto checkTurbo = [=] {
        const QString pick = quality->currentData().toString();
        const bool turbo = pick.startsWith("model:") ? models().value(pick.mid(6).toInt()).id.contains("turbo")
                                                     : pick.contains("turbo", Qt::CaseInsensitive);
        turboWarning->setVisible(translate->isChecked() && turbo);
    };
    QObject::connect(quality, &QComboBox::currentIndexChanged, turboWarning, checkTurbo);
    QObject::connect(translate, &QCheckBox::toggled, turboWarning, checkTurbo);
    checkTurbo();

    auto* hint = new QLineEdit(remembered.value("captions/hint").toString());
    hint->setPlaceholderText("e.g. MixMedia, Shayan, CSS (optional)");
    hint->setToolTip("Names and unusual words that get said. Whisper reads these first, so it spells them right.");
    auto* careful = new QCheckBox("Take extra care (more accurate, about twice as slow)");
    careful->setChecked(remembered.value("captions/careful", false).toBool());
    auto* onlyTalking = new QCheckBox("Only listen to the talking (skips silence and music: quicker, fewer made-up words)");
    onlyTalking->setChecked(remembered.value("captions/onlyTalking", true).toBool());
    auto* replaceThem = new QRadioButton("Replace the subtitles that are there");
    auto* addThem = new QRadioButton("Keep them, and add these too");
    replaceThem->setChecked(true);

    auto* form = new QFormLayout;
    form->addRow("Spoken in", language);
    form->addRow("", translate);
    form->addRow("Quality", quality);
    form->addRow("", turboWarning);
    form->addRow("Names and words", hint);
    form->addRow("", careful);
    form->addRow("", onlyTalking);
    if (haveSubtitles) {
        auto* choice = new QVBoxLayout;
        choice->addWidget(replaceThem);
        choice->addWidget(addThem);
        form->addRow("Already got some", choice);
    }
    auto* note = new QLabel("Pick the language that's really spoken: the wrong one makes it slow and muddled. "
                            "It listens to the whole timeline, as edited, all on this computer. Have a read through "
                            "the result in the Subtitles tab, it's good but not perfect.");
    note->setWordWrap(true);
    note->setProperty("role", "hint");
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Help);
    buttons->button(QDialogButtonBox::Ok)->setText("Start");
    QObject::connect(buttons, &QDialogButtonBox::helpRequested, [] { HelpWindow::open("subtitles.md"); });
    QPushButton* folder = buttons->addButton("Models folder", QDialogButtonBox::ResetRole);
    folder->setToolTip("Where the speech models are kept. Delete them there to free up space, or drop your own in.");
    QObject::connect(folder, &QPushButton::clicked, &dialog, [&dialog] { AppPaths::openFolder(&dialog, modelFolder()); });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(note);
    layout->addWidget(buttons);
    Theme::fit(dialog);
    if (dialog.exec() != QDialog::Accepted)
        return {};
    remembered.setValue("captions/language", language->currentData());
    remembered.setValue("captions/translate", translate->isChecked());
    const QString pick = quality->currentData().toString();
    if (pick == "add")
        return {};
    remembered.setValue("captions/model", pick);
    remembered.setValue("captions/hint", hint->text().trimmed());
    remembered.setValue("captions/careful", careful->isChecked());
    remembered.setValue("captions/onlyTalking", onlyTalking->isChecked());
    *replace = !haveSubtitles || replaceThem->isChecked();

    // ---- The speech model (asks before downloading) ----
    QString modelFile;
    const bool ownModel = pick.startsWith("file:");
    if (ownModel) {
        modelFile = pick.mid(5);
    } else {
        const Model model = models().value(pick.mid(6).toInt());
        modelFile = modelPath(model.id);
        if (!QFileInfo::exists(modelFile)) {
            const QString name = model.name.section(" (", 0, 0);
            auto answer = QMessageBox::question(
                parent, "Download the speech model?",
                QString("Auto-captions need a speech model to listen with. The \"%1\" one is %2 MB, downloaded once "
                        "from Hugging Face (where whisper's models live) and kept for next time.\n\nDownload it now?")
                    .arg(name).arg(model.megabytes));
            const QString url = "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/" + QFileInfo(modelFile).fileName();
            if (answer != QMessageBox::Yes
                || !download(parent, url, modelFile, QString("the \"%1\" speech model (%2 MB)").arg(name).arg(model.megabytes)))
                return {};
        }
    }
    // The speech detector: under a megabyte, so it just comes along (and if it can't, no matter)
    QString detector;
    if (onlyTalking->isChecked()) {
        if (!QFileInfo::exists(detectorPath()))
            download(parent, "https://huggingface.co/ggml-org/whisper-vad/resolve/main/ggml-silero-v5.1.2.bin",
                     detectorPath(), "the speech detector (1 MB)", true);
        if (QFileInfo::exists(detectorPath()))
            detector = detectorPath();
    }

    // ---- Listen (on its own thread, so the window stays responsive) ----
    struct Job {
        std::atomic<double> done { 0.0 };
        std::atomic<bool> cancel { false };
        std::mutex lock;
        QList<Word> words;
        QString heard; // the latest line, to show while it works
        int result = VE_OK;
    };
    auto job = std::make_shared<Job>();
    const QByteArray path = modelFile.toUtf8();
    const QByteArray lang = language->currentData().toString().toUtf8();
    const QByteArray vad = detector.toUtf8();
    const QByteArray words = hint->text().trimmed().toUtf8();
    const bool toEnglish = translate->isChecked(), extraCare = careful->isChecked();
    QThread* worker = QThread::create([job, clips, path, lang, toEnglish, vad, words, extraCare] {
        ve_timeline* tl = ve_timeline_create();
        applyClips(tl, clips);
        ve_caption_settings s {};
        s.model_path = path.constData();
        s.language = lang.constData();
        s.translate = toEnglish;
        s.threads = AppSettings::jobThreads();
        s.use_gpu = AppSettings::gpuForCaptions();
        s.vad_model_path = vad.isEmpty() ? nullptr : vad.constData();
        s.hint = words.isEmpty() ? nullptr : words.constData();
        s.careful = extraCare;
        s.heard = [](const char* text, void* user) {
            auto* j = static_cast<Job*>(user);
            std::lock_guard guard(j->lock);
            j->heard = QString::fromUtf8(text).trimmed();
        };
        job->result = ve_auto_captions(
            tl, &s,
            [](const ve_caption_word* w, void* user) {
                auto* j = static_cast<Job*>(user);
                std::lock_guard guard(j->lock);
                j->words << Word { w->start, w->end, QString::fromUtf8(w->text), w->starts_sentence != 0 };
            },
            [](double done, void* user) {
                auto* j = static_cast<Job*>(user);
                j->done = done;
                return j->cancel ? 1 : 0;
            },
            job.get());
        ve_timeline_destroy(tl);
    });

    QProgressDialog progress("Listening…", "Cancel", 0, 1000, parent);
    progress.setWindowTitle("Auto-captions");
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setMinimumWidth(460);
    auto* label = new QLabel; // (wraps long lines, rather than the window growing)
    label->setWordWrap(true);
    label->setMinimumHeight(QFontMetrics(label->font()).lineSpacing() * 4);
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    progress.setLabel(label);
    QObject::connect(&progress, &QProgressDialog::canceled, [job] { job->cancel = true; });
    QElapsedTimer clock;
    clock.start();
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &progress, [&] {
        double done = job->done;
        progress.setValue(int(done * 1000));
        QString text = done < 0.1 ? QStringLiteral("Getting the sound ready…") : QStringLiteral("Listening…");
        if (done > 0.15) {
            int left = int(clock.elapsed() / 1000.0 * (1.0 - done) / done);
            text += QString(" about %1:%2 left").arg(left / 60).arg(left % 60, 2, 10, QChar('0'));
        }
        // What it's just heard, so you can see it working (and spot straight away if it's the
        // wrong language, rather than finding out at the end)
        QString heard;
        {
            std::lock_guard guard(job->lock);
            heard = job->heard;
        }
        text = "<b>" + text.toHtmlEscaped() + "</b>";
        if (!heard.isEmpty()) {
            if (heard.size() > 90)
                heard = "…" + heard.right(89);
            text += QString("<br><span style='color:%1;'>Just heard: “%2”</span>")
                        .arg(Theme::colours().dim.name(), heard.toHtmlEscaped());
        }
        progress.setLabelText(text);
    });
    QEventLoop loop;
    QObject::connect(worker, &QThread::finished, &loop, &QEventLoop::quit);
    // Full speed when nothing else is going on, but everything else gets the CPU first
    // (on Linux, "idle" is the setting that really does that, and whisper's own threads inherit it)
    worker->start(QThread::IdlePriority);
    poll.start(200);
    loop.exec();
    poll.stop();
    progress.reset();
    delete worker;

    if (job->result == VE_ERR_CANCELLED)
        return {};
    if (job->result != VE_OK) {
        QMessageBox::warning(parent, "Auto-captions didn't work",
                             job->result == VE_ERR_OPEN
                                 ? (ownModel ? "Couldn't load that model. It needs to be a whisper.cpp model (a ggml .bin file)."
                                             : "Couldn't load the speech model. Try another quality, or delete it from "
                                                   + modelFolder() + " to download it again.")
                                                        : QString("Something went wrong: %1").arg(ve_error_string(job->result)));
        return {};
    }
    QList<TimelineClip> lines = linesFromWords(job->words);
    if (lines.isEmpty())
        QMessageBox::information(parent, "Auto-captions", "Didn't hear any talking.");
    return lines;
}

} // namespace AutoCaptions
