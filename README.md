# ECHOFIELD

An original, experimental granular sampler for the **R36S** handheld (ArkOS, launched
through **PortMaster**). Record your **FM-1** — or any class-compliant USB audio device —
straight into the handheld, and turn a few seconds of sound into clouds, shards, tape
smears, ghosts, pulses and slowly drifting textures.

Version 1.0.0 · native C + SDL2 · aarch64 · 640×480

---

## 1. Install (R36S / ArkOS)

The archive `ECHOFIELD_R36S_PortMaster.zip` contains:

```
Echofield.sh                 <- launch script (shows up in PORTS)
echofield/
  echofield.aarch64          <- the program (native aarch64, needs only libc, libm, SDL2)
  assets/samples/*.wav       <- 5 factory sounds
  licenses/                  <- font licenses, ECHOFIELD license
  port.json  gameinfo.xml  screenshot.png  cover.png
  README.md  HARDWARE_TESTS.md
```

1. Make sure PortMaster is installed (ArkOS ships it under *Options → Tools → PortMaster*).
2. Copy the **contents** of the zip into the ports folder of the SD card:
   * single-card ArkOS: `/roms/ports/` (on a PC: the `EASYROMS` partition → `ports/`)
   * two-card setup: `/roms2/ports/`

   You should end up with `ports/Echofield.sh` and `ports/echofield/echofield.aarch64`.
3. Refresh the menu: *START → Quit → Restart EmulationStation* (or reboot).
4. Open **PORTS → ECHOFIELD**.

Updating: extract a newer zip over the old one. Your recordings, samples, presets and
projects live in `ports/echofield/userdata/`, which the zip never contains, so they are
never overwritten. (If the ports partition is read-only, ECHOFIELD falls back to
`~/.local/share/echofield/`.)

## 2. Recording the FM-1

1. Connect the FM-1 to the R36S **OTG** USB-C port (not the DC/charging port), using a
   USB-C↔USB-C OTG cable or an OTG adapter. If the FM-1 does not light up or appears and
   disappears, use a **powered USB hub** — the R36S OTG port supplies little current.
2. Press **R2** anywhere. The RECORD screen opens the FM-1 input and recording starts as
   soon as the stream runs. Play.
3. Press **R2** again to stop. The **TAKE** screen shows the waveform: trim with the
   D-pad (↕ chooses START/END marker, ↔ moves it), **X** listens, **Y** toggles normalize.
4. Press **A** to keep. The take is saved to `userdata/recordings/FM-1_<date>_<time>.wav`
   (32-bit float, original rate and channels) and immediately becomes the playing sample.
   **B** discards (with confirmation).

The RECORD screen shows a 5-step checklist for the selected device so you always know
*where* it fails:

| Step | Meaning | If it fails |
|---|---|---|
| 1 USB device connected | something is enumerated on the OTG port | cable/adapter/power, try a powered hub |
| 2 exposes audio capture | the device's USB descriptors contain an audio **IN** stream | **"PLAYBACK ONLY"** or **"MIDI only"**: the device firmware does not send audio to a host. Nothing on the R36S can fix that |
| 3 driver / ALSA device | Linux `snd-usb-audio` created a sound card for it | kernel without USB-audio support, or driver refused the device |
| 4 capture stream open | ALSA accepted a format (shows format / channels / rate) | the error text from ALSA (busy, unsupported format…) |
| 5 real signal received | samples above −70 dBFS are arriving | "digital silence": check the FM-1's USB audio output / volume |

ECHOFIELD never silently records another device instead of the one you chose, and never
records its own output.

**Input gain**: L1/R1 (or ←/→) on the RECORD screen, −12…+30 dB digital gain. The CLIP
lamp lights when the source itself hits full scale or the gain pushes it over.
**Monitoring**: Y toggles hearing the input through the R36S (off by default to avoid
feedback when the FM-1 also plays back R36S audio). Disconnecting the device during a
take stops the recording safely and offers the partial take; plugging it back in reopens
the input automatically. If the app is killed while recording, the take is recovered on
the next launch (`recordings/recovered_*.wav`).

### About the FM-1 (M-VAVE FM-1)

The FM-1 is a JieLi AC791N-based FM synth. Its USB-C port carries power and USB-MIDI. As
of October 2026, public information says the **stock firmware receives audio from a
host** (USB playback) but does not document sending its own audio **to** a host. Several
community firmwares (e.g. SLOOP, Clipper, FIZZ, Frodhes, Hortator) add class-compliant
USB audio streaming of the synth output. Whether your FM-1 can be recorded over USB
therefore depends on its firmware — checklist step 2 tells you definitively. If it reports
PLAYBACK ONLY / MIDI only, use a firmware with USB audio output, or record the FM-1's
headphone output with a class-compliant USB audio interface (also supported).

## 3. Controls

