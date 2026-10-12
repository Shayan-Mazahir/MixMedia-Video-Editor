// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "AppPaths.h"
#include "QuietCut.h"
#include "HelpWindow.h"
#include "Theme.h"

#include <ve/engine.h>

#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>

namespace QuietCut {

float autoThreshold(const QVector<float>& levels)
{
    QVector<float> heard;
    for (float l : levels)
        if (l > -99.0f)
            heard << l;
    if (heard.size() < 10)
        return -45.0f;
    std::sort(heard.begin(), heard.end());
    const float background = heard[heard.size() / 10];   // the quiet end
    const float talking = heard[heard.size() * 9 / 10];  // the loud end
    // A third of the way up from the background (and never right on top of it)
    return std::max(background + 4.0f, background + (talking - background) / 3.0f);
}

QList<QPair<double, double>> quietRanges(const QVector<float>& levels, float threshold, double minQuiet, double keep)
{
    const int n = int(levels.size());
    QVector<bool> quiet(n);
    for (int i = 0; i < n; ++i)
        quiet[i] = levels[i] < threshold;
    // A click or a breath in the middle of a pause doesn't count as talking
    const int blip = int(std::lround(0.12 / Step));
    for (int i = 0; i < n;) {
        if (quiet[i]) {
            ++i;
            continue;
        }
        int end = i;
        while (end < n && !quiet[end])
            ++end;
        if (i > 0 && end < n && end - i <= blip)
            std::fill(quiet.begin() + i, quiet.begin() + end, true);
        i = end;
    }

    QList<QPair<double, double>> ranges;
    const double total = n * Step;
    for (int i = 0; i < n;) {
        if (!quiet[i]) {
            ++i;
            continue;
        }
        int end = i;
        while (end < n && quiet[end])
            ++end;
        const double from = i * Step, to = end * Step;
        if (to - from >= minQuiet) {
            // Breathing room either side, except at the very start and end (nothing to breathe next to)
            const double a = i == 0 ? 0.0 : from + keep;
            const double b = end == n ? total : to - keep;
            if (b - a >= 0.05)
                ranges << qMakePair(a, b);
        }
        i = end;
    }
    return ranges;
}

namespace {

QString clock(double seconds)
{
    const int s = int(std::lround(seconds));
    return s >= 3600 ? QString("%1:%2:%3").arg(s / 3600).arg(s / 60 % 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'))
                     : QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}

// How loud the timeline is all the way through (on its own thread, with a progress bar).
// Empty = cancelled.
QVector<float> listen(QWidget* parent, const QList<RenderClip>& clips)
{
    struct Job {
        std::atomic<double> done { 0.0 };
        std::atomic<bool> cancel { false };
        QVector<float> levels;
    };
    auto job = std::make_shared<Job>();
    QThread* worker = QThread::create([job, clips] {
        ve_timeline* tl = ve_timeline_create();
        applyClips(tl, clips);
        job->levels.resize(int(std::ceil(ve_timeline_duration(tl) / Step)));
        int got = ve_timeline_loudness(tl, Step, job->levels.data(), int(job->levels.size()),
                                       [](double done, void* user) {
                                           auto* j = static_cast<Job*>(user);
                                           j->done = done;
                                           return j->cancel ? 1 : 0;
                                       },
                                       job.get());
        job->levels.resize(std::max(0, got));
        ve_timeline_destroy(tl);
    });
    QProgressDialog progress("Listening for the quiet bits…", "Cancel", 0, 1000, parent);
    progress.setWindowTitle("Cut out the quiet bits");
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(300);
    QObject::connect(&progress, &QProgressDialog::canceled, [job] { job->cancel = true; });
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &progress, [&] { progress.setValue(int(job->done * 1000)); });
    QEventLoop loop;
    QObject::connect(worker, &QThread::finished, &loop, &QEventLoop::quit);
    worker->start(QThread::LowPriority);
    poll.start(100);
    loop.exec();
    poll.stop();
    progress.reset();
    delete worker;
    return job->cancel ? QVector<float> {} : job->levels;
}

} // namespace

QList<QPair<double, double>> run(QWidget* parent, const QList<RenderClip>& clips)
{
    const QVector<float> levels = listen(parent, clips);
    if (levels.isEmpty())
        return {};
    // No sound at all (or hardly any)? Then it'd all count as quiet, and that's not what anyone means
    const auto heard = std::count_if(levels.begin(), levels.end(), [](float l) { return l > -60.0f; });
    if (heard < levels.size() / 50) {
        QMessageBox::information(parent, "Cut out the quiet bits",
                                 "There's no sound on the timeline to listen to. Is the sound turned off on the clips?");
        return {};
    }
    const float automatic = autoThreshold(levels);
    const double total = levels.size() * Step;

    QDialog dialog(parent);
    dialog.setWindowTitle("Cut out the quiet bits");
    QSettings remembered = AppPaths::settings();

    auto* minQuiet = new QDoubleSpinBox;
    minQuiet->setRange(0.2, 10.0);
    minQuiet->setSingleStep(0.1);
    minQuiet->setDecimals(1);
    minQuiet->setSuffix(" s");
    minQuiet->setValue(remembered.value("quietCut/minQuiet", 0.6).toDouble());
    minQuiet->setToolTip("Shorter pauses than this are left alone (they're just the gaps between words).");

    auto* keep = new QDoubleSpinBox;
    keep->setRange(0.0, 1.0);
    keep->setSingleStep(0.05);
    keep->setDecimals(2);
    keep->setSuffix(" s");
    keep->setValue(remembered.value("quietCut/keep", 0.15).toDouble());
    keep->setToolTip("A little of each pause is kept either side, so it doesn't sound chopped.");

    // How quiet counts as quiet, around the level worked out for this video
    auto* level = new QSlider(Qt::Horizontal);
    level->setRange(-12, 12);
    level->setValue(remembered.value("quietCut/level", 0).toInt());
    level->setToolTip("Left: only near-silence counts. Right: quiet talking counts too.");
    auto* levelRow = new QHBoxLayout;
    auto* quieter = new QLabel("Only silence");
    auto* louder = new QLabel("Quiet talking too");
    for (QLabel* l : { quieter, louder })
        l->setProperty("role", "hint");
    levelRow->addWidget(quieter);
    levelRow->addWidget(level, 1);
    levelRow->addWidget(louder);

    auto* found = new QLabel;
    found->setWordWrap(true);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Help);
    QPushButton* go = buttons->button(QDialogButtonBox::Ok);
    go->setText("Cut them out");

