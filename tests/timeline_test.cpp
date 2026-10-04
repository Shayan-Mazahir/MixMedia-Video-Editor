// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

// Pretends to be a mouse and keyboard and makes sure the timeline edits do what they should.
// Run with: QT_QPA_PLATFORM=offscreen ./build/tests/timeline_test

#include "AutoCaptions.h"
#include "ClipInspector.h"
#include "ClipPresets.h"
#include "MainWindow.h"
#include "NumberSlider.h"
#include "PreviewWidget.h"
#include "SubtitleFile.h"
#include "SubtitlePanel.h"
#include "TitleRenderer.h"
#include "UpdateChecker.h"
#include "ProjectFile.h"
#include "TimelineWidget.h"

#include <QApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QDoubleSpinBox>
#include <QFile>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QSlider>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>

namespace {

// Layout numbers from TimelineWidget
constexpr int HeaderWidth = 90;
constexpr int RulerHeight = 28;
constexpr int TrackHeight = 58;
constexpr int FxTrackHeight = 40;
constexpr int WidgetWidth = 1000;

// The default tracks, top to bottom
constexpr int FX2 = 0, FX1 = 1, V2 = 2, V1 = 3, A1 = 4, A2 = 5;

int trackY(int track)
{
    int y = RulerHeight;
    for (int i = 0; i < track; ++i)
        y += i <= FX1 ? FxTrackHeight : TrackHeight;
    return y + (track <= FX1 ? FxTrackHeight : TrackHeight) / 2;
}

TimelineClip fakeVideo(double length)
{
    TimelineClip c;
    c.path = "does-not-exist.mp4"; // the filmstrip just shows black, that's fine here
    c.name = "test";
    c.sourceDuration = length;
    c.duration = length;
    c.hasVideo = c.hasAudio = true;
    c.width = 1920;
    c.height = 1080;
    c.fps = 30;
    return c;
}

void send(QWidget* w, QEvent::Type type, QPoint pos, Qt::MouseButtons buttons)
{
    Qt::MouseButton button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
    QMouseEvent e(type, pos, w->mapToGlobal(pos), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(w, &e);
}

void drag(QWidget* w, QPoint from, QPoint to)
{
    send(w, QEvent::MouseButtonPress, from, Qt::LeftButton);
    QPoint mid = (from + to) / 2;
    send(w, QEvent::MouseMove, mid, Qt::LeftButton);
    send(w, QEvent::MouseMove, to, Qt::LeftButton);
    send(w, QEvent::MouseButtonRelease, to, Qt::NoButton);
}

} // namespace

class TimelineTest : public QObject {
    Q_OBJECT

    TimelineWidget* tl = nullptr;
    double pps = 0; // pixels per second after zoom-to-fit

    int xAt(double sec) const { return int(HeaderWidth + sec * pps + 0.5); }
    const TimelineClip& clip(int i) const { return tl->clips().at(i); }

private slots:
    void init()
    {
        tl = new TimelineWidget;
        tl->resize(WidgetWidth, 300);
        tl->show();
        tl->appendClips({ fakeVideo(10) });
        pps = (WidgetWidth - HeaderWidth - 30) / 10.0; // what zoomToFit picks for 10s
    }

    void cleanup()
    {
        delete tl;
        tl = nullptr;
    }

    void appendGoesOnVideo1()
    {
        QCOMPARE(tl->clips().size(), 1);
        QCOMPARE(clip(0).track, V1);
        QCOMPARE(clip(0).start, 0.0);
    }

    void audioOnlyGoesOnAudioTrack()
    {
        TimelineClip song = fakeVideo(5);
        song.hasVideo = false;
        tl->appendClips({ song });
        QCOMPARE(clip(1).track, A1);
        QCOMPARE(clip(1).start, 0.0); // the audio track was empty
    }

    void splitThenUndoRedo()
    {
        tl->setPlayhead(4.0);
        tl->splitAtPlayhead();
        QCOMPARE(tl->clips().size(), 2);
        QCOMPARE(clip(0).duration, 4.0);
        QCOMPARE(clip(1).start, 4.0);
        QCOMPARE(clip(1).in, 4.0);
        QCOMPARE(clip(1).duration, 6.0);

        tl->undo();
        QCOMPARE(tl->clips().size(), 1);
        QCOMPARE(clip(0).duration, 10.0);
        tl->redo();
        QCOMPARE(tl->clips().size(), 2);
    }

    void trimRightEdge()
    {
        tl->setPlayhead(4.0);
        tl->splitAtPlayhead();
        // Drag the end of the first clip from 4s back to 2s
        drag(tl, { xAt(4.0) - 2, trackY(V1) }, { xAt(2.0), trackY(V1) });
        QVERIFY(qAbs(clip(0).duration - 2.0) < 0.02);
        QCOMPARE(clip(0).in, 0.0);

        // Now try dragging it way past the next clip: it should stop at 4s
        drag(tl, { xAt(clip(0).end()) - 2, trackY(V1) }, { xAt(8.0), trackY(V1) });
        QVERIFY(qAbs(clip(0).end() - 4.0) < 1e-9);
    }

    void trimLeftEdge()
    {
        tl->setPlayhead(4.0);
        tl->splitAtPlayhead();
        // Pull the start of the second clip from 4s to 6s: it should skip ahead in the file too
        drag(tl, { xAt(4.0) + 2, trackY(V1) }, { xAt(6.0), trackY(V1) });
        QVERIFY(qAbs(clip(1).start - 6.0) < 0.02);
        QVERIFY(qAbs(clip(1).in - 6.0) < 0.02);
        QVERIFY(qAbs(clip(1).end() - 10.0) < 1e-9); // the end didn't budge
    }

    void cantTrimPastStartOfFile()
    {
        // The whole file is already on the timeline, so pulling the left edge further left does nothing
        tl->setPlayhead(9.0); // keep the snap target out of the way
        drag(tl, { xAt(0.0) + 2, trackY(V1) }, { xAt(0.0) - 50, trackY(V1) });
        QCOMPARE(clip(0).start, 0.0);
        QCOMPARE(clip(0).in, 0.0);
        QCOMPARE(clip(0).duration, 10.0);
    }

    void moveToOtherTrackAndBack()
    {
        // Grab the middle and drop it on Video 2, a little later
        drag(tl, { xAt(5.0), trackY(V1) }, { xAt(7.0), trackY(V2) });
        QCOMPARE(clip(0).track, V2);
        QVERIFY(qAbs(clip(0).start - 2.0) < 0.02);
        tl->undo();
        QCOMPARE(clip(0).track, V1);
        QCOMPARE(clip(0).start, 0.0);
    }

    void movedClipsDontOverlap()
    {
        tl->setPlayhead(4.0);
        tl->splitAtPlayhead();
        // Drag the second clip (4-10s) back on top of the first one
        drag(tl, { xAt(7.0), trackY(V1) }, { xAt(5.0), trackY(V1) });
        QVERIFY(clip(1).start >= clip(0).end() - 1e-9);
    }

    void snapsToPlayhead()
    {
        tl->appendClips({ fakeVideo(2) }); // second clip at 10-12s, made a bit smaller
        tl->setPlayhead(5.0);
        // Drop it on Video 2 with its start a few pixels off the playhead, it should snap right onto it
        int grabX = xAt(11.0);
        int targetStartX = xAt(5.0) + 4;
        drag(tl, { grabX, trackY(V1) }, { targetStartX + (grabX - xAt(10.0)), trackY(V2) });
        QCOMPARE(clip(1).start, 5.0);
    }

    void clickSelectsAndDeleteRemoves()
    {
        QSignalSpy changed(tl, &TimelineWidget::clipsChanged);
        send(tl, QEvent::MouseButtonPress, { xAt(5.0), trackY(V1) }, Qt::LeftButton);
        send(tl, QEvent::MouseButtonRelease, { xAt(5.0), trackY(V1) }, Qt::NoButton);
        QCOMPARE(changed.count(), 0); // just clicking isn't an edit
        QTest::keyClick(tl, Qt::Key_Delete);
        QCOMPARE(tl->clips().size(), 0);
        QCOMPARE(changed.count(), 1);
    }