| Button | Main screen | Everywhere else |
|---|---|---|
| D-pad ↕ | select control | select item |
| D-pad ↔ | adjust (hold = accelerate, **L2 held = fine**) | adjust / move marker |
| A | play / stop the grains | confirm / load / open |
| B | — | back / cancel |
| X | **RANDOM** (respects locks) | context action (preview, rescan, listen) |
| Y | lock / unlock control for RANDOM | toggle / delete (always asks first) |
| L1 / R1 | previous / next **mode** | tabs, pages, input gain |
| L2 | **FREEZE** | fine adjust while held |
| R2 | **RECORD** (start / stop) | record |
| START | menu | menu |
| SELECT | contextual help (tap) | help |
| SELECT + X | undo last RANDOM | |
| SELECT + START | quit (session is saved) | |
| Left stick | X: scrub position · Y: bend pitch ±12 st | |
| Right stick | X: push TEXTURE · Y: push SCATTER | |

Sticks spring back to the stored values. If A and B are swapped on your unit:
*SETTINGS → SWAP A / B*. The PortMaster hotkey exit (gptokeyb, usually SELECT+START)
may close the app instantly — that is safe: the session is autosaved every 10 s and an
unfinished recording is recovered at the next launch.

## 4. The instrument

### Macros (main screen)
POSITION · TEXTURE (grain size + overlap) · SCATTER (randomness of position/timing/size) ·
DRIFT (slow pitch/position wander) · MOTION (self-animation) · PITCH · SPACE (reverb) ·
TAPE (tape stage) · WIDTH · LEVEL. Press **SELECT** on any control for an explanation,
including what it does *in the current mode*.

### Modes (L1 / R1)
| Mode | Behaviour |
|---|---|
| CLOUD | dense overlapping Hann grains (80–900 ms, 3–10× overlap), slow LFO sway |
| SHARDS | 6–90 ms grains, stutter repeats, reversals, octave/fifth jumps |
| TAPE | varispeed "tape heads" following a moving playhead; speed = pitch; wow, flutter, motor stops |
| GHOST | sparse 0.3–2 s grains, upper partials (+12/+19/+24), thinned per-grain filtering, long tails |
| PULSE | grains on a euclidean 16-step clock (MOTION = 40–200 BPM), per-step slices, mutation |
| DRIFT | random walks through position, pitch, size and density |

### Engine
Real asynchronous granular synthesis: up to 96 voices (default cap 40), 4-point Hermite
interpolation, five grain envelopes (Hann, Tukey, percussive, swell, Gaussian), per-grain
pitch / position / size / direction randomization, per-grain colour filter, region
boundaries enforced at grain birth, sample-accurate scheduling, parameter smoothing,
click-free sample swaps.

**GRAIN page**: grain size and density multipliers, SCAN (auto-movement, ±1 = real-time
speed), reverse chance, pitch and size randomness, envelope, grain tone.

**EFFECTS page** (signal order): tape (wow/flutter delay line → saturation → wear/LP →
optional hiss) → state-variable filter (LP/BP/HP + resonance) → saturation → stereo delay
(ping-pong, filtered/saturated feedback hard-limited to 0.9) → 8-line FDN reverb with
optional octave-up shimmer → width → master level → soft limiter (never hard-clips; LIM /
CLIP indicators).

**DRIFT ORACLE** (optional): root + 12 scales (major, minor, dorian, phrygian, lydian,
mixolydian, pentatonics, hirajoshi, in-sen, whole tone, "glass"), weighted bass motion
(root/fifth favoured, AMBIGUITY flattens), chords from scale thirds with suspensions,
GLIDE, RANGE, chance events (bloom, scatter burst, reverse swell, octave flicker, leap,
hush), DRONE mode and HOLD.

**RANDOM** (X) is weighted and constrained: musical pitch choices, no muffled low-pass,
no silent density, safe delay feedback; LEVEL is never randomized; locked controls (Y) are
skipped; SELECT+X undoes.

### Samples, editing, presets, projects
* **SAMPLES**: RECORDINGS / MY SAMPLES / FACTORY tabs; A load, X preview, Y delete
  (confirmed; factory sounds are read-only). Copy your own WAVs to `userdata/samples/`.
  WAV: PCM 8/16/24/32-bit, float 32/64, WAVE_FORMAT_EXTENSIBLE, any rate (resampled with
  a windowed-sinc converter), mono/stereo (more channels are folded to stereo).
* **EDIT SAMPLE**: START/END region and POSITION markers, A loop-listens the region,
  X reverse, Y normalize — all non-destructive; **R1 renders** the region into a *new*
  file in `samples/` (originals are never modified).
* **PRESETS**: 10 factory presets (FM-1 Afterimage, Glass Rain, Hollow Machine, Distant
  Broadcast, Soft Debris, Broken Constellation, Night Current, Magnetic Fog, Slow
  Satellites, Unstable Memory) + Init. X saves the current sound with a generated name.
  User presets remember their sample; a missing sample is reported and the current one kept.
* **PROJECTS**: save/restore sample + all parameters + region + locks. The last session is
  autosaved (every 10 s when changed, and on exit) and restored at launch.

All files are plain text with `format=` and `version=` lines (forward compatible).

```
userdata/
  recordings/   samples/   presets/*.efpreset   projects/*.efproj
  session.efproj  settings.ini  echofield.log  diagnostics.txt
```

