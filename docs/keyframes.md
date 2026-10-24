# Keyframes and zoom & pan

[← Help home](README.md)

## Keyframes

A keyframe says "at this moment, this setting should be this". Add two or more, and MixMedia glides the setting between them. You can keyframe a clip's **size**, **position** (left/right, up/down), **opacity**, **shake**, **rotation** and **volume**.

1. Select the clip and move the playhead to where the movement should start.
2. Click the **◇** next to the setting (in Properties). It turns into **◆**: that's a keyframe.
3. Move the playhead to where the movement should end.
4. Change the setting with its slider. That adds a second keyframe there by itself.

Play it back and the clip moves between the two. Add as many as you like.

- **◆** means there's a keyframe right at the playhead (click it to remove it).
- **◇** in teal means the setting has keyframes, just not here.
- Once a clip has keyframes, a **Keyframes** row appears near the top of Properties: **◀** and **▶** jump to the previous and next one, and **Clear** removes them all (the clip stays how it looks at the playhead).
- Little yellow diamonds on the clip in the timeline show where its keyframes are.

### How it gets there

Put the playhead on a keyframe and **Gets there** (in the Keyframes row) picks how the setting gets to it from the one before:

- **Smooth**: speeds up, then slows down to land (the usual).
- **Straight**: the same speed all the way.
- **Bounce**: goes a bit past and springs back. Fun on zooms and titles popping in.
- **Jump**: stays put, then snaps there right at the keyframe. No glide at all.

### Shake

**Shake** (in Position & size) jiggles the clip about like a handheld camera. Leave it on for the whole clip, or keyframe it for a quick rumble: 0% -> 80% -> back to 0% over half a second hits pretty hard. It's the same wobble every time, so what you see in the preview is what you get in the export. A full-size clip zooms in a touch while it shakes, so you never see its edges.

Keyframes stick to the bit of footage they're on, so splitting, trimming or speeding up a clip doesn't mess them up.

## Quick zoom

Right at the top of Properties for any video. Put the playhead where you want the zoom, then hit:

- **Punch**: snaps straight in. The YouTuber "wait, WHAT" zoom.
- **Bounce**: zooms in, goes a bit past, and springs back.
- **Smooth**: glides in nice and calm.

It stays zoomed for a couple of seconds, then goes back out. Tick **Shake when it lands** for a rumble when it gets there, and **Pick a spot** to aim it at something (click the spot on the preview). Otherwise it zooms into the middle.

**Z** (or **Zoom here** on the toolbar) does the last one you picked, at the playhead. So pick Punch once, then just hit Z at every funny bit. If there's a facecam on top, Z zooms the big video underneath, unless you've selected the facecam. Zooms stack up, so add as many as you like along a clip.

## Zoom & pan

The fine-tuning for Quick zoom, plus a few more moves. Ready-made keyframes for the most common moves, in the **Zoom & pan** section of Properties.

**Zoom into a spot** (great for screen recordings):

1. Put the playhead where the zoom should start.
2. Click **Pick the spot on the preview**, then click the spot in the preview.
3. Choose how far to **Zoom**, how long **Zooming takes**, and how long to **Stay zoomed**.
4. Click **Add**. It zooms in (in the Quick zoom style picked up top), stays, then zooms back out.

These settings are what Quick zoom and Z use too. The other moves (**slow zoom in**, **slow zoom out**, **pan left -> right**, **pan right -> left**) cover the whole clip and replace its size and position keyframes. Tweak any of it with the ◆ buttons afterwards.
