# Troubleshooting

[← Help home](README.md)

**Some clips show up black.**
Their files were moved or renamed since the project was saved. MixMedia tells you which ones when you open the project. Put the files back where they were (or import them again).

**Playback is choppy.**
Big 4K files ask a lot of a computer. It usually smooths out after the first play-through. If MixMedia is limited to a few CPU threads in [Settings](settings.md), try giving it more.

**"Instant" export is greyed out.**
It only works for simple cuts from one video, with no titles, effects, speed changes or subtitles in the picture. Use *Normal*.

**My export has black bars.**
The export size is a different shape from your video. Set the project's shape to match (see [Settings](settings.md)) and tick *Fill the frame* on your clips, or pick a preset that matches your project.

**The green screen leaves green bits / eats into the person.**
Turn **Strength** up for leftover bits, down if the person starts disappearing. Pick the colour again from a different part of the background if it's unevenly lit. See [Changing a clip](clips.md).

**Auto-captions got words wrong.**
Fix them in the Subtitles tab, double-click the text. A higher quality helps with tricky audio. Make sure the language is set right, and try **Remove noise** on noisy recordings first.

**Auto-captions can't download the model.**
It needs the internet the first time. The models are kept in MixMedia's data folder (under `models`). Deleting one there makes it download again next time.

**MixMedia crashed!**
Open it again: it offers to bring back your work from the last auto-save (up to a minute old).

**Still stuck?**
Open an issue on [GitHub](https://github.com/Shayan-Mazahir/MixMedia-Video-Editor/issues) and say what you did and what happened.