## 5. Troubleshooting

* **Nothing in PORTS** – check the file locations above and restart EmulationStation.
* **Black screen / immediate exit** – read `ports/echofield/log.txt`. "missing system
  libraries" means the firmware lacks SDL2 (not expected on ArkOS).
* **No sound** – DIAGNOSTICS → AUDIO OUTPUT; check the system volume (ArkOS volume keys).
* **Crackles** – SETTINGS → MAX GRAINS lower (24–32) or ECO MODE on.
* **Recording problems** – RECORD checklist (section 2) and DIAGNOSTICS → CAPTURE /
  USB DEVICES. Save a report with DIAGNOSTICS → A.
* From SSH: `cd /roms/ports/echofield && ./echofield.aarch64 --diag` prints the full
  audio/USB report; `--bench 5` measures DSP speed per mode.

## 6. Architecture

| Module | Role |
|---|---|
| `src/engine.c` | granular engine, 6 modes, DRIFT ORACLE, effects, limiter (real-time, lock-free, no allocation) |
| `src/capture.c` | /proc + /sys discovery, USB descriptor parsing, ALSA capture thread (libasound loaded at run time), recorder + crash-safe writer thread |
| `src/sample.c` | WAV decode/encode, streaming writer + repair, sinc resampler, render/trim/reverse |
| `src/audio.c` | SDL2 audio output callback (renders the engine, measures DSP load) |
| `src/ui.c`, `src/draw.c` | 640×480 interface, controls, worker jobs, recorder workflow |
| `src/store.c` | presets, projects, settings, constrained randomizer |
| `src/diag.c` | diagnostics report, `--diag`, `--bench` |
| `src/compat/` | minimal libc / SDL2 declarations (see Building) |

Threads: UI (30 fps) · SDL audio callback · ALSA capture thread · recorder disk writer ·
worker (load/resample/save). Capture → audio uses a lock-free SPSC ring with drift
correction (monitoring); capture → recorder uses a preallocated buffer with an atomic
write index; sample swaps happen under SDL's audio lock with a 20 ms crossfade and old
buffers are freed on the UI thread.

## 7. Building

Requirements: `clang`, `lld`, `python3` (+ Pillow and NumPy only to regenerate fonts /
factory samples / screenshots). **No aarch64 sysroot or SDL/ALSA headers are needed.**

```
./build.sh test      # automated tests (host, with ASan/UBSan) + optimized run
./build.sh host      # desktop build for development (uses system libSDL2)
./build.sh r36s      # build/echofield.aarch64 + ELF/ABI checks
./package.sh         # -> dist/ECHOFIELD_R36S_PortMaster.zip
```

How the R36S build works: the program uses only a small, layout-safe subset of libc
(`src/compat/minilibc.h`), SDL2 (`src/compat/sdl_min.h`) and ALSA via `dlopen`. It is
compiled with `clang --target=aarch64-linux-gnu -mcpu=cortex-a35`, started by a tiny
`_start` (`src/crt/start_aarch64.S`), and linked with `ld.lld` against generated stub
libraries carrying the real SONAMEs and the `GLIBC_2.17` symbol version
(`tools/mkstubs.py`). The result runs on any aarch64 glibc ≥ 2.17 (ArkOS has 2.30);
`tools/check_elf.py` verifies architecture, interpreter, needed libraries and glibc
versions. With a normal aarch64 toolchain you can also compile `src/*.c` conventionally.

Desktop testing: `SDL_VIDEODRIVER=dummy ECHOFIELD_APPDIR=port/echofield build/echofield.host
--windowed --screenshots DIR` renders every screen; `ECHOFIELD_TEST_PCM=<alsa pcm>` adds a
clearly labelled simulated input; `ECHOFIELD_SCRIPT="R2,wait:1500,R2,A,quit"` scripts buttons.

## 8. What has been verified

| Area | Status |
|---|---|
| DSP engine, all modes, effects stability, feedback limits, voice limits, boundaries, NaN/Inf/clipping, oracle scale constraint | **automated tests, desktop** (165 checks, ASan/UBSan clean) |
| WAV formats, trimming, reverse, mono/stereo conversion, resampling, crash repair | automated tests, desktop |
| Presets / projects / settings round trips, version handling, randomizer constraints | automated tests, desktop |
| USB / ALSA discovery logic (FM-1 with capture, playback-only, no driver, nothing connected) | automated tests on **simulated** /proc and /sys trees |
| ALSA capture path (open, negotiate, read, convert, record, stop at max, temp-file recovery, error on missing device, exit while recording) | automated tests through the real libasound with the ALSA *file* plugin — **simulation, not USB hardware** |
| Full UI and record → keep → play workflow | scripted headless run on desktop with the simulated input |
| aarch64 binary | built and statically checked (ELF, interpreter, libs, glibc 2.17) — **not executed** (no emulator available) |
| R36S launch, controls, display, audio, **FM-1 USB capture** | **NOT YET TESTED** — see `HARDWARE_TESTS.md` |
