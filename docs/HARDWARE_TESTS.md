# ECHOFIELD – tests that need the real R36S and FM-1

Everything below was **not** possible in the development environment (no R36S, no
FM-1, no aarch64 emulator). Please run these in order and note the result. Each test
says what to look at; the in-app **DIAGNOSTICS** page and `echofield/log.txt` hold the
details. A saved report is written to `echofield/userdata/diagnostics.txt` (DIAGNOSTICS → A).

| # | Test | How | Pass when |
|---|------|-----|-----------|
| H1 | Launch | PORTS → ECHOFIELD | Main screen appears within a few seconds, glass_tines plays, header CPU < 40 % |
| H2 | Binary / libraries | read `echofield/log.txt` | no "missing system libraries", no "video init failed" |
| H3 | Controls | press every button | D-pad moves, A plays/stops, B goes back. If A/B feel swapped: SETTINGS → SWAP A / B |
| H4 | Sticks & triggers | move both sticks, press L2/R2 | left stick scrubs position / bends pitch, L2 toggles FROZEN, R2 opens RECORD |
| H5 | Exit | SELECT+START → A | returns to EmulationStation; relaunch restores the session |
| H6 | Audio output | listen on speaker + headphones | clean sound, no crackle; DIAGNOSTICS → dsp load avg < 50 % |
| H7 | FM-1 enumerates | plug FM-1 into the **OTG** USB-C port, RECORD → X | checklist step 1 ✓ shows the FM-1 (vid:pid). If ✗: try another cable/adapter or a powered hub |
| H8 | FM-1 capture interface | RECORD | step 2 ✓ "capture endpoint present". **"PLAYBACK ONLY"** or "MIDI only" = the FM-1 firmware does not send audio to a host (see README → FM-1 firmware) |
| H9 | Kernel driver | RECORD | step 3 ✓ "ALSA card N". "kernel has no snd-usb-audio" = ArkOS kernel limitation (see README) |
| H10 | Stream opens | select FM-1, press A | step 4 ✓ with format/channels/rate |
| H11 | Real signal | play the FM-1 | step 5 ✓, L/R meters move, CLIP lights only when very loud |
| H12 | Record | R2, play 10 s, R2 | TAKE screen shows the waveform; A keeps it → main screen plays the take within seconds |
| H13 | Unplug while recording | unplug FM-1 during REC | "device lost"; take review offers the partial take; no crash; replugging reopens input |
| H14 | Persistence | quit, relaunch, SAMPLES → RECORDINGS | the take is listed and loads |
| H15 | Monitor | RECORD → Y | you hear the FM-1 through the R36S; no runaway feedback |
| H16 | Benchmark | SSH: `cd /roms/ports/echofield && ./echofield.aarch64 --bench 5` | every mode well above 1.0x realtime; send the numbers so voice limits can be tuned |
| H17 | Diagnostics CLI | SSH: `./echofield.aarch64 --diag` | prints cards, USB devices and the FM-1 capture verdict |

If something fails, send `log.txt` and `userdata/diagnostics.txt`.
