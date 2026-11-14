# ECHOFIELD: hardware checklist

ECHOFIELD has been tested on a real R36S. If something doesn't work on your unit or firmware, run these in order and
note the result. The in-app **DIAGNOSTICS** page and
`ports/echofield/log.txt` hold the details; a saved report is written to `ports/echofield/userdata/diagnostics.txt`.

| # | Test | How | Pass when |
|---|------|-----|-----------|
| H1 | Launch | PORTS → ECHOFIELD | the main screen appears within a few seconds |
| H2 | Libraries | read `ports/echofield/log.txt` | no "missing system libraries", no "video init failed" |
| H3 | Play | press A | sound starts; the ORBIT visualiser moves |
| H4 | Buttons | press every button | D-pad picks and turns knobs, L1/R1 change the knob page, X dice, B undo, Y next scene, L2 freeze, R2 SAMPLES, SELECT next visualiser, START menu. If A/B feel swapped: SETTINGS → SWAP A / B |
| H5 | Sticks | move both sticks | left: TONE and REVERB move; right: ECHO and SHIMMER move; they stay put when you let go |
| H6 | Audio | listen on the speaker and on headphones | clean sound, no crackle; DIAGNOSTICS shows DSP load well under 100 % |
| H7 | Own samples | copy a WAV to `ports/echofield/userdata/samples/`, R2 → MY SAMPLES | the file is listed and plays |
| H8 | Exit and restore | SELECT + START | back in EmulationStation; relaunching restores the session |
| H9 | Benchmark | SSH: `cd /roms/ports/echofield && ./echofield.aarch64 --bench 5` | every scene well above 1.0× real time (send the numbers so the grain limits can be tuned) |
| H10 | Diagnostics | SSH: `./echofield.aarch64 --diag` | prints the system and audio report |

If something fails, send `log.txt` and `userdata/diagnostics.txt`.
