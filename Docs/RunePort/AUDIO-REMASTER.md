# Layered sound effects — Windows processing

## Current status — shelved

On November 2 the user judged the full collection mixed, with limited improvement even among the better examples, and shelved the sound pack. Preserve the source, optional packs and comparison pages; do not continue processing or enable it by default. Windows was read back as **Original** during the following shadow-light work. The earlier technical checks below establish format/runtime compatibility, not subjective approval of the collection.

## Production update — November 2, 2026

The user approved expanding the layered examples. The complete sound inventory contains **2,558 sounds** across all eleven shipped UAX packages. The latest pack, `outputs/audio-production-20261102/pack-v3`, contains **1,612 replacements**, **946 intentionally retained originals**, **337 authored loops**, zero technical holds and **145 suspect-review entries**. Review flags concern larger waveform changes or more than 2 dB of headroom-related level change; the flagged candidates are included and listed for later listening. Spoken dialogue, score, menu/silence cues and unclassified voices are retained. Full collection quality has not been established by individual listening to every sound.

Processing ran on Windows using source-aware offline impact, texture, movement and resonance layers. The two approved axe/shield WAV candidates are copied byte-for-byte. New voices/words are not generated. All replacements preserve rate, channel count, frame count and loop metadata. Added one-shot layers do not start before the authored onset. The seam guard was corrected after three fire loops failed the first pass; all loops now pass. Exact processing scripts, package/raw/replacement hashes, profiles, counts, holds and suspect notes are saved outside Git. No paid service, model or API key is used.

### Runtime integration

Windows and Android expose **Audio → Sound effects → Original / Remastered (WIP)**, default Original. **Restart Open Rune to apply.** The page explains the restart and reports a missing optional pack. Mac accepts `--sound-remaster=0/1`; its modern graphical settings UI remains unported.

The optional pack lives in `GAME/SoundPacks/OpenRune-Layered-v1`. `runtime.json` indexes identities, source/replacement SHA-1 fingerprints, dimensions and loops; the production audit separately retains SHA-256 hashes. These fingerprints prevent accidental source/version mismatches; they are not a publisher-authentication system. The engine checks the original encoded sound, replacement bytes, frame count, rate, channels and loops before selecting a WAV. Missing, changed, corrupt or incompatible assets fall back to the original. Paths leaving the pack root, duplicate identities and oversized files/indexes are rejected. Dedicated servers use original decoded timing and do not load this cosmetic pack.

Sounds are decoded/cached through the existing USound/OpenAL path. There is no new per-frame effect synthesizer. Switching requires restart so live OpenAL buffers and playing sources are not replaced. Gameplay volumes, pitch, event timing, network messages, combat wind-ups and the block window are not changed. Existing environmental reverb/obstruction and HRTF remain separate settings.

### Validation and delivery

- Windows and Mac native checks cover **all 2,558 sounds**: all 1,612 mapped candidates change the decoded signal, all 946 retained entries stay identical, and durations/channels/loops/sample-buffer sizes match. Native checks cover missing packs/files, source/file mismatch, timing mismatch and path escape. Windows saved-menu mode is exercised in an isolated configuration.
- The unchanged decoded-buffer sizes do not imply zero overhead: index memory, fingerprint validation and extra file I/O add work when a sound is first loaded. Raw timings are in the native summaries; they are not gameplay FPS benchmarks.
- Whole-pack file/hash verification passes on Windows and the Mac copy. Production tests cover dialogue/music exclusions, deterministic processing, stereo loops/headroom and no earlier one-shot onset. Existing signal tests pass. Menu models cover 48 Android and 42 Windows settings with one parent each.
- A Mac rendered goblin-combat smoke passes at native scale/4× AA with the pack active and no replacement rejection. It uses an isolated save/configuration and null audio output. It does not establish physical-speaker listening quality, Android FPS or sustained combat performance.
- The collection page includes 36 representative A/B pairs with embedded audio; all **72 playback buttons** pass Edge headless decoding/playback checks.
- The optional pack is installed alongside the Windows and Mac developer game copies. Original packages, configurations and saves are not overwritten; default sound choices are not changed. The saved Windows launcher uses the new build, with its prior executable backed up.
- The signed Android APK is built with the option/loader, retained signing key and optional Turnip payload. **It and the separate sound pack have not been installed on the tablet for this change.**

Replacement WAVs occupy about **131.6 MiB**; the private portable archive is about **120.7 MiB** at `outputs/audio-production-20261102/OpenRune-Layered-v1.zip`. Extract its `OpenRune-Layered-v1` folder into the game's `SoundPacks` directory. The archive contains owned-game-derived artwork and must remain outside source Git/public release. The app APK does not embed these original/derived sound assets. No server is started, and no commit/push/publication is performed.

New tools: `production.py`, `verify_production.py`, `install_pack.py`, `test_production.py`, `test_native_pack.py` under `Tools/RuneAudio`. Loader: `SurrealEngine/Audio/RuneSoundPack.h`; isolated native decoder fixture: `RuneSoundPackProbe.h`. Production reports and native summaries are in `outputs/audio-production-20261102` and the earlier audio evidence folder. The sections below describe earlier pilots and are superseded by this update where their status differs.

## Initial pilot history

November 2, 2026. The Windows workstation is the processing host; the Mac controls it over the existing SSH connection. Nothing is uploaded to an audio service. No paid tool, API key, model download or GPU inference is used. This first pilot uses the already-installed NumPy/SciPy stack for offline DSP. It is not generative AI, bandwidth reconstruction, declipping or a complete sound remaster.

## Actual output