    void clickingRulerMovesPlayhead()
    {
        QSignalSpy moved(tl, &TimelineWidget::playheadMoved);
        send(tl, QEvent::MouseButtonPress, { xAt(3.0), 10 }, Qt::LeftButton);
        send(tl, QEvent::MouseButtonRelease, { xAt(3.0), 10 }, Qt::NoButton);
        QCOMPARE(moved.count(), 1);
        QVERIFY(qAbs(tl->playhead() - 3.0) < 0.02);
    }

    void detachAudioMakesASoundClip()
    {
        tl->detachAudio(); // the clip from init() is selected
        QCOMPARE(tl->clips().size(), 2);
        QVERIFY(!clip(0).audioOn);                 // picture stays, sound's gone from it
        QVERIFY(clip(1).audioOnly());              // the new one is just sound
        QCOMPARE(clip(1).track, A1);                // on Audio 1
        QCOMPARE(clip(1).start, clip(0).start);    // lined up exactly
        QCOMPARE(clip(1).duration, clip(0).duration);

        QList<RenderClip> r = tl->renderClips();
        QVERIFY(r[0].video && !r[0].audio);
        QVERIFY(!r[1].video && r[1].audio);

        tl->undo();
        QCOMPARE(tl->clips().size(), 1);
        QVERIFY(clip(0).audioOn);
    }

    void detachingAgainUsesTheOtherAudioTrack()
    {
        tl->appendClips({ fakeVideo(10) }); // 10-20s
        TimelineClip song = fakeVideo(30);
        song.hasVideo = false;
        tl->appendClips({ song }); // fills Audio 1 from 0-30s
        // Select the second video by clicking it, then detach: Audio 1 is busy, so Audio 2
        send(tl, QEvent::MouseButtonPress, { xAt(15.0), trackY(V1) }, Qt::LeftButton);
        send(tl, QEvent::MouseButtonRelease, { xAt(15.0), trackY(V1) }, Qt::NoButton);
        tl->detachAudio();
        QCOMPARE(tl->clips().size(), 4);
        QCOMPARE(clip(3).track, A2);
    }

    void titlesGoOnTopAtThePlayhead()
    {
        tl->setPlayhead(2.0);
        tl->addTitle();
        const TimelineClip& t = clip(1);
        QVERIFY(t.isTitle());
        QCOMPARE(t.track, FX1);
        QCOMPARE(t.start, 2.0);
        QCOMPARE(tl->selectedIndex(), 1);

        // The engine gets whatever picture file we make for it
        QList<RenderClip> r = tl->renderClips([](const TitleStyle&, int) { return QStringLiteral("/tmp/title.png"); });
        QCOMPARE(r[1].path, QStringLiteral("/tmp/title.png"));
        QVERIFY(r[1].layer > r[0].layer);
        QVERIFY(!r[1].audio);
    }

    void sliderDragIsOneUndoStep()
    {
        TimelineClip c = clip(0);
        for (int v = 90; v >= 10; v -= 10) { // like dragging a volume slider
            c.volume = v / 100.0f;
            tl->updateClip(0, c, "volume");
        }
        c.fadeIn = 1.5;
        tl->updateClip(0, c, "fadeIn"); // a different setting = a new step
        QCOMPARE(clip(0).fadeIn, 1.5);

        tl->undo();
        QCOMPARE(clip(0).fadeIn, 0.0);
        QCOMPARE(clip(0).volume, 0.1f);
        tl->undo();
        QCOMPARE(clip(0).volume, 1.0f); // the whole drag undone at once
    }

    void projectSaveAndOpen()
    {
        tl->setPlayhead(4.0);
        tl->splitAtPlayhead();
        tl->addTitle();
        TimelineClip t = clip(2);
        t.title.text = "Hello\nworld";
        t.title.color = QColor(255, 200, 0);
        t.fadeIn = 0.5;
        tl->updateClip(2, t, "text");

        QTemporaryDir dir;
        QString file = dir.filePath("test.mixmedia");
        ProjectFile::Data out;
        out.media = { "does-not-exist.mp4" };
        out.clips = tl->clips();
        out.playhead = 4.0;
        QString error;
        QVERIFY2(ProjectFile::save(file, out, &error), qPrintable(error));

        ProjectFile::Data in;
        QStringList missing;
        QVERIFY2(ProjectFile::load(file, &in, &missing, &error), qPrintable(error));
        QCOMPARE(in.clips.size(), 3);
        QCOMPARE(in.playhead, 4.0);
        QCOMPARE(in.clips[1].in, 4.0);
        QCOMPARE(in.clips[1].duration, 6.0);
        QVERIFY(in.clips[2].isTitle());
        QCOMPARE(in.clips[2].title.text, QStringLiteral("Hello\nworld"));
        QCOMPARE(in.clips[2].title.color, QColor(255, 200, 0));
        QCOMPARE(in.clips[2].fadeIn, 0.5);
        QVERIFY(!missing.isEmpty()); // our fake video really doesn't exist
    }

    void cutAndDeleteClosesTheGap()
    {
        // Cut out the 3-6s bit: split at 3, split at 6, delete the middle
        tl->setPlayhead(3.0);
        tl->splitAtPlayhead();
        tl->setPlayhead(6.0);
        tl->splitAtPlayhead();
        QCOMPARE(tl->clips().size(), 3);
        send(tl, QEvent::MouseButtonPress, { xAt(4.5), trackY(V1) }, Qt::LeftButton);
        send(tl, QEvent::MouseButtonRelease, { xAt(4.5), trackY(V1) }, Qt::NoButton);
        QTest::keyClick(tl, Qt::Key_Delete);

        QCOMPARE(tl->clips().size(), 2);
        // The last piece slid back to 3s, right up against the first one
        const TimelineClip* later = clip(0).start > clip(1).start ? &clip(0) : &clip(1);
        QCOMPARE(later->start, 3.0);
        QCOMPARE(later->in, 6.0); // but it still plays from 6s in the file
        QCOMPARE(tl->duration(), 7.0);
    }

    void shiftDeleteLeavesTheGap()
    {
        tl->setPlayhead(5.0);
        tl->splitAtPlayhead(); // selects the right piece (5-10s)
        tl->setPlayhead(2.0);
        send(tl, QEvent::MouseButtonPress, { xAt(1.0), trackY(V1) }, Qt::LeftButton);
        send(tl, QEvent::MouseButtonRelease, { xAt(1.0), trackY(V1) }, Qt::NoButton);
        QTest::keyClick(tl, Qt::Key_Delete, Qt::ShiftModifier);
        QCOMPARE(tl->clips().size(), 1);
        QCOMPARE(clip(0).start, 5.0); // didn't move
    }

    void detachedSoundStaysInSync()
    {
        tl->detachAudio();
        // Split with the video selected: the sound gets cut in the same spot
        send(tl, QEvent::MouseButtonPress, { xAt(5.0), trackY(V1) }, Qt::LeftButton);
        send(tl, QEvent::MouseButtonRelease, { xAt(5.0), trackY(V1) }, Qt::NoButton);
        tl->setPlayhead(4.0);
        tl->splitAtPlayhead();
        QCOMPARE(tl->clips().size(), 4);

        // Delete the first video piece: its sound goes too, and both tracks close up
        send(tl, QEvent::MouseButtonPress, { xAt(1.0), trackY(V1) }, Qt::LeftButton);
        send(tl, QEvent::MouseButtonRelease, { xAt(1.0), trackY(V1) }, Qt::NoButton);
        QTest::keyClick(tl, Qt::Key_Delete);
        QCOMPARE(tl->clips().size(), 2);
        for (const TimelineClip& c : tl->clips()) {
            QCOMPARE(c.start, 0.0);
            QCOMPARE(c.in, 4.0);
        }

        // Moving the video drags its sound along
        drag(tl, { xAt(3.0), trackY(V1) }, { xAt(5.0), trackY(V1) });
        QVERIFY(clip(0).start > 1.5);
        QCOMPARE(clip(0).start, clip(1).start);
    }

    void speedChangesLengthAndSlidesTheRest()
    {
        tl->appendClips({ fakeVideo(10) }); // second clip at 10-20s
        tl->setClipSpeed(0, 2.0);           // first clip twice as fast: 10s becomes 5s
        QCOMPARE(clip(0).duration, 5.0);
        QCOMPARE(clip(0).speed, 2.0);
        QCOMPARE(clip(1).start, 5.0);       // the next one slid back to meet it
        tl->undo();
        QCOMPARE(clip(0).duration, 10.0);
        QCOMPARE(clip(1).start, 10.0);

        tl->setClipSpeed(0, 0.5);           // slow motion: 20s long, pushes the next clip later
        QCOMPARE(clip(0).duration, 20.0);
        QCOMPARE(clip(1).start, 20.0);
    }

