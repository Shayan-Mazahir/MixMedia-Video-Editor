# The timeline

[← Help home](README.md)

The timeline is the strip at the bottom where your video gets put together, left to right. The red line is the **playhead**: whatever it's on is what the preview shows.

## Tracks

From top to bottom:

- **Subtitles**: appears when you add your first subtitle line. See [Subtitles](subtitles.md).
- **FX tracks**: titles, effects and transitions. An effect here changes everything below it.
- **Video tracks**: your footage and pictures. Higher tracks show on top of lower ones (that's how picture-in-picture works).
- **Audio tracks**: sound on its own, like music or detached audio.

Click **+ Track** (top-left corner of the timeline) to add any kind of track, or right-click a track's name to add one or remove an empty one.

## Stacking videos on top of each other

Each video track shows on top of the ones below it, so stacking 5 videos means 5 video tracks. The easy way: **drag a video up onto the FX tracks**. It goes on the top video track, and if that's busy there, MixMedia makes a new video track on top for it. Then shrink and place each one with **Quick spot** in Properties (corners, halves, a 2×2 grid), see [Changing a clip](clips.md).

## Moving and trimming

- **Move**: drag the middle of a clip. It snaps to the playhead and other clips' edges.
- **Trim**: drag the left or right edge.
- **Split**: put the playhead where you want the cut and press **S** (or right-click → *Split here*).
- **Delete**: select it and press **Delete**. Everything after it slides back to close the gap ("ripple delete"). **Shift+Delete** leaves the gap.

## Cutting out the quiet bits

Talking to the camera and pausing a lot? **Edit → Cut out the quiet bits…** finds the pauses and cuts them out for you (the "jump cut" style).

1. It listens to the timeline (a few seconds, even for an hour of video).
2. Then you choose:
   - **Pauses longer than**: shorter ones are left alone, they're just the gaps between words. 0.6 seconds is a good start.
   - **Leave either side**: keeps a little of each pause, so it doesn't sound chopped. Turn it up if words get clipped.
   - **What counts as quiet**: MixMedia works out a level for your recording. Slide left if it's cutting too much, right if it's missing pauses (like when there's background noise).
3. It tells you how many pauses it found and how long the video will be, and changes as you adjust.
4. Click **Cut them out**.

Every track gets cut at the same spots, so picture, sound and titles stay in sync. **Ctrl+Z** puts it all back in one go. Tip: do this *before* auto-captions, so the subtitles line up with the final cut.

## Selecting lots of clips

- **Ctrl+click** clips to add them to the selection (Ctrl+click again to take one out).
- **Drag a box** across empty space to select everything it touches.
- **Ctrl+A** selects everything.

Then drag any of them to move them all together, or delete them all at once.

## Copy and paste

**Ctrl+C** copies the selected clips, **Ctrl+X** cuts them, and **Ctrl+V** pastes them at the playhead, keeping how they sit next to each other. If something's already there, they slide along to the next free spot. You can also right-click empty space → *Paste here*.

## Detaching sound

Select a video and press **Ctrl+D** (or *Detach audio*) to put its sound on its own track, so you can move, trim or fade it separately. The video and its sound still move together when you drag one of them.

## Zooming

**Ctrl + mouse wheel** zooms around the mouse, **Ctrl+=** / **Ctrl+-** zoom in and out, and **Ctrl+0** fits the whole thing. The mouse wheel on its own scrolls sideways.

## Undo

**Ctrl+Z** undoes, **Ctrl+Shift+Z** (or **Ctrl+Y**) redoes. Pretty much everything can be undone.