    QList<QPair<double, double>> ranges;
    auto update = [&] {
        ranges = quietRanges(levels, automatic + float(level->value()), minQuiet->value(), keep->value());
        double cut = 0;
        for (const auto& r : ranges)
            cut += r.second - r.first;
        found->setText(ranges.isEmpty()
                           ? QString("No pauses that long. Try a shorter time, or move the slider right.")
                           : QString("<b>Found %1 pause%2, %3 in all.</b> The video goes from %4 to %5.")
                                 .arg(ranges.size()).arg(ranges.size() == 1 ? "" : "s").arg(clock(cut))
                                 .arg(clock(total)).arg(clock(total - cut)));
        go->setEnabled(!ranges.isEmpty());
    };
    QObject::connect(minQuiet, &QDoubleSpinBox::valueChanged, &dialog, update);
    QObject::connect(keep, &QDoubleSpinBox::valueChanged, &dialog, update);
    QObject::connect(level, &QSlider::valueChanged, &dialog, update);
    update();

    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::helpRequested, [] { HelpWindow::open("timeline.md"); });

    auto* form = new QFormLayout;
    form->addRow("Pauses longer than", minQuiet);
    form->addRow("Leave either side", keep);
    form->addRow("What counts as quiet", levelRow);
    auto* note = new QLabel("Cuts the pauses out of every track at once, so the picture, sound and titles stay "
                            "in sync. One Ctrl+Z puts it all back.");
    note->setWordWrap(true);
    note->setProperty("role", "hint");
    auto* layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(found);
    layout->addWidget(note);
    layout->addWidget(buttons);
    dialog.setMinimumWidth(480);
    Theme::fit(dialog);
    if (dialog.exec() != QDialog::Accepted)
        return {};
    remembered.setValue("quietCut/minQuiet", minQuiet->value());
    remembered.setValue("quietCut/keep", keep->value());
    remembered.setValue("quietCut/level", level->value());
    return ranges;
}

} // namespace QuietCut
