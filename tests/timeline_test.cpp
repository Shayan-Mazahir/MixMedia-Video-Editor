// Pretends to be a mouse and keyboard and makes sure the timeline edits do what they should.
// Run with: QT_QPA_PLATFORM=offscreen ./build/tests/timeline_test

#include "TimelineWidget.h"

#include <QApplication>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTest>

namespace {

// Layout numbers from TimelineWidget
constexpr int HeaderWidth = 90;
constexpr int RulerHeight = 28;
constexpr int TrackHeight = 64;
constexpr int WidgetWidth = 1000;

int trackY(int track) { return RulerHeight + track * TrackHeight + TrackHeight / 2; }

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
        QCOMPARE(clip(0).track, 1);
        QCOMPARE(clip(0).start, 0.0);
    }

    void audioOnlyGoesOnAudioTrack()
    {
        TimelineClip song = fakeVideo(5);
        song.hasVideo = false;
        tl->appendClips({ song });
        QCOMPARE(clip(1).track, 2);
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
        drag(tl, { xAt(4.0) - 2, trackY(1) }, { xAt(2.0), trackY(1) });
        QVERIFY(qAbs(clip(0).duration - 2.0) < 0.02);
        QCOMPARE(clip(0).in, 0.0);

        // Now try dragging it way past the next clip: it should stop at 4s
        drag(tl, { xAt(clip(0).end()) - 2, trackY(1) }, { xAt(8.0), trackY(1) });
        QVERIFY(qAbs(clip(0).end() - 4.0) < 1e-9);
    }

    void trimLeftEdge()
    {
        tl->setPlayhead(4.0);
        tl->splitAtPlayhead();
        // Pull the start of the second clip from 4s to 6s: it should skip ahead in the file too
        drag(tl, { xAt(4.0) + 2, trackY(1) }, { xAt(6.0), trackY(1) });
        QVERIFY(qAbs(clip(1).start - 6.0) < 0.02);
        QVERIFY(qAbs(clip(1).in - 6.0) < 0.02);
        QVERIFY(qAbs(clip(1).end() - 10.0) < 1e-9); // the end didn't budge
    }

    void cantTrimPastStartOfFile()
    {
        // The whole file is already on the timeline, so pulling the left edge further left does nothing
        tl->setPlayhead(9.0); // keep the snap target out of the way
        drag(tl, { xAt(0.0) + 2, trackY(1) }, { xAt(0.0) - 50, trackY(1) });
        QCOMPARE(clip(0).start, 0.0);
        QCOMPARE(clip(0).in, 0.0);
        QCOMPARE(clip(0).duration, 10.0);
    }

    void moveToOtherTrackAndBack()
    {
        // Grab the middle and drop it on Video 2, a little later
        drag(tl, { xAt(5.0), trackY(1) }, { xAt(7.0), trackY(0) });
        QCOMPARE(clip(0).track, 0);
        QVERIFY(qAbs(clip(0).start - 2.0) < 0.02);
        tl->undo();
        QCOMPARE(clip(0).track, 1);
        QCOMPARE(clip(0).start, 0.0);
    }

    void movedClipsDontOverlap()
    {
        tl->setPlayhead(4.0);
        tl->splitAtPlayhead();
        // Drag the second clip (4-10s) back on top of the first one
        drag(tl, { xAt(7.0), trackY(1) }, { xAt(5.0), trackY(1) });
        QVERIFY(clip(1).start >= clip(0).end() - 1e-9);
    }

    void snapsToPlayhead()
    {
        tl->appendClips({ fakeVideo(2) }); // second clip at 10-12s, made a bit smaller
        tl->setPlayhead(5.0);
        // Drop it on Video 2 with its start a few pixels off the playhead, it should snap right onto it
        int grabX = xAt(11.0);
        int targetStartX = xAt(5.0) + 4;
        drag(tl, { grabX, trackY(1) }, { targetStartX + (grabX - xAt(10.0)), trackY(0) });
        QCOMPARE(clip(1).start, 5.0);
    }

    void clickSelectsAndDeleteRemoves()
    {
        QSignalSpy changed(tl, &TimelineWidget::clipsChanged);
        send(tl, QEvent::MouseButtonPress, { xAt(5.0), trackY(1) }, Qt::LeftButton);
        send(tl, QEvent::MouseButtonRelease, { xAt(5.0), trackY(1) }, Qt::NoButton);
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

    void renderLayers()
    {
        tl->appendClips({ fakeVideo(3) });
        drag(tl, { xAt(11.0), trackY(1) }, { xAt(1.0), trackY(0) }); // put it on Video 2
        QList<RenderClip> r = tl->renderClips();
        QCOMPARE(r.size(), 2);
        QVERIFY(r[1].layer > r[0].layer); // Video 2 draws over Video 1
    }
};

QTEST_MAIN(TimelineTest)
#include "timeline_test.moc"