    void splittingASpedUpClipKeepsTheRightBitOfTheFile()
    {
        tl->setClipSpeed(0, 2.0); // 0-5s on the timeline, covering 0-10s of the file
        tl->setPlayhead(2.0);
        tl->splitAtPlayhead();
        QCOMPARE(clip(1).start, 2.0);
        QCOMPARE(clip(1).in, 4.0);  // 2s in at double speed = 4s into the file
        QCOMPARE(clip(1).duration, 3.0);
        QCOMPARE(clip(1).speed, 2.0);
    }

    void trimmingASpedUpClipStopsAtTheEndOfTheFile()
    {
        tl->setClipSpeed(0, 2.0); // already uses the whole 10s file, in 5s
        drag(tl, { xAt(5.0) - 2, trackY(V1) }, { xAt(8.0), trackY(V1) });
        QVERIFY(qAbs(clip(0).duration - 5.0) < 1e-9); // can't stretch past the end of the file
    }

    void effectsSurviveSaveAndOpen()
    {
        TimelineClip c = clip(0);
        c.look = 3;
        c.brightness = 0.25f;
        c.blur = 0.5f;
        c.scale = 0.4f;
        c.posX = 0.3f;
        c.opacity = 0.8f;
        tl->updateClip(0, c, "fx");
        tl->setClipSpeed(0, 1.5);

        QTemporaryDir dir;
        QString file = dir.filePath("fx.mixmedia");
        ProjectFile::Data out;
        out.clips = tl->clips();
        QString error;
        QVERIFY(ProjectFile::save(file, out, &error));
        ProjectFile::Data in;
        QVERIFY(ProjectFile::load(file, &in, nullptr, &error));
        const TimelineClip& r = in.clips[0];
        QCOMPARE(r.look, 3);
        QCOMPARE(r.brightness, 0.25f);
        QCOMPARE(r.blur, 0.5f);
        QCOMPARE(r.scale, 0.4f);
        QCOMPARE(r.posX, 0.3f);
        QCOMPARE(r.opacity, 0.8f);
        QCOMPARE(r.speed, 1.5);

        QList<RenderClip> rc = tl->renderClips();
        QCOMPARE(rc[0].look, 3);
        QCOMPARE(rc[0].speed, 1.5);
    }

    void numberSliderStaysInSync()
    {
        // Slider covers -100..100, but typing can go out to -200..200
        NumberSlider s(-200, 200, -100, 100, 1, 0, " %");
        QSignalSpy changed(&s, &NumberSlider::valueChanged);
        auto* box = s.findChild<QDoubleSpinBox*>();
        auto* slider = s.findChild<QSlider*>();

        box->setValue(150.5); // typed, past the slider's end
        QCOMPARE(s.value(), 150.5);
        QCOMPARE(slider->value(), 1000); // slider just pins at its end (100.0 x 10 steps)
        QCOMPARE(changed.count(), 1);

        slider->setValue(-255); // dragged to -25.5
        QCOMPARE(s.value(), -25.5);
        QCOMPARE(changed.last().first().toDouble(), -25.5);

        s.setValue(42.0); // set from code: no signal
        QCOMPARE(changed.count(), 2);
        QCOMPARE(slider->value(), 420);

        QTest::mouseDClick(slider, Qt::LeftButton); // double-click resets
        QCOMPARE(s.value(), 0.0);
    }

    void transitionsOverlapTheClips()
    {
        tl->detachAudio();
        tl->selectClip(0);
        tl->setPlayhead(5.0);
        tl->splitAtPlayhead(); // cuts the video and its sound at 5s
        int right = tl->selectedIndex();
        int rightSound = right + 1; // its sound got split straight after it
        tl->setTransition(right, VE_TRANSITION_DISSOLVE);

        // Like Filmora: the clip after the cut (and its sound) slides back 1s over the one before,
        // and the block covers that overlap on an FX track
        QCOMPARE(clip(right).start, 4.0);
        QCOMPARE(clip(rightSound).start, 4.0);
        const int blockIndex = int(tl->clips().size()) - 1;
        const TimelineClip& block = tl->clips().last();
        QVERIFY(block.isTransition());
        QCOMPARE(tl->tracks()[block.track].kind, TimelineTrack::Kind::Fx);
        QCOMPARE(block.start, 4.0);
        QCOMPARE(block.duration, 1.0);
        QVERIFY(tl->isOnCut(block));

        // The engine gets it on the clip that slid back (and its sound, for a crossfade)
        QList<RenderClip> r = tl->renderClips();
        int withDissolve = 0;
        for (const RenderClip& c : r)
            withDissolve += c.transition == VE_TRANSITION_DISSOLVE && c.start == 4.0;
        QCOMPARE(withDissolve, 2);
        QCOMPARE(r.size(), 4); // the block itself isn't drawn, it just tells the clips what to do

        // Saved and opened again: tracks, kinds and all
        QTemporaryDir dir;
        ProjectFile::Data out;
        out.clips = tl->clips();
        out.tracks = tl->tracks();
        QString error;
        QVERIFY(ProjectFile::save(dir.filePath("t.mixmedia"), out, &error));
        ProjectFile::Data in;
        QVERIFY(ProjectFile::load(dir.filePath("t.mixmedia"), &in, nullptr, &error));
        QCOMPARE(in.tracks, tl->tracks());
        QVERIFY(in.clips.last().isTransition());
        QCOMPARE(in.clips.last().transition, int(VE_TRANSITION_DISSOLVE));

        // Longer: the clip slides back further. The block stays glued to the overlap.
        TimelineClip longer = tl->clips()[blockIndex];
        longer.duration = 2.0;
        tl->updateClip(blockIndex, longer, "length");
        QCOMPARE(clip(right).start, 3.0);
        QCOMPARE(clip(rightSound).start, 3.0);
        QCOMPARE(clip(blockIndex).start, 3.0);
        QCOMPARE(clip(blockIndex).duration, 2.0);
        QVERIFY(tl->isOnCut(clip(blockIndex)));

        // Deleting it puts everything back where it was
        tl->selectClip(blockIndex);
        tl->deleteSelected();
        QCOMPARE(tl->clips().size(), 4);
        QCOMPARE(clip(right).start, 5.0);
        QCOMPARE(clip(rightSound).start, 5.0);
        tl->undo();
        QCOMPARE(clip(right).start, 3.0);
        QVERIFY(tl->clips().last().isTransition());
    }

    void droppingATransitionUsesTheNearestCut()
    {
        tl->setPlayhead(5.0);
        tl->splitAtPlayhead();
        TimelineClip block = makeTransitionClip(VE_TRANSITION_WIPE_LEFT, 2.0);
        tl->dropClips({ block }, { xAt(5.3), trackY(FX1) }); // let go a little off the cut
        const int index = int(tl->clips().size()) - 1;
        QVERIFY(clip(index).isTransition());
        QCOMPARE(clip(index).start, 3.0); // 2s overlap ending where the first clip ends
        QCOMPARE(clip(index).track, FX1);
        QCOMPARE(clip(1).start, 3.0);
        QCOMPARE(tl->duration(), 8.0); // the video got 2s shorter

        // One undo step takes the whole thing back
        tl->undo();
        QCOMPARE(tl->clips().size(), 2);
        QCOMPARE(clip(1).start, 5.0);
        tl->redo();

        // It's locked to its overlap: dragging it does nothing
        drag(tl, { xAt(4.0), trackY(FX1) }, { xAt(8.5), trackY(FX1) });
        QCOMPARE(clip(index).start, 3.0);
        QVERIFY(tl->isOnCut(clip(index)));
    }

