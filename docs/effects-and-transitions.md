# Effects and transitions

[← Help home](README.md)

## Effect blocks

The **Effects** tab has looks (vintage, black & white, warm...) and adjustments (blur, sharpen, vignette...). Drag one onto an FX track and it changes **everything below it**, for as long as the block lasts. Stretch it to cover more. Stack a few on different FX tracks to combine them. **Strength** in Properties dials it down.

Want an effect on just one clip? Use that clip's own **Effects** in Properties instead (see [Changing a clip](clips.md)).

## Your own looks

Colour-grading programs share their looks as **.cube** files (also called **LUTs**), and loads are free online. Search for "free cube LUTs" and you'll find heaps: film looks, moody, warm, teal and orange...

1. Pick **Open the looks folder…** at the bottom of the **Look** list in Properties (or Settings -> Files).
2. Drop your `.cube` files in there.
3. They show up in the **Look** list straight away, under the built-in ones, by their file name.

When one of yours is picked, **Look strength** dials it down (100% is all of it). The other sliders (brightness, warmth and so on) still work on top, so you can tweak it. It works on effect blocks too, so one look can cover a whole stretch of the video.

Only 3D `.cube` files work (nearly all of them are). A file that isn't one just does nothing.

## Transitions

The **Transitions** tab has dissolves, fades through black, wipes, slides and a zoom. Where you drop one decides what it does:

| Where you drop it | What it does |
|---|---|
| **On a cut** | The clip after slides back to overlap the one before, and they blend. The video gets shorter by the transition's length. |
| **At the start of a clip** | The picture comes in from black. |
| **At the end of a clip** | The picture goes out to black. |
| **Anywhere else** | Plays on the spot: out and straight back in. |

Transitions on a cut are locked to it. To change its length, select it and use **Length** in Properties. Deleting it slides the clips back apart. Moving the clip after a cut takes its transition along with it.

You can also right-click a clip -> *Transition into this clip*.
