# Speech models explained

[← Help home](README.md) · [Subtitles and auto-captions](subtitles.md)

Auto-captions need a **speech model**: a file that has learned how speech sounds, so MixMedia can turn talking into words. You don't need to understand any of this to use auto-captions, the four in the **Quality** list are all you need. This page is for when you're curious, or want to try a different one.

## The four in the list

| In MixMedia | Download | What it's like |
|---|---|---|
| **Fast** (the usual pick) | 57 MB | Quick, small, good for most things |
| **Good** | 182 MB | Fewer mistakes, a few times slower |
| **Great** | 548 MB | Handles accents and mixed languages well. **Can't translate** |
| **Best** | 1 GB | The most accurate, and the one for **translating** |

**Nothing is downloaded until you need it.** The first time you run auto-captions, MixMedia asks, then downloads the one you picked (plus a tiny 1 MB "speech detector" that finds where people are talking). Each other one only downloads if you pick it. MixMedia itself stays the same size whichever you use.

Bigger is more accurate, but slower, and uses more memory while it works. Start with **Fast**, and only step up if it gets too much wrong.

## Where they're kept

In the **models** folder inside **MixMedia data** (next to MixMedia itself). The quickest way there: **Settings -> Files -> Open the folder**, or the **Models folder** button in the auto-captions window.

To **free up space**, just delete the ones you don't use from that folder. MixMedia asks before downloading one again.

## Trying a different model

All sorts of speech models are free to download, including ones trained to be better at a particular language. Here's how to get one into MixMedia:

1. Open the official collection: **[huggingface.co/ggerganov/whisper.cpp](https://huggingface.co/ggerganov/whisper.cpp/tree/main)**. (Hugging Face is the website where speech models are shared. No account needed.)
2. You'll see a list of files. Pick one using "Reading the names" below, then click the **small download arrow** next to its size.
3. When it's downloaded, put the file in MixMedia's **models** folder (see above).
4. Open auto-captions. It's in the **Quality** list as **Your model: …**

Or, in the auto-captions window, pick **Add a model file…** and choose the file wherever it is. It's then remembered (but it's not copied, so don't delete it).

### Reading the names

The file names look scary, but they're made of a few simple parts. Take `ggml-large-v3-turbo-q5_0.bin`:

| Part | What it means |
|---|---|
| `ggml-` … `.bin` | The kind of file MixMedia can use. **Only these work** |
| `tiny`, `base`, `small`, `medium`, `large` | The size: bigger hears better but is slower (tiny is 75 MB, large is 3 GB) |
| `v2`, `v3` | Newer versions of large, a bit better each time |
| `turbo` | A large one made much quicker, almost as good. Can't translate though |
| `.en` | **English only.** Slightly better at English, but if anything else is said it goes wrong badly (making things up, repeating itself) |
| `q5_0`, `q5_1`, `q8_0` | Squeezed: about a third of the size, quicker, and hears just about as well. No `q` = full size |

So `ggml-small.en-q5_1.bin` is the small one, English only, squeezed.

### Files that won't work

MixMedia uses **whisper.cpp** models, which are single `ggml-….bin` files. Lots of model pages on Hugging Face have other kinds of files (`.safetensors`, `.pt`, `.onnx`, `.gguf`), which are for other programs. If a model page doesn't have a `ggml` file, it won't work in MixMedia *(for now at least 👀)*. 

### A tip for word-by-word captions

MixMedia works out exactly when each word is said using the model's size, which it reads from the **file name**. So if you rename a model, keep the size in the name (like `ggml-small-urdu.bin`, not `my-model.bin`). Otherwise it still works, but the word timings are a little rougher.

## Other models worth a look

- **[distil-large-v3](https://huggingface.co/distil-whisper/distil-large-v3-ggml/tree/main)** (1.5 GB, English only): very good at English, about as quick as Great. Worth a try if Great makes mistakes on your English videos. Get `ggml-distil-large-v3.bin`.
- **[The speech detector](https://huggingface.co/ggml-org/whisper-vad/tree/main)**: the tiny file that finds the talking (MixMedia gets this one by itself).
- **[Everything else people have made](https://huggingface.co/models?search=whisper%20ggml)**: a search of Hugging Face for whisper models in the right format. Look for ones with a `ggml-….bin` file in their files list.

## Want to know more?

- **[OpenAI's Whisper](https://github.com/openai/whisper#available-models-and-languages)**: where these models come from, with the list of languages and how well each one is understood.
- **[whisper.cpp's models page](https://github.com/ggml-org/whisper.cpp/blob/master/models/README.md)**: the technical details of every model file.
- **[Whisper on Wikipedia](https://en.wikipedia.org/wiki/Whisper_%28speech_recognition_system%29)**: the plain-English story of what it is.
- **[Downloading from Hugging Face](https://huggingface.co/docs/hub/models-downloading)**: their own guide, if the website is confusing.

Seen a really good explanation somewhere else, like a Reddit post or a YouTube video? [Tell us on GitHub](https://github.com/Shayan-Mazahir/MixMedia-Video-Editor/issues) and we'll link it here.