    void transitionsWorkWithoutACut()
    {
        auto blockFor = [this](int index) -> const RenderClip* {
            static QList<RenderClip> r;
            r = tl->renderClips();
            for (const RenderClip& c : r)
                if (c.kind == VE_CLIP_TRANSITION && std::abs(c.start - clip(index).start) < 1e-6)
                    return &c;
            return nullptr;
        };

        // Dropped in the middle of a clip: plays there on the spot, nothing gets cut or moved
        tl->dropClips({ makeTransitionClip(VE_TRANSITION_WIPE_LEFT, 1.0) }, { xAt(4.0), trackY(FX1) });
        const int index = int(tl->clips().size()) - 1;
        QVERIFY(clip(index).isTransition());
        QCOMPARE(clip(index).track, FX1);
        QVERIFY(std::abs(clip(index).start - 4.0) < 0.05);
        QCOMPARE(clip(0).start, 0.0);
        QCOMPARE(clip(0).duration, 10.0);
        QCOMPARE(tl->transitionPart(clip(index)), int(VE_PART_THROUGH));
        const RenderClip* r = blockFor(index);
        QVERIFY(r);
        QCOMPARE(r->transition, int(VE_TRANSITION_WIPE_LEFT));
        QCOMPARE(r->part, int(VE_PART_THROUGH));

        // Not stuck like the ones on cuts: drag it to the start of the clip and it brings it in...
        drag(tl, { xAt(clip(index).start + 0.5), trackY(FX1) }, { xAt(0.55), trackY(FX1) });
        QCOMPARE(clip(index).start, 0.0);
        QCOMPARE(tl->transitionPart(clip(index)), int(VE_PART_IN));
        QCOMPARE(blockFor(index)->part, int(VE_PART_IN));
        // ...or to the end and it takes it out
        drag(tl, { xAt(0.5), trackY(FX1) }, { xAt(9.45), trackY(FX1) });
        QCOMPARE(clip(index).end(), 10.0);
        QCOMPARE(tl->transitionPart(clip(index)), int(VE_PART_OUT));

        // Double-click with the playhead away from any cut: lands right there too
        tl->setPlayhead(2.0);
        tl->addAtPlayhead(makeTransitionClip(VE_TRANSITION_ZOOM, 1.0));
        QCOMPARE(tl->clips().last().start, 2.0);
        QCOMPARE(tl->transitionPart(tl->clips().last()), int(VE_PART_THROUGH));
        QCOMPARE(clip(0).duration, 10.0);
    }

    void sameLookingCutsStillShowTheTransition()
    {
        // The bug this fixes: one continuous recording cut in two. Blending the frames right
        // next to the cut changed nothing you could see. Now both sides keep playing over
        // the overlap, so it's a real blend between different moments.
        tl->setPlayhead(5.0);
        tl->splitAtPlayhead();
        tl->setTransition(1, VE_TRANSITION_DISSOLVE);
        QList<RenderClip> r = tl->renderClips();
        QCOMPARE(r.size(), 2);
        QCOMPARE(r[0].start + r[0].duration, 5.0); // the first bit still ends at 5s...
        QCOMPARE(r[1].start, 4.0);                 // ...and the second starts at 4s over it
        QCOMPARE(r[1].in, 5.0);                    // with what came after the cut
    }

    void effectBlocksLayOverEverythingBelow()
    {
        TimelineClip fx;
        fx.kind = TimelineClip::Kind::Effect;
        fx.name = "Vintage";
        fx.look = VE_LOOK_VINTAGE;
        fx.duration = 4.0;
        tl->dropClips({ fx }, { xAt(2.0), trackY(V1) }); // dropped on footage: goes up to an FX track
        const TimelineClip& placed = tl->clips().last();
        QCOMPARE(tl->tracks()[placed.track].kind, TimelineTrack::Kind::Fx);
        QCOMPARE(placed.start, 2.0);

        QList<RenderClip> r = tl->renderClips();
        QCOMPARE(r.last().kind, int(VE_CLIP_ADJUSTMENT));
        QCOMPARE(r.last().look, int(VE_LOOK_VINTAGE));
        QVERIFY(r.last().layer > r.first().layer); // applies on top of the video
    }

    void rippleDeleteCarriesFxAlong()
    {
        // Cut 3-6s out of the video; a title at 8s should slide back to 5s with it
        tl->setPlayhead(8.0);
        tl->addTitle();
        tl->setPlayhead(3.0);
        tl->selectClip(0);
        tl->splitAtPlayhead();
        tl->setPlayhead(6.0);
        tl->splitAtPlayhead();
        int middle = -1;
        for (int i = 0; i < tl->clips().size(); ++i)
            if (tl->clips()[i].start == 3.0)
                middle = i;
        tl->selectClip(middle);
        tl->deleteSelected();
        for (const TimelineClip& c : tl->clips())
            if (c.isTitle())
                QCOMPARE(c.start, 5.0);
    }

    void tracksCanBeAddedRemovedAndUndone()
    {
        QCOMPARE(tl->tracks().size(), 6);
        tl->addTrack(TimelineTrack::Kind::Fx);
        QCOMPARE(tl->tracks().size(), 7);
        QCOMPARE(tl->tracks()[0].name, QStringLiteral("FX 3"));
        QCOMPARE(clip(0).track, V1 + 1); // everything moved down one
        QVERIFY(!tl->removeTrack(V1 + 1)); // not empty
        tl->undo();
        QCOMPARE(tl->tracks().size(), 6);
        QCOMPARE(clip(0).track, V1);
        QVERIFY(tl->removeTrack(FX2)); // empty, fine
        QCOMPARE(tl->tracks().size(), 5);
        QVERIFY(!tl->removeTrack(0)); // the last FX track stays
    }

    void stackingThingsAtTheSameTimeMakesRoom()
    {
        // Three effects at the same moment: FX 1, then FX 2, then a brand new FX 3 on top
        tl->setPlayhead(2.0);
        QList<TimelineClip> fx = effectPresets();
        tl->addAtPlayhead(fx[0]);
        tl->addAtPlayhead(fx[1]);
        tl->addAtPlayhead(fx[2]);
        QCOMPARE(tl->tracks().size(), 7);
        QCOMPARE(tl->tracks()[0].name, QStringLiteral("FX 3"));
        QList<int> tracks;
        for (const TimelineClip& c : tl->clips()) {
            if (!c.isEffect())
                continue;
            QCOMPARE(c.start, 2.0); // all of them right at the playhead
            tracks << c.track;
        }
        std::sort(tracks.begin(), tracks.end());
        QCOMPARE(tracks, (QList<int> { 0, 1, 2 }));

        tl->undo(); // one step takes away the third effect and the track made for it
        QCOMPARE(tl->tracks().size(), 6);
        QCOMPARE(tl->clips().size(), 3);
    }

    void oldProjectsGetUpgraded()
    {
        // Before FX tracks: Video 2, Video 1, Audio 1, Audio 2, and transitions stored on clips
        TimelineClip a = fakeVideo(5), b = fakeVideo(5);
        a.track = b.track = 1; // old "Video 1"
        b.start = 5.0;
        b.transition = VE_TRANSITION_FADE_BLACK;
        b.transitionDuration = 2.0;
        tl->setClips({ a, b });
        QCOMPARE(tl->tracks(), TimelineWidget::defaultTracks());
        QCOMPARE(clip(0).track, V1);
        QCOMPARE(clip(1).transition, 0); // moved off the clip...
        QCOMPARE(tl->clips().size(), 3); // ...into its own block
        QVERIFY(tl->clips().last().isTransition());
        QCOMPARE(tl->clips().last().start, 3.0); // the 2s fade became a 2s overlap
        QCOMPARE(clip(1).start, 3.0);
        QVERIFY(tl->isOnCut(tl->clips().last()));
    }

    void renderLayers()
    {
        tl->appendClips({ fakeVideo(3) });
        drag(tl, { xAt(11.0), trackY(V1) }, { xAt(1.0), trackY(V2) }); // put it on Video 2
        QList<RenderClip> r = tl->renderClips();
        QCOMPARE(r.size(), 2);
        QVERIFY(r[1].layer > r[0].layer); // Video 2 draws over Video 1
    }

    void projectShapesComeOutTheRightSize()
    {
        ProjectSettings s;
        QCOMPARE(s.sizeFor({ 1280, 720 }), QSize(1280, 720)); // "auto" = whatever the first video is
        s.shape = "16:9";
        QCOMPARE(s.sizeFor({ 1280, 720 }), QSize(1920, 1080));
        s.shape = "9:16";
        QCOMPARE(s.sizeFor({}), QSize(1080, 1920));
        s.shape = "1:1";
        s.resolution = 720;
        QCOMPARE(s.sizeFor({}), QSize(720, 720));
        s.shape = "4:5";
        s.resolution = 1080;
        QCOMPARE(s.sizeFor({}), QSize(1080, 1350));
    }

