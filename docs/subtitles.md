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
2. Choose the language that's **really spoken** (**English** by default, or *Work it out*). This matters: pick English for a video in Urdu and it gets slow and muddled, because it's guessing and translating at once.
3. Tick **translate** if you'd like the subtitles in English whatever's being said.
4. Choose a **quality** (see below).
5. The first time, it asks to download the speech model (once, then it's kept).
6. When it's done, **read through the lines** in the table. It's good, but not perfect, especially with names.

It listens to the timeline as you've edited it, so the subtitles line up with your cuts. It all happens on your computer: nothing gets uploaded. A long video can take a while (the progress bar tells you roughly how long). You can choose how much of your computer it uses in [Settings](settings.md).

### Which quality?

| Quality | Download | Good for |
|---|---|---|
| **Fast** (the usual pick) | 57 MB | Most things: quick, and the smallest download |
| **Good** | 182 MB | Fewer mistakes, a few times slower |
| **Great** | 548 MB | Accents, mixed languages, mumbling. Can't translate! |
| **Best** | 1 GB | The most accurate, and the one to use for **translating** |

Only the one you pick gets downloaded, and MixMedia itself stays the same size. Bigger ones are more accurate but slower, and use more memory while they work (about 0.4 GB for Fast up to 2 GB for Best). Start with Fast, and step up only if it gets too much wrong.

### Getting it more right

- **Names and words**: type names and unusual words that get said (like *MixMedia, Shayan, flexbox*), separated by commas. Whisper reads them first, so it spells them right.
- **Take extra care**: it weighs up a few guesses for each bit instead of going with the first. More accurate, about twice as slow.
- **Only listen to the talking** (on by default): a little speech detector finds where people are talking, and only those bits get listened to. Quicker for videos with music or quiet bits, and it stops whisper making up words in the silence.

### Your own models

Any **whisper.cpp model** (a `ggml-….bin` file) works, such as one trained for your language or a bigger one:

- In **Quality**, pick **Add a model file…** and choose it, or
- click **Models folder** and drop the file in there.

It then shows up in the list as *Your model: …*. The models folder is also where to delete the downloaded ones if you'd like the space back.

### Right-to-left languages

Urdu, Arabic, Persian and Hebrew subtitles come out right to left, with the letters joined up. Word by word lights up the words from the right.

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