Windows extracted **1,810 sounds** from WeaponsSnd, FootstepsSnd, EnvironmentalSnd, CreaturesSnd and OtherSnd with zero extraction holds. Original sound-package, configuration and save hashes match before/after. The new package-only engine export path does not initialize rendering/playback or run actors. It writes both exact original encoded bytes and decoded float WAV files; identities retain package/group names.

The **20-sound pilot** covers flesh/metal/wood/stone impacts, shields, a swing, wood/water/metal footsteps, two fire loops and goblin attack/hurt/death/awareness vocals. Source rates are 22,050 or 44,100 Hz. The processing applies conservative high-pass cleanup, body/presence EQ and transient shaping for impact categories. It preserves rate, channel count, frame count and original smpl/cue/LIST metadata, does not add artificial stereo or extend tails, and protects loop edges. Replacement candidates are 16-bit PCM with deterministic dither and a -1 dBFS four-times-oversampled peak ceiling. Original sounds remain separate.

The listening previews are RMS matched to within 0.1 dB, measured from the final quantized files. This controls average level, not perceptual loudness in every frequency band. Each card in `comparison.html` offers Original/Remastered switching at the current playback position. Full-file fire loops repeat for seam comparison. Review notes flag larger waveform changes and headroom reductions; low correlation can result from filter phase and is not proof of a defect.

**Listening approval remains pending.** No one has established that these samples sound better through the game mix. No runtime replacement loader/menu toggle is introduced, and no gameplay audio has been replaced on any device. The pilot is a comparison artifact, not an installed or automatically loadable game pack. Dialogue, music, environmental-routing behavior and game/network sound timing are unchanged.

## Locations and reproducibility

- Windows originals/export inventory: `C:/OpenRune/outputs/audio-remaster-20261102/exports`.
- Latest Windows pilot: `C:/OpenRune/outputs/audio-remaster-20261102/pilot-v3`.
- Mac copy and evidence: `outputs/audio-remaster-20261102/pilot-v3`, outside this source checkout.
- Previous pilot versions remain intact. Derived/original audio files must stay outside Git and must not be published with the source.

Each manifest records raw/decoded/package/replacement hashes, exact settings, rates/frames/loops, signal metrics, review notes, host/tool versions and processing-source hash. Each pack includes its exact processing script and selection. The tested Windows environment has NumPy 2.5.4 and SciPy 1.18.1. Processing runs at Below Normal process priority with two BLAS/OpenMP threads; extraction is sequential. No permanent system settings are changed.

Tools:

```text
python Tools/RuneAudio/export_samples.py --engine ENGINE --game OWNED_GAME --output NEW_OUTPUT
python Tools/RuneAudio/test_remaster.py
python Tools/RuneAudio/remaster.py --exports EXPORTS --selection SELECTION.json --output NEW_VERSION
python Tools/RuneAudio/verify_pack.py NEW_VERSION
```

The engine exporter is `--rune-export-sounds --sound-package=NAME --sound-output=NEW_FOLDER --save-directory=ISOLATED_FOLDER OWNED_GAME`. Export/report tools verify original hashes. Failed sound decodes are inventoried as held rather than fabricated. Processing outputs are versioned and refuse an existing destination.

## Validation and next boundary

The native Windows build passes. Five Windows signal/file tests cover stereo separation, exact lengths/rates, loop metadata, reproducibility, headroom, silence/nonfinite data and invalid PCM. All 20 actual samples pass duration/channel/rate, metadata, clipping/oversampled peak and final A/B-level checks. Verification runs against the Windows pack and copied Mac pack. No claim of listening quality or in-game performance follows from these checks.

After listening, the next step is a source-hash-aware, optional audio replacement loader with Original/Remastered selection and original fallback, followed by in-game combat/ambience listening and CPU/memory checks. Preserve authored volumes, pitch, timing and network event delivery. Do not replace every sample based solely on this pilot.

Separately, the requested larger Windows shadow-light budget is recorded in [IDEAS.md](IDEAS.md); this audio task does not change the renderer.

### Layered impact examples

The user heard no worthwhile quality improvement in the EQ/transient pilot. That pilot remains uninstalled. Two substantially re-authored examples are now prepared on Windows in `layered-examples-v2`: an axe/flesh hit and a shield impact. They combine owned Rune donor recordings with original synthesized percussion, texture and modal resonance. They use no external sound recording, model or paid service. Harmonics on the low-impact layers are intended to retain weight through small speakers; this has not been verified by listening on the tablet.

Both candidates retain the stock event's frame count, sample rate and mono format. Added layers start at the original event onset. Layer weights, donor hashes, procedural code and separately auditionable stems are included. The comparison files are level matched and the player embeds its audio. Windows file/peak checks pass for both examples, and all four Original/Layered buttons decode and start playback in Edge headless. Subjective quality remains for user listening; waveform correlation only establishes a stronger signal change, not improved quality. No runtime audio replacement is installed.

The earlier pilot's exact processing source and all previous outputs remain intact. The two-example pack includes its own processing source and helper module, with hashes in its manifest. See `Tools/RuneAudio/layered_examples.py` for repeatable construction from the existing private exports.

### Preview playback follow-up

The initial HTML depended on neighboring WAV files and its comparison buttons only selected a source. The page now embeds all preview audio, provides explicit Play original/Play remastered buttons, calls playback inside the user gesture, pauses other players and reports browser failures. Edge headless successfully decodes all 40 clips and starts all 40 play buttons; physical speaker output and Codex preview behavior are separate. A Mac loopback-only browser server is available for the current review; `comparison-playback-fixed.html` also works as a self-contained file in a normal browser. No audio processing or replacement bytes changed.