    void cropAndRotateGetSaved()
    {
        TimelineClip c = fakeVideo(5);
        c.track = V1;
        c.cropLeft = 0.1f;
        c.cropBottom = 0.25f;
        c.rotation = -90.0f;
        c.flipH = true;
        c.fill = true;
        ProjectFile::Data out;
        out.clips = { c };
        out.tracks = TimelineWidget::defaultTracks();
        out.settings.shape = "9:16";
        out.settings.fps = 60;
        QTemporaryDir dir;
        QString error;
        QVERIFY(ProjectFile::save(dir.filePath("p.mixmedia"), out, &error));
        ProjectFile::Data in;
        QVERIFY(ProjectFile::load(dir.filePath("p.mixmedia"), &in, nullptr, &error));
        const TimelineClip& back = in.clips.first();
        QCOMPARE(back.cropLeft, 0.1f);
        QCOMPARE(back.cropBottom, 0.25f);
        QCOMPARE(back.rotation, -90.0f);
        QVERIFY(back.flipH && !back.flipV && back.fill);
        QCOMPARE(in.settings, out.settings);

        // ...and they reach the engine
        tl->setClips(in.clips, in.tracks);
        const RenderClip r = tl->renderClips().first();
        QCOMPARE(r.cropLeft, 0.1f);
        QCOMPARE(r.rotation, -90.0f);
        QVERIFY(r.flipH && r.fill);
    }

    void keyframesGlideBetweenValues()
    {
        TimelineClip c = fakeVideo(10);
        c.scale = 0.5f;
        QCOMPARE(c.valueAt(VE_KEY_SIZE, 3.0), 0.5f); // no keyframes: the normal value
        c.setKey(VE_KEY_SIZE, 2.0, 1.0f);
        c.setKey(VE_KEY_SIZE, 6.0, 2.0f);
        QCOMPARE(c.valueAt(VE_KEY_SIZE, 0.0), 1.0f); // before the first: holds it
        QCOMPARE(c.valueAt(VE_KEY_SIZE, 4.0), 1.5f); // halfway
        QCOMPARE(c.valueAt(VE_KEY_SIZE, 9.0), 2.0f); // after the last: holds it
        QVERIFY(c.valueAt(VE_KEY_SIZE, 2.5) < 1.125f); // eases in, so slower than a straight line at first
        c.setKey(VE_KEY_SIZE, 6.005, 3.0f); // close enough to the one at 6s: changes it
        QCOMPARE(c.keys[VE_KEY_SIZE].size(), 2);
        QCOMPARE(c.valueAt(VE_KEY_SIZE, 6.0), 3.0f);
    }

    void keyframesSurviveEdits()
    {
        TimelineClip c = fakeVideo(10);
        c.track = V1;
        c.setKey(VE_KEY_POS_X, 2.0, -0.5f);
        c.setKey(VE_KEY_POS_X, 8.0, 0.5f);
        tl->setClips({ c }, TimelineWidget::defaultTracks());
        const float midway = clip(0).valueAt(VE_KEY_POS_X, 5.0);

        // Split in the middle: the right half carries on exactly where the left one was
        tl->selectClip(0);
        tl->setPlayhead(5.0);
        tl->splitAtPlayhead();
        QCOMPARE(clip(1).valueAt(VE_KEY_POS_X, 0.0), midway);
        QCOMPARE(clip(1).valueAt(VE_KEY_POS_X, 3.0), 0.5f); // the 8s keyframe, now 3s into the right half

        // Twice as fast: keyframes come twice as soon
        tl->setClipSpeed(0, 2.0);
        QCOMPARE(clip(0).keys[VE_KEY_POS_X].first().time, 1.0);

        // Saved, opened, and handed to the engine
        QTemporaryDir dir;
        ProjectFile::Data out;
        out.clips = tl->clips();
        out.tracks = tl->tracks();
        QString error;
        QVERIFY(ProjectFile::save(dir.filePath("k.mixmedia"), out, &error));
        ProjectFile::Data in;
        QVERIFY(ProjectFile::load(dir.filePath("k.mixmedia"), &in, nullptr, &error));
        QCOMPARE(in.clips[1].keys[VE_KEY_POS_X], clip(1).keys[VE_KEY_POS_X]);
        QCOMPARE(tl->renderClips()[1].keys.size(), 2);
    }

    void keyframeButtonsInProperties()
    {
        ClipInspector inspector;
        inspector.resize(340, 2000);
        inspector.show();
        TimelineClip c = fakeVideo(10);
        c.start = 20.0;
        QSignalSpy edited(&inspector, &ClipInspector::edited);
        auto latest = [&] { return edited.last().at(1).value<TimelineClip>(); };
        auto sizeRow = [&] {
            // The ◇ next to Size (the first keyframe button)
            for (QToolButton* b : inspector.findChildren<QToolButton*>())
                if (b->text() == "◇" || b->text() == "◆")
                    return b;
            return static_cast<QToolButton*>(nullptr);
        };

        inspector.setPlayhead(22.0);
        inspector.showClip(0, c);
        sizeRow()->click(); // ◆ at 2s into the clip
        QCOMPARE(edited.size(), 1);
        c = latest();
        QCOMPARE(c.keys[VE_KEY_SIZE].size(), 1);
        QCOMPARE(c.keys[VE_KEY_SIZE].first().time, 2.0);

        // Somewhere else, drag the size slider: that makes a second keyframe there
        inspector.setPlayhead(26.0);
        inspector.showClip(0, c);
        auto* sizeSlider = inspector.findChildren<NumberSlider*>().at(0);
        for (NumberSlider* s : inspector.findChildren<NumberSlider*>())
            if (s->parentWidget() == sizeRow()->parentWidget())
                sizeSlider = s;
        sizeSlider->setValue(250);
        emit sizeSlider->valueChanged(250);
        c = latest();
        QCOMPARE(c.keys[VE_KEY_SIZE].size(), 2);
        QCOMPARE(c.valueAt(VE_KEY_SIZE, 6.0), 2.5f);
        QCOMPARE(c.scale, 1.0f); // the normal value stays put

        // Zoom into a spot at the playhead: in, stay, out
        inspector.setSpot(QPointF(-0.25, 0.1));
        inspector.showClip(0, c);
        for (QPushButton* b : inspector.findChildren<QPushButton*>())
            if (b->text() == "Add")
                b->click();
        c = latest();
        QCOMPARE(c.keys[VE_KEY_SIZE].size(), 4);
        QCOMPARE(c.keys[VE_KEY_SIZE].first().time, 6.0);
        QVERIFY(c.keys[VE_KEY_POS_X][1].value > 0); // spot on the left: the picture moves right
        QVERIFY(c.keys[VE_KEY_POS_Y][1].value < 0);
    }

    void pickingASpotOnThePreview()
    {
        PreviewWidget preview;
        preview.resize(400, 300); // a 16:9 picture: 400 x 225, with bars above and below
        preview.show();
        QSignalSpy picked(&preview, &PreviewWidget::picked);
        QTest::mouseClick(&preview, Qt::LeftButton, {}, QPoint(100, 150));
        QCOMPARE(picked.size(), 0); // not picking yet: clicks do nothing
        preview.pickSpot();
        QTest::mouseClick(&preview, Qt::LeftButton, {}, QPoint(200, 10));
        QCOMPARE(picked.size(), 0); // in the bars, off the picture: still waiting
        QTest::mouseClick(&preview, Qt::LeftButton, {}, QPoint(100, 150));
        QCOMPARE(picked.size(), 1);
        QPointF spot = picked.first().first().toPointF();
        QVERIFY(std::abs(spot.x() - (-0.25)) < 0.01); // a quarter of the way in from the left
        QVERIFY(std::abs(spot.y()) < 0.01);           // halfway down
    }

