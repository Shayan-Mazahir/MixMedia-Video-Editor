### Hey! 👋

I'm **Shayan Mazahir**, and I made this. I wanted a video editor that's quick, small and doesn't bury everything under ten menus, so I built one. I made it mainly for Linux, because I couldn't find one there I really liked (it runs on Windows too). It's a hobby project, so you'll probably find the odd bug. When you do, [tell me about it]({repo}/issues) and I'll fix it.

[github.com/Shayan-Mazahir/MixMedia-Video-Editor]({repo})

### Your videos stay on your computer

Everything happens right here: editing, effects, exporting, all of it. Nothing gets uploaded, and there's no cloud. Even auto-captions, which use an AI speech model ([Whisper](help:speech-models.md)), run completely offline on your own computer, so your videos and voice never go anywhere.

MixMedia only goes online twice: to check for updates (you can turn that off in Settings), and to download a speech model the first time you use auto-captions (it asks first).

### You're in charge

It's your computer, so you decide how MixMedia uses it. How much of your CPU and graphics card it gets, which speech models it downloads (or bring your own), whether it checks for updates and which kind, light or dark, where the panels go, your keyboard shortcuts, your own fonts and colour looks... Pretty much everything's a setting, and the settings are all in one plain [settings.ini](folder:) file if you'd rather poke at it directly. 

And of course I will aim to make it as much customizable as I can, so that you can have control over pretty much everything :D

### Stuck?

[Getting started](help:getting-started.md) walks you through your first video. Everything else is in the [help pages](help:README.md) (F1 any time): [shortcuts](help:shortcuts.md), [speech models](help:speech-models.md), and [what to do when stuff breaks](help:troubleshooting.md).

### Free. Like, actually free.

No accounts, no ads, no subscription. The code's all out in the open ([GPL v3](https://www.gnu.org/licenses/gpl-3.0.html)): poke around, change it, share it. Just keep it open, and keep my name on it 🙂

[What's new]({repo}/releases/tag/v{version}) · [Got an idea?]({repo}/issues) · [The code]({repo})

### Couldn't have done it without

- [Qt {qt}](https://www.qt.io), for every window and button
- [FFmpeg](https://ffmpeg.org), which does all the heavy lifting with video
- [whisper.cpp](https://github.com/ggml-org/whisper.cpp) and OpenAI's [Whisper](https://github.com/openai/whisper), which listen so you don't have to type subtitles
- [Silero](https://github.com/snakers4/silero-vad), which spots when someone's actually talking

All free and open too (Qt: LGPL, FFmpeg and x264: GPL, the rest: MIT). Thank you!

Engine v{engine} · Your stuff lives in [MixMedia data](folder:) · Every release's notes have a "fun mistakes" bit, if you'd like [a laugh]({repo}/releases) 😄
