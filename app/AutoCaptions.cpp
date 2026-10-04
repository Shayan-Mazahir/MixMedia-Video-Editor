// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "AutoCaptions.h"
#include "AppSettings.h"
#include "HelpWindow.h"

#include <ve/engine.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <atomic>
#include <memory>
#include <mutex>

namespace AutoCaptions {

QList<Model> models()
{
    return {
        { "tiny", "Fast (rough, but quick)", 78 },
        { "base", "Good (the usual pick)", 148 },
        { "small", "Better (slower)", 488 },
        { "large-v3-turbo", "Best (slowest, wants a strong computer)", 1624 },
    };
}

QString modelFolder()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("models");
}

QString modelPath(const QString& id)
{
    return QDir(modelFolder()).filePath("ggml-" + id + ".bin");
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
            bool sentenceEnded = last.text.endsWith('.') || last.text.endsWith('?') || last.text.endsWith('!');
            bool pause = w.start - last.end > 0.7;
            bool tooLong = length() + w.text.size() > maxChars || w.end - current.first().start > maxSeconds;
            // (a comma is a nice place to break a long line)
            bool niceBreak = last.text.endsWith(',') && length() > maxChars * 0.6;
            if (w.startsSentence || sentenceEnded || pause || tooLong || niceBreak)
                finish();
        }
        current << w;
    }
    finish();
    // Linger a moment after the last word (easier to read), but never into the next line
    for (int i = 0; i < lines.size(); ++i) {
        double room = i + 1 < lines.size() ? lines[i + 1].start - lines[i].end() : 1.0;
        lines[i].duration += std::clamp(room, 0.0, 0.4);
    }
    return lines;
}

namespace {

// Gets the model file, showing how it's going. False = cancelled or failed (and says why).
bool download(QWidget* parent, const Model& model)
{
    QDir().mkpath(modelFolder());
    const QString target = modelPath(model.id);
    const QString partial = target + ".part";
    QFile file(partial);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(parent, "Couldn't download", "Couldn't write to " + modelFolder());
        return false;
    }
    QNetworkAccessManager network;
    QNetworkRequest request(QUrl("https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-" + model.id + ".bin"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = network.get(request);

    QProgressDialog progress(QString("Downloading the %1 speech model (%2 MB)…").arg(model.id).arg(model.megabytes),
                             "Cancel", 0, 1000, parent);
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
    const bool ok = reply->error() == QNetworkReply::NoError && file.size() > 1000000;
    const QString why = reply->errorString();
    reply->deleteLater();
    progress.reset();
    if (!ok) {
        QFile::remove(partial);
        if (!progress.wasCanceled())
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
    QSettings remembered;
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
    auto* quality = new QComboBox;
    for (const Model& m : models()) {
        bool here = QFileInfo::exists(modelPath(m.id));
        quality->addItem(QString("%1 · %2 MB%3").arg(m.name).arg(m.megabytes).arg(here ? "  ✓ downloaded" : ""), m.id);
    }
    quality->setCurrentIndex(std::max(0, quality->findData(remembered.value("captions/model", "base"))));
    auto* replaceThem = new QRadioButton("Replace the subtitles that are there");
    auto* addThem = new QRadioButton("Keep them, and add these too");
    replaceThem->setChecked(true);

    auto* form = new QFormLayout;
    form->addRow("Spoken in", language);
    form->addRow("", translate);
    form->addRow("Quality", quality);
    if (haveSubtitles) {
        auto* choice = new QVBoxLayout;
        choice->addWidget(replaceThem);
        choice->addWidget(addThem);
        form->addRow("Already got some", choice);
    }
    auto* note = new QLabel("Listens to the whole timeline, as edited, and writes subtitles that line up with it. "
                            "It all happens on this computer. Have a read through the result in the Subtitles tab, "
                            "it's good but not perfect.");
    note->setWordWrap(true);
    note->setStyleSheet("color: #808286; font-size: 11px;");
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Help);
    buttons->button(QDialogButtonBox::Ok)->setText("Start");
    QObject::connect(buttons, &QDialogButtonBox::helpRequested, [] { HelpWindow::open("subtitles.md"); });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(note);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return {};
    remembered.setValue("captions/language", language->currentData());
    remembered.setValue("captions/translate", translate->isChecked());
    remembered.setValue("captions/model", quality->currentData());
    *replace = !haveSubtitles || replaceThem->isChecked();

    // ---- The speech model (asks before downloading) ----
    const Model model = models().at(quality->currentIndex());
    if (!QFileInfo::exists(modelPath(model.id))) {
        auto answer = QMessageBox::question(
            parent, "Download the speech model?",
            QString("Auto-captions need a speech model to listen with. The \"%1\" one is %2 MB, downloaded once "
                    "from Hugging Face (where whisper's models live) and kept for next time.\n\nDownload it now?")
                .arg(model.id).arg(model.megabytes));
        if (answer != QMessageBox::Yes || !download(parent, model))
            return {};
    }

    // ---- Listen (on its own thread, so the window stays responsive) ----
    struct Job {
        std::atomic<double> done { 0.0 };
        std::atomic<bool> cancel { false };
        std::mutex lock;
        QList<Word> words;
        int result = VE_OK;
    };
    auto job = std::make_shared<Job>();
    const QByteArray path = modelPath(model.id).toUtf8();
    const QByteArray lang = language->currentData().toString().toUtf8();
    const bool toEnglish = translate->isChecked();
    QThread* worker = QThread::create([job, clips, path, lang, toEnglish] {
        ve_timeline* tl = ve_timeline_create();
        applyClips(tl, clips);
        ve_caption_settings s {};
        s.model_path = path.constData();
        s.language = lang.constData();
        s.translate = toEnglish;
        s.threads = AppSettings::jobThreads();
        s.use_gpu = AppSettings::gpuForCaptions();
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
    progress.setMinimumWidth(380);
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
        progress.setLabelText(text);
    });
    QEventLoop loop;
    QObject::connect(worker, &QThread::finished, &loop, &QEventLoop::quit);
    worker->start();
    poll.start(200);
    loop.exec();
    poll.stop();
    progress.reset();
    delete worker;

    if (job->result == VE_ERR_CANCELLED)
        return {};
    if (job->result != VE_OK) {
        QMessageBox::warning(parent, "Auto-captions didn't work",
                             job->result == VE_ERR_OPEN ? "Couldn't load the speech model. Try another quality, "
                                                          "or delete it from " + modelFolder() + " to download it again."
                                                        : QString("Something went wrong: %1").arg(ve_error_string(job->result)));
        return {};
    }
    QList<TimelineClip> lines = linesFromWords(job->words);
    if (lines.isEmpty())
        QMessageBox::information(parent, "Auto-captions", "Didn't hear any talking.");
    return lines;
}

} // namespace AutoCaptions