    void ctrlClickAndMoveTogether()
    {
        tl->appendClips({ fakeVideo(3) }); // 0-10s and 10-13s on Video 1
        tl->selectClip(-1); // (adding a clip selects it)
        QTest::mouseClick(tl, Qt::LeftButton, Qt::ControlModifier, { xAt(5.0), trackY(V1) });
        QTest::mouseClick(tl, Qt::LeftButton, Qt::ControlModifier, { xAt(11.5), trackY(V1) });
        QCOMPARE(tl->selectedIndexes().size(), 2);

        drag(tl, { xAt(5.0), trackY(V1) }, { xAt(7.0), trackY(V1) }); // grab one, both go
        QCOMPARE(clip(0).start, 2.0);
        QCOMPARE(clip(1).start, 12.0);
        QCOMPARE(tl->selectedIndexes().size(), 2); // still both selected

        QTest::mouseClick(tl, Qt::LeftButton, Qt::ControlModifier, { xAt(13.5), trackY(V1) }); // Ctrl+click again: out
        QCOMPARE(tl->selectedIndexes(), QList<int> { 0 });
        tl->undo();
        QCOMPARE(clip(0).start, 0.0); // one undo step for the whole move
        QCOMPARE(clip(1).start, 10.0);
    }

    void boxSelectAndDeleteSeveral()
    {
        tl->appendClips({ fakeVideo(3) });
        tl->setPlayhead(2.0);
        tl->addTitle(); // on an FX track, 2-7s
        // A box from empty space on Audio 2 up to Video 1, across both video clips (not up to the title)
        drag(tl, { xAt(1.0), trackY(A2) }, { xAt(11.0), trackY(V1) });
        QList<int> picked = tl->selectedIndexes();
        std::sort(picked.begin(), picked.end());
        QCOMPARE(picked, (QList<int> { 0, 1 }));

        tl->deleteSelected();
        QCOMPARE(tl->clips().size(), 1); // just the title left
        QVERIFY(tl->clips().first().isTitle());
        tl->undo();
        QCOMPARE(tl->clips().size(), 3); // and back in one step
    }

    void copyAndPaste()
    {
        tl->selectClip(0);
        tl->copySelected();
        tl->setPlayhead(15.0);
        tl->paste();
        QCOMPARE(tl->clips().size(), 2);
        QCOMPARE(clip(1).start, 15.0);
        QCOMPARE(clip(1).track, V1);
        QCOMPARE(tl->selectedIndexes(), QList<int> { 1 });

        // Pasting where something already is: it slides along to the next free spot
        tl->setPlayhead(5.0);
        tl->paste();
        QCOMPARE(tl->clips().size(), 3);
        QCOMPARE(clip(2).start, 25.0); // after the one pasted at 15s
        tl->undo();
        QCOMPARE(tl->clips().size(), 2);
    }

    void transitionsStickToTheirClip()
    {
        tl->setPlayhead(5.0);
        tl->splitAtPlayhead();
        tl->setTransition(1, VE_TRANSITION_DISSOLVE); // the second half slides back to 4s
        const int block = int(tl->clips().size()) - 1;
        QCOMPARE(clip(block).start, 4.0);

        // Drag the clip after the cut away: the transition goes with it (now bringing it in from black)
        drag(tl, { xAt(7.0), trackY(V1) }, { xAt(10.0), trackY(V1) });
        QCOMPARE(clip(1).start, 7.0);
        QCOMPARE(clip(block).start, 7.0);
        QCOMPARE(tl->transitionPart(clip(block)), int(VE_PART_IN));

        // Deleting a clip takes its transitions with it
        tl->undo();
        tl->selectClip(0);
        tl->deleteSelected();
        QVERIFY(std::none_of(tl->clips().begin(), tl->clips().end(), [](const TimelineClip& c) { return c.isTransition(); }));
    }

    void freezeFrameHoldsAMoment()
    {
        tl->setPlayhead(4.0);
        tl->freezeFrame(2.0);
        QCOMPARE(tl->clips().size(), 3);
        const TimelineClip& still = tl->clips().last();
        QVERIFY(still.freeze);
        QCOMPARE(still.start, 4.0);
        QCOMPARE(still.duration, 2.0);
        QCOMPARE(still.in, 4.0); // the frame that was under the playhead
        QVERIFY(!still.playsAudio());
        QCOMPARE(clip(0).duration, 4.0); // before it
        QCOMPARE(clip(1).start, 6.0);    // the rest, pushed along
        QCOMPARE(clip(1).in, 4.0);
        QCOMPARE(tl->duration(), 12.0);
        QVERIFY(!tl->renderClips().last().audio);
        tl->undo();
        QCOMPARE(tl->clips().size(), 1); // one step
    }

    void backwardsClipsSplitTheRightWay()
    {
        tl->setReverse(0, true);
        QVERIFY(clip(0).reverse);
        tl->selectClip(0);
        tl->setPlayhead(4.0);
        tl->splitAtPlayhead();
        // Played backwards, the first 4s of the timeline show the last 4s of the file
        QCOMPARE(clip(0).in, 6.0);
        QCOMPARE(clip(0).duration, 4.0);
        QCOMPARE(clip(1).in, 0.0);
        QCOMPARE(clip(1).duration, 6.0);
        QVERIFY(clip(1).reverse);
    }

    void greenScreenAndFriendsGetSaved()
    {
        TimelineClip c = fakeVideo(5);
        c.track = V1;
        c.reverse = true;
        c.chromaKey = true;
        c.keyColor = QColor(10, 20, 250);
        c.keyStrength = 0.33f;
        TimelineClip f = fakeVideo(5);
        f.track = V1;
        f.start = 5.0;
        f.freeze = true;
        f.in = 2.5;
        ProjectFile::Data out;
        out.clips = { c, f };
        out.tracks = TimelineWidget::defaultTracks();
        QTemporaryDir dir;
        QString error;
        QVERIFY(ProjectFile::save(dir.filePath("g.mixmedia"), out, &error));
        ProjectFile::Data in;
        QVERIFY(ProjectFile::load(dir.filePath("g.mixmedia"), &in, nullptr, &error));
        QVERIFY(in.clips[0].reverse && in.clips[0].chromaKey);
        QCOMPARE(in.clips[0].keyColor, QColor(10, 20, 250));
        QCOMPARE(in.clips[0].keyStrength, 0.33f);
        QVERIFY(in.clips[1].freeze);
        QCOMPARE(in.clips[1].in, 2.5);
        tl->setClips(in.clips, in.tracks);
        QList<RenderClip> r = tl->renderClips();
        QVERIFY(r[0].reverse && r[0].chromaKey);
        QCOMPARE(r[0].keyColor, 0x0a14faU);
        QVERIFY(r[1].freeze && !r[1].audio);
    }

    void pickingAColourOnThePreview()
    {
        PreviewWidget preview;
        preview.resize(400, 225);
        preview.show();
        QImage frame(160, 90, QImage::Format_RGB32);
        frame.fill(QColor(0, 177, 64));
        preview.setFrame(frame);
        QSignalSpy picked(&preview, &PreviewWidget::colorPicked);
        preview.pickColor();
        QTest::mouseClick(&preview, Qt::LeftButton, {}, QPoint(200, 110));
        QCOMPARE(picked.size(), 1);
        QCOMPARE(picked.first().first().value<QColor>(), QColor(0, 177, 64));
    }

