# Changing a clip

[← Help home](README.md)

Click a clip on the timeline and its settings show up in **Properties** on the right. Every slider has a box next to it for typing an exact number, and **double-clicking a slider** puts it back to normal.

There are a lot of settings, so they come in sections. **Click a section's title to fold it away** (or open it again). The ones most people only need now and then (crop, zoom & pan, green screen, animation) start folded, and MixMedia remembers how you leave them.

## Speed

- **Play at**: below 1× is slow motion, above is fast forward (0.1× to 10×). Use *Presets…* for the usual ones.
- **Keep the sound's pitch**: on means voices sound normal, just faster or slower. Off sounds like a record at the wrong speed.
- **Play backwards**: plays the clip in reverse (its sound too).
- **Freeze frame at the playhead**: holds the frame under the playhead for 2 seconds and pushes the rest of the clip along. Also in **Edit -> Freeze frame** (`Ctrl+Shift+F`).

## Effects

Pick a **Look** (black & white, vintage, vivid... or [your own .cube looks](effects-and-transitions.md)), and fine-tune brightness, contrast, colour, warmth, blur, sharpen and vignette. *Reset effects* puts it all back. To put an effect over several clips at once, use an effect block instead, see [Effects and transitions](effects-and-transitions.md).

## Position & size

Make a clip smaller and move it around for **picture-in-picture**: put one clip on Video 2 above another on Video 1, and shrink it. **Quick spot** puts it straight into a corner, a half of the screen, or a cell of a 2×2 grid. **Opacity** makes it see-through. These can all change over time with [keyframes](keyframes.md).

## Crop & rotate

- **Fill the frame**: zooms in just enough that there are no black bars. Handy for putting wide footage in a tall video. Use *Left / right* to pick which part shows.
- **Blurred background**: keeps the whole picture, and fills the black bars around it with a blurred, zoomed-in copy of the same video instead (the way phones show wide videos). Works on shrunk-down clips too.
- **Rotate**: any angle, or quarter turns with **↺ 90°** and **↻ 90°**.
- **Mirror ↔** and **Flip ↕**: light up when they're on.
- **Crop**: cut a bit off any edge.

## Frame (facecams and the like)

For a webcam over gameplay (or any clip shown small on top of another). Put the facecam on a track above the gameplay, then in **Frame**:

- **Facecam**: one click for a ready-made look, like *Circle, bottom right* or *Rounded box, top left*. It sets the shape, a white border, a shadow, the size and the spot all at once (one Ctrl+Z undoes it).
- **Shape**: rectangle, **rounded corners** (with **Corners** for how round), or **circle**. Picking circle crops the sides off for you, so it's a proper circle rather than a squashed one.
- **Border** and **Border colour**: a line round the edge.
- **Shadow**: a soft shadow behind it, so it stands out from what's underneath.

It works with **Green screen** (further down) too, for a cut-out webcam with no box at all.

## Zoom & pan

Ready-made movements, like zooming into one spot of a screen recording. See [Keyframes and zoom & pan](keyframes.md).

## Green screen

It works with **any colour of screen**: green, blue, red, purple, yellow, or even white or black. Put the screen clip on a track above whatever should show behind it, then tick **Remove a colour**. Tell it the screen's colour:

- **Screen**: pick the usual colour (Green, Blue, Red...) to start from.
- **Find it**: works out the exact shade from the frame under the playhead (it looks round the edges, where the screen usually is).
- **Pick**: click the background in the preview yourself.

The clip goes see-through wherever it's that colour. It copes with uneven lighting, so a screen that's darker in the corners still goes. Then fine-tune:

- **Strength**: how different from the colour something can be and still go. Turn it up if bits of the background are left over, down if the person starts disappearing (or fine hair goes).
- **Soft edge**: how gradually things go see-through, for smoother edges.
- **Remove glow**: takes the green tint off hair and edges.
- **Clean edges**: softens the edge, and turned up, pulls it in a little to get rid of any fringe of screen colour round the person. Turn it down if fine hair goes missing.
- **Show what's see-through**: the preview shows what's kept in white and what's gone in black. Tune until the person's solid white and the background's solid black, then untick it. It never ends up in the export.

A few tips:

- **Light the screen evenly.** Colourful screens cope well with a bit of shadow, but **white and black screens need even lighting**: a white screen that's grey in the corners looks just like grey clothes.
- **Don't wear the screen's colour** (a green top on a green screen goes see-through too). That's why green and blue are the usual ones: hardly anyone's skin or clothes are those colours.
- Very fine, wispy hair is the hardest bit. If it goes missing, lower **Strength** or **Clean edges** a little.
