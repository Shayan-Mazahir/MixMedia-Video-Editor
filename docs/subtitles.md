# Subtitles and auto-captions

[← Help home](README.md)

Everything lives in the **Subtitles** tab on the left, and on the **Subtitles** track at the top of the timeline.

## Writing them yourself

- **+ Line** adds a line at the playhead, ready to type.
- **Double-click** a time or the words in the table to change them. Times are minutes:seconds (like `1:02.5`).
- Clicking a line jumps the playhead there and shows it in Properties.
- On the timeline, subtitle lines can be moved, trimmed and split like any clip.
- **Shift all…** moves every line earlier or later, for when a whole file is a bit out of sync.

## Auto-captions

Let MixMedia listen to your video and write the subtitles for you:

1. Click **Auto-captions…** (in the Subtitles tab, or **Edit → Auto-captions…**).
2. Choose what language is spoken (**English** by default, or *Work it out*), and tick **translate** if you'd like English subtitles whatever's being said.
3. Choose a **quality**. *Good* suits most things. *Fast* is rougher, *Better* and *Best* are more accurate but slower and bigger.
4. The first time, it asks to download the speech model (once, then it's kept).
5. When it's done, **read through the lines** in the table. It's good, but not perfect, especially with names.

It listens to the timeline as you've edited it, so the subtitles line up with your cuts. It all happens on your computer: nothing gets uploaded. A long video can take a while (the progress bar tells you roughly how long). You can choose how much of your computer it uses in [Settings](settings.md).

## How they look

Select any line and change the look in Properties: font, size, colour, outline, shadow, box and so on, just like [titles](titles.md). Normally every line on the track shares one look, so you only change it once. Tick **This line has its own look** to style one line differently.

**Word by word** shows a few words at a time, lighting up each one as it's said (the TikTok style). Pick the **lit-up colour** and how many **words at once**. Auto-captions know exactly when each word is said; for lines you typed or imported, MixMedia spreads the words out over the line.

## Importing and exporting

- **Import…** brings in an `.srt` or `.vtt` file. If there are subtitles already, it asks whether to replace them or add these as well.
- **Export…** saves them as an `.srt` file (what YouTube and most other places want).

## When you export the video

The export window has a **Subtitles** section. Tick any mix of:

- **In the picture**: always showing, works everywhere (burned in).
- **As an .srt file next to the video**: same name, for uploading alongside it.
- **As a track viewers can switch on and off**: inside the MP4. VLC, phones and most players understand it.
