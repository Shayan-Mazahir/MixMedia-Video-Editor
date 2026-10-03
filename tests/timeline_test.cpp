// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

// Pretends to be a mouse and keyboard and makes sure the timeline edits do what they should.
// Run with: QT_QPA_PLATFORM=offscreen ./build/tests/timeline_test

#include "ClipPresets.h"
#include "NumberSlider.h"
#include "ProjectFile.h"
#include "TimelineWidget.h"

#include <QApplication>
#include <QDoubleSpinBox>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QSlider>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

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
        QList<RenderClip> r = tl->renderClips([](const TimelineClip&) { return QStringLiteral("/tmp/title.png"); });
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
};

QTEST_MAIN(TimelineTest)
#include "timeline_test.moc"