    void fancyTitlesGetSavedAndDrawn()
    {
        TimelineClip t = titlePresets().first();
        t.track = FX1;
        t.title.font = "Monospace";
        t.title.italic = true;
        t.title.align = 2;
        t.title.x = 0.9;
        t.title.outline = 7;
        t.title.outlineColor = QColor(1, 2, 3);
        t.title.shadowColor = QColor(200, 0, 0, 100);
        t.title.boxColor = QColor(9, 8, 7, 50);
        t.title.spacing = 25;
        ProjectFile::Data out;
        out.clips = { t };
        out.tracks = TimelineWidget::defaultTracks();
        QTemporaryDir dir;
        QString error;
        QVERIFY(ProjectFile::save(dir.filePath("t.mixmedia"), out, &error));
        ProjectFile::Data in;
        QVERIFY(ProjectFile::load(dir.filePath("t.mixmedia"), &in, nullptr, &error));
        QCOMPARE(in.clips.first().title, t.title);

        // Older projects had no "shadow" setting: titles without a box had one anyway
        QFile file(dir.filePath("t.mixmedia"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QByteArray json = file.readAll().replace("\"shadow\": true", "\"oldThing\": true");
        file.close();
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(json);
        file.close();
        QVERIFY(ProjectFile::load(dir.filePath("t.mixmedia"), &in, nullptr, &error));
        QVERIFY(in.clips.first().title.shadow); // (the preset has no box)

        // Where the ink lands: lined up left at 10%, or right at 90%
        auto ink = [](const TitleStyle& style) {
            QImage img = TitleRenderer::render(style, { 400, 200 });
            int left = 400, right = -1, count = 0;
            for (int y = 0; y < 200; ++y)
                for (int x = 0; x < 400; ++x)
                    if (qAlpha(img.pixel(x, y)) > 128) {
                        left = std::min(left, x);
                        right = std::max(right, x);
                        ++count;
                    }
            return std::tuple { left, right, count };
        };
        TitleStyle plain;
        plain.text = "Hi there";
        plain.box = false;
        plain.align = 0;
        plain.x = 0.1;
        auto [l, r1, n1] = ink(plain);
        QVERIFY(std::abs(l - 40) <= 3);
        plain.align = 2;
        plain.x = 0.9;
        auto [l2, r, n2] = ink(plain);
        QVERIFY(std::abs(r - 360) <= 3);
        plain.outline = 10;
        auto [l3, r3, outlined] = ink(plain);
        QVERIFY(outlined > n2 * 1.3); // an outline adds plenty of ink
        Q_UNUSED(r1);
        Q_UNUSED(n1);
        Q_UNUSED(l2);
        Q_UNUSED(l3);
        Q_UNUSED(r3);
    }

    void soundToolsGetSaved()
    {
        TimelineClip c = fakeVideo(5);
        c.track = V1;
        QVERIFY(c.keepPitch); // on unless you turn it off
        c.keepPitch = false;
        c.denoise = 0.55f;
        c.duck = true;
        c.duckAmount = 0.4f;
        ProjectFile::Data out;
        out.clips = { c };
        out.tracks = TimelineWidget::defaultTracks();
        QTemporaryDir dir;
        QString error;
        QVERIFY(ProjectFile::save(dir.filePath("s.mixmedia"), out, &error));
        ProjectFile::Data in;
        QVERIFY(ProjectFile::load(dir.filePath("s.mixmedia"), &in, nullptr, &error));
        const TimelineClip& back = in.clips.first();
        QVERIFY(!back.keepPitch && back.duck);
        QCOMPARE(back.denoise, 0.55f);
        QCOMPARE(back.duckAmount, 0.4f);
        tl->setClips(in.clips, in.tracks);
        const RenderClip r = tl->renderClips().first();
        QVERIFY(!r.keepPitch && r.duck);
        QCOMPARE(r.denoise, 0.55f);
    }

    void readingSubtitleFiles()
    {
        // A messy .srt: byte-order mark, Windows line endings, styling tags, two-line subtitles
        QString srt = QString(QChar(0xFEFF)) + "1\r\n00:00:01,500 --> 00:00:03,000\r\n<i>Hello</i> there\r\n\r\n"
                      "2\r\n00:00:04,000 --> 00:00:06,250\r\n{\\an8}Two\r\nlines\r\n\r\n";
        QList<SubtitleFile::Line> lines = SubtitleFile::parse(srt);
        QCOMPARE(lines.size(), 2);
        QCOMPARE(lines[0].start, 1.5);
        QCOMPARE(lines[0].end, 3.0);
        QCOMPARE(lines[0].text, QString("Hello there"));
        QCOMPARE(lines[1].text, QString("Two\nlines"));
        QCOMPARE(lines[1].end, 6.25);
        // ...and back out again
        QCOMPARE(SubtitleFile::parse(SubtitleFile::toSrt(lines)).size(), 2);
        QVERIFY(SubtitleFile::toSrt(lines).contains("00:00:06,250"));

        // WebVTT: header, short times, cue names, notes
        QString vtt = "WEBVTT\n\nNOTE made by hand\n\nintro\n00:02.000 --> 00:03.500\nHi\n\n1:00:00.000 --> 1:00:01.000\nLate\n";
        lines = SubtitleFile::parse(vtt);
        QCOMPARE(lines.size(), 2);
        QCOMPARE(lines[0].start, 2.0);
        QCOMPARE(lines[1].start, 3600.0);

        QCOMPARE(SubtitleFile::parseTime("1:02.5"), 62.5);
        QCOMPARE(SubtitleFile::parseTime("nope"), -1.0);
    }

    void subtitlesOnTheirOwnTrack()
    {
        auto line = [](double start, double length, const QString& text) {
            TimelineClip c;
            c.kind = TimelineClip::Kind::Subtitle;
            c.title.text = text;
            c.start = start;
            c.duration = length;
            return c;
        };
        tl->addSubtitles({ line(1, 2, "Hello there friend"), line(4, 1, "Bye") }, false);
        QCOMPARE(tl->tracks().first().kind, TimelineTrack::Kind::Subtitles); // a new track, right on top
        QCOMPARE(tl->subtitleLines().size(), 2);
        const int first = tl->subtitleLines().first();
        QCOMPARE(clip(first).track, 0);

        // Each line is a picture over everything else
        auto pictures = [this] {
            QList<RenderClip> out;
            for (const RenderClip& r : tl->renderClips([](const TitleStyle& s, int lit) { return s.text + "#" + QString::number(lit); }))
                if (r.path.contains('#'))
                    out << r;
            return out;
        };
        QList<RenderClip> r = pictures();
        QCOMPARE(r.size(), 2);
        QCOMPARE(r[0].path, QString("Hello there friend#-1"));
        QVERIFY(r[0].layer > tl->renderClips().first().layer); // on top of the video

        // Word by word, two at a time: a picture per word, the one being said lit up
        TimelineClip shown = clip(first);
        shown.title = tl->subtitleLook(shown);
        shown.title.wordByWord = true;
        shown.title.wordsAtOnce = 2;
        tl->updateClip(first, shown, "look");
        QVERIFY(tl->tracks().first().style.wordByWord); // a shared look: the whole track changed
        r = pictures();
        QCOMPARE(r.size(), 4); // "Hello there" x2 + "friend" + "Bye"
        QCOMPARE(r[0].path, QString("Hello there#0"));
        QCOMPARE(r[1].path, QString("Hello there#1"));
        QCOMPARE(r[2].path, QString("friend#0"));
        QCOMPARE(r[0].start, 1.0);
        QCOMPARE(r[2].start + r[2].duration, 3.0); // the last word lasts to the end of the line

        // Saved and opened again, look and all
        QTemporaryDir dir;
        ProjectFile::Data out;
        out.clips = tl->clips();
        out.tracks = tl->tracks();
        QString error;
        QVERIFY(ProjectFile::save(dir.filePath("subs.mixmedia"), out, &error));
        ProjectFile::Data in;
        QVERIFY(ProjectFile::load(dir.filePath("subs.mixmedia"), &in, nullptr, &error));
        QCOMPARE(in.tracks, out.tracks);
        QCOMPARE(in.clips[first].title.text, QString("Hello there friend"));

        // Adding a line at the playhead lands on the subtitle track
        tl->setPlayhead(8.0);
        TimelineClip extra = line(0, 2, "New");
        tl->addAtPlayhead(extra);
        QCOMPARE(tl->subtitleLines().size(), 3);
        QCOMPARE(tl->clips().last().start, 8.0);
        QCOMPARE(tl->clips().last().track, 0);
    }

    void subtitlePanelEditsTheTimeline()
    {
        TimelineClip c;
        c.kind = TimelineClip::Kind::Subtitle;
        c.title.text = "First";
        c.start = 1;
        c.duration = 2;
        tl->addSubtitles({ c }, false);
        SubtitlePanel panel(tl);
        auto* table = panel.findChild<QTableWidget*>();
        QCOMPARE(table->rowCount(), 1);
        QCOMPARE(table->item(0, 0)->text(), QString("0:01.000"));

        table->item(0, 2)->setText("Changed words"); // typing into the table...
        const int line = tl->subtitleLines().first();
        QCOMPARE(clip(line).title.text, QString("Changed words")); // ...changes the timeline
        table->item(0, 1)->setText("0:05");
        QCOMPARE(clip(line).end(), 5.0);
        table->item(0, 0)->setText("rubbish");
        QCOMPARE(clip(line).start, 1.0); // couldn't read it: nothing changes
    }

    void captionWordsBecomeLines()
    {
        using W = AutoCaptions::Word;
        QList<W> words = {
            { 2.0, 2.3, "Hello", true }, { 2.3, 2.6, "and", false }, { 2.6, 3.0, "welcome.", false },
            // a new sentence
            { 3.2, 3.5, "Today", false }, { 3.5, 3.6, "we're", false }, { 3.6, 4.0, "styling", false },
            // a long pause
            { 6.0, 6.4, "tables", false },
        };
        QList<TimelineClip> lines = AutoCaptions::linesFromWords(words);
        QCOMPARE(lines.size(), 3);
        QCOMPARE(lines[0].title.text, QString("Hello and welcome."));
        QCOMPARE(lines[0].start, 2.0);
        QVERIFY(lines[0].end() <= lines[1].start + 1e-9); // lingers, but not into the next line
        QCOMPARE(lines[1].title.text, QString("Today we're styling"));
        QCOMPARE(lines[2].title.text, QString("tables"));
        QCOMPARE(lines[1].words.size(), 3);
        QCOMPARE(lines[1].words[1].start, 0.3); // word timings kept, from the start of the line
        QVERIFY(lines[0].isSubtitle());

        // Too long to read in one go: split
        QList<W> chatty;
        for (int i = 0; i < 20; ++i)
            chatty << W { i * 0.25, i * 0.25 + 0.2, "word" + QString::number(i), false };
        for (const TimelineClip& line : AutoCaptions::linesFromWords(chatty))
            QVERIFY(line.title.text.size() <= 42);
    }

    void helpPagesAreAllThere()
    {
        // The pages the "?" buttons open
        for (const char* page : { "README.md", "getting-started.md", "timeline.md", "clips.md", "keyframes.md", "titles.md",
                                  "effects-and-transitions.md", "subtitles.md", "sound.md", "export.md", "settings.md",
                                  "shortcuts.md", "troubleshooting.md" })
            QVERIFY2(QFile::exists(QString(":/docs/") + page), page);

        // Every link from one page to another goes somewhere
        static const QRegularExpression link("\\]\\(([^)#]+)(#[^)]*)?\\)");
        const QStringList pages = QDir(":/docs").entryList({ "*.md" });
        QVERIFY(pages.size() >= 13);
        for (const QString& page : pages) {
            QFile file(":/docs/" + page);
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QString text = QString::fromUtf8(file.readAll());
            QVERIFY2(text.startsWith("# "), qPrintable(page + " needs a heading"));
            for (const QRegularExpressionMatch& m : link.globalMatch(text)) {
                QString target = m.captured(1);
                if (target.startsWith("http"))
                    continue;
                QVERIFY2(pages.contains(target), qPrintable(page + " links to a missing page: " + target));
            }
        }
    }

    void updateCheckPicksTheRightRelease()
    {
        using namespace UpdateChecker;
        const QDateTime now = QDateTime::fromString("2026-10-05T12:00:00Z", Qt::ISODate);
        auto release = [](const char* tag, bool pre, bool draft, const char* published, const char* body) {
            return QJsonObject { { "tag_name", tag }, { "prerelease", pre }, { "draft", draft }, { "published_at", published },
                                 { "html_url", QString("https://example.com/") + tag }, { "body", body } };
        };
        const QByteArray json = QJsonDocument(QJsonArray {
            release("v0.6.0-beta", true, false, "2026-10-04T10:00:00Z", "test"),
            release("v0.5.2", false, false, "2026-10-05T11:30:00Z", "brand new"),
            release("v0.5.1", false, false, "2026-10-03T09:00:00Z", "## Hi"),
            release("v0.7.0", false, true, "", ""),
            release("v0.4.2", false, false, "2026-09-01T09:00:00Z", ""),
        }).toJson();
        const QList<Release> releases = parseReleases(json);
        QCOMPARE(releases.size(), 5);
        QCOMPARE(releases[0].version, QVersionNumber(0, 6, 0));
        QVERIFY(releases[0].preRelease);
        const QVersionNumber have(0, 5, 0);

        // Releases only: 0.5.2 came out 30 minutes ago, too fresh, so 0.5.1 it is
        auto r = pickUpdate(releases, have, Channel::Releases, now, {}, false);
        QVERIFY(r);
        QCOMPARE(r->tag, QString("v0.5.1"));
        // An hour later, 0.5.2's had time to get its downloads up
        r = pickUpdate(releases, have, Channel::Releases, now.addSecs(3600), {}, false);
        QCOMPARE(r->tag, QString("v0.5.2"));
        // Pre-releases too: the beta's newest (drafts never count)
        r = pickUpdate(releases, have, Channel::PreReleases, now, {}, false);
        QCOMPARE(r->tag, QString("v0.6.0-beta"));
        // Skipped 0.5.1: nothing to say right now...
        QVERIFY(!pickUpdate(releases, have, Channel::Releases, now, "v0.5.1", false));
        // ...but asking by hand ignores the skip and the hour
        r = pickUpdate(releases, have, Channel::Releases, now, "v0.5.1", true);
        QCOMPARE(r->tag, QString("v0.5.2"));
        // Turned off: never on its own
        QVERIFY(!pickUpdate(releases, have, Channel::Off, now.addDays(10), {}, false));
        // Already up to date
        QVERIFY(!pickUpdate(releases, QVersionNumber(0, 5, 2), Channel::Releases, now.addDays(1), {}, false));
    }

    void droppingUpTopStacksVideos()
    {
        // Dropped on the FX tracks = "on top": the top video track's free there, so it goes in that
        tl->dropClips({ fakeVideo(3) }, { xAt(2.0), trackY(FX1) });
        QCOMPARE(tl->tracks().size(), 6);
        QCOMPARE(clip(1).track, V2);
        QCOMPARE(clip(1).start, 2.0);

        // Again: Video 2's busy now, so a new video track appears on top for it
        tl->dropClips({ fakeVideo(3) }, { xAt(2.0), trackY(FX1) });
        QCOMPARE(tl->tracks().size(), 7);
        QCOMPARE(tl->tracks()[2].kind, TimelineTrack::Kind::Video);
        QCOMPARE(tl->tracks()[2].name, QString("Video 3"));
        QCOMPARE(clip(2).track, 2);
        QCOMPARE(clip(2).start, 2.0);
        QCOMPARE(clip(1).track, 3); // (the one before moved down a row with its track)
        // The newest one shows on top of the others
        QList<RenderClip> r = tl->renderClips();
        QVERIFY(r[2].layer > r[1].layer && r[1].layer > r[0].layer);

        tl->undo(); // one step: the clip and its new track
        QCOMPARE(tl->tracks().size(), 6);
        QCOMPARE(tl->clips().size(), 2);
    }

    void crashRecoveryBringsWorkBack()
    {
        // Pretend an earlier MixMedia crashed: an auto-save with nobody holding its lock
        QStandardPaths::setTestModeEnabled(true); // (keeps this away from your real files)
        QDir folder(QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("autosave"));
        QVERIFY(folder.mkpath("."));
        ProjectFile::Data data;
        TimelineClip c = fakeVideo(7);
        c.track = 3; // Video 1
        data.clips = { c };
        data.tracks = TimelineWidget::defaultTracks();
        QString error;
        QVERIFY(ProjectFile::save(folder.filePath("crashed.mixmedia"), data, &error));
        QFile where(folder.filePath("crashed.txt"));
        QVERIFY(where.open(QIODevice::WriteOnly));
        where.write(QDir::temp().filePath("Holiday.mixmedia").toUtf8());
        where.close();

        MainWindow window;
        // Say yes to "Get your work back?" when it pops up (and OK to "some files are missing",
        // since the fake clip's file doesn't exist)
        QStringList asked;
        QTimer clicker;
        clicker.setInterval(20);
        QObject::connect(&clicker, &QTimer::timeout, [&] {
            for (QWidget* w : QApplication::topLevelWidgets()) {
                auto* box = qobject_cast<QMessageBox*>(w);
                if (!box || !box->isVisible())
                    continue;
                asked << box->windowTitle();
                QAbstractButton* yes = box->button(QMessageBox::Open);
                (yes ? yes : box->button(QMessageBox::Ok))->click();
            }
        });
        clicker.start();
        window.offerRecovery();
        clicker.stop();
        QVERIFY(asked.contains("Get your work back?"));
        QCOMPARE(window.windowTitle(), QString("Holiday* — MixMedia Video Editor")); // back, and still unsaved
        QVERIFY(!QFile::exists(folder.filePath("crashed.mixmedia"))); // and only offered the once
        QVERIFY(!QFile::exists(folder.filePath("crashed.txt")));
    }
};

QTEST_MAIN(TimelineTest)
#include "timeline_test.moc"
