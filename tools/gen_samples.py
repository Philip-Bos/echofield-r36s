#!/usr/bin/env python3
"""Synthesize ECHOFIELD's factory samples (original material, public domain / CC0).
Each one is designed as granular source material: sustained, harmonically rich and
stereo where it helps. usage: gen_samples.py OUTDIR"""
import sys, wave, numpy as np
out = sys.argv[1]
SR = 44100
rng = np.random.default_rng(7)

def save(name, x):
    x = np.asarray(x, dtype=np.float64)
    if x.ndim == 1: x = x[:, None]
    x = x / (np.max(np.abs(x)) + 1e-9) * 0.89
    fade = int(0.01 * SR)
    x[:fade] *= np.linspace(0, 1, fade)[:, None]; x[-fade:] *= np.linspace(1, 0, fade)[:, None]
    pcm = (x * 32767).astype('<i2')
    with wave.open(f"{out}/{name}", "wb") as w:
        w.setnchannels(x.shape[1]); w.setsampwidth(2); w.setframerate(SR); w.writeframes(pcm.tobytes())

def t(sec): return np.arange(int(sec * SR)) / SR

def fm_note(f, dur, ratio=3.5, index=4.0, decay=2.5):
    tt = t(dur); env = np.exp(-tt * decay); ienv = np.exp(-tt * decay * 2.2)
    return env * np.sin(2*np.pi*f*tt + index * ienv * np.sin(2*np.pi*f*ratio*tt))

# 1 glass tines: FM bell arpeggio, stereo
L = np.zeros(int(8.0 * SR)); R = np.zeros_like(L)
notes = [523.25, 659.25, 783.99, 987.77, 1174.66, 783.99, 659.25, 1318.5, 587.33, 880.0]
for i, f in enumerate(notes):
    n = fm_note(f, 2.4, ratio=3.5 + 0.01*i, index=3.0, decay=1.8); s = int(i * 0.52 * SR)
    pan = 0.5 + 0.4 * np.sin(i * 1.7)
    L[s:s+len(n)] += n * np.sqrt(1-pan); R[s:s+len(n)] += n * np.sqrt(pan)
save("glass_tines.wav", np.stack([L, R], 1))

# 2 breath choir: noise through moving formant resonators, stereo
def reson(x, f, q):
    y = np.zeros_like(x); y1 = y2 = 0.0
    for i in range(len(x)):
        w = 2*np.pi*f[i]/SR; r = np.exp(-w/(2*q)); a1 = -2*r*np.cos(w); a2 = r*r
        yi = x[i] - a1*y1 - a2*y2; y2 = y1; y1 = yi; y[i] = yi
    return y
dur = 7.0; tt = t(dur); chans = []
for c in range(2):
    nz = rng.standard_normal(len(tt)) * (0.6 + 0.4*np.sin(2*np.pi*0.21*tt + c))
    acc = np.zeros_like(tt)
    for base, mv in [(420, 120), (1150, 300), (2600, 500)]:
        acc += reson(nz, base + mv*np.sin(2*np.pi*(0.13 + 0.05*c)*tt + base), 18.0)
    chans.append(acc * np.minimum(1, tt*1.5))
save("breath_choir.wav", np.stack(chans, 1))

# 3 dust rhythm: kick, clicks, metal - mono 4 s
x = np.zeros(int(5.0 * SR))
for b in range(8):
    s = int(b * 0.5 * SR); kt = t(0.35)
    kick = np.sin(2*np.pi*(45 + 120*np.exp(-kt*30))*kt) * np.exp(-kt*9); x[s:s+len(kick)] += kick
    hs = s + int(0.25*SR); ht = t(0.12)
    hat = rng.standard_normal(len(ht)) * np.exp(-ht*40); hat = np.diff(hat, prepend=0)
    x[hs:hs+len(ht)] += 0.4*hat
    if b % 3 == 1:
        mt = t(0.6); metal = sum(np.sin(2*np.pi*f*mt) for f in (317, 731, 1187, 1663)) * np.exp(-mt*7)
        ms = s + int(0.375*SR); x[ms:ms+len(mt)] += 0.25*metal
save("dust_rhythm.wav", x[:int(4.0 * SR)])

# 4 low drone: detuned saws + sub, slow filter sweep, stereo
dur = 8.0; tt = t(dur); chans = []
for c in range(2):
    sig = np.zeros_like(tt)
    for k, det in enumerate((-0.11, 0.0, 0.13)):
        f = 55.0 * (1 + det/100*(c*2-1)) * (1 + 0.003*k)
        ph = (f * tt + 0.3*k) % 1.0; sig += 2*ph - 1
    sig += 0.8 * np.sin(2*np.pi*27.5*tt)
    cut = 300 + 900*(0.5 + 0.5*np.sin(2*np.pi*0.08*tt + c))
    y = np.zeros_like(sig); z = 0.0
    a = 1 - np.exp(-2*np.pi*cut/SR)
    for i in range(len(sig)): z += a[i]*(sig[i]-z); y[i] = z
    chans.append(y)
save("low_drone.wav", np.stack(chans, 1))

# 5 wire plucks: Karplus-Strong with bending, mono
x = np.zeros(int(5.0 * SR))
for i, f in enumerate([196.0, 293.66, 246.94, 392.0, 220.0, 329.63]):
    n = int(SR / f); buf = rng.uniform(-1, 1, n); ks = np.zeros(int(2.0*SR))
    for k in range(len(ks)):
        j = k % n; nxt = (k+1) % n
        ks[k] = buf[j]; buf[j] = 0.4985*(buf[j] + buf[nxt])
    s = int(i * 0.75 * SR); x[s:s+len(ks)] += ks[:len(x)-s]
save("wire_plucks.wav", x)

# ---------------------------------------------------------------- ambient sources
def midi(n): return 440.0 * 2 ** ((n - 69) / 12)

def env_ar(tt, a, r, dur):
    e = np.minimum(1.0, tt / max(a, 1e-3))
    return e * np.clip((dur - tt) / max(r, 1e-3), 0, 1)

def stereo_place(x, pan):
    return np.stack([x * np.sqrt(1 - pan), x * np.sqrt(pan)], 1)

def additive(f, tt, amps, vib=0.0, vib_rate=5.0, phase_seed=0, detune=1.0):
    """sum of harmonics with optional vibrato; amps[k] for harmonic k+1 (array or callable(fk))"""
    r = np.random.default_rng(phase_seed)
    lfo = 1 + vib * np.sin(2*np.pi*vib_rate*tt + r.uniform(0, 6.28))
    ph0 = np.cumsum(lfo) / SR * f * detune
    y = np.zeros_like(tt)
    for k, a in enumerate(amps):
        fk = f * (k + 1)
        if fk > SR * 0.45: break
        if callable(a): a = a(fk)
        y += a * np.sin(2*np.pi*(k + 1)*ph0 + r.uniform(0, 6.28))
    return y

# felt piano: inharmonic partials, soft hammer, notes of an Fmaj9 voicing
dur = 9.0; tt = t(dur); mix = np.zeros((len(tt), 2))
for i, n in enumerate([53, 60, 64, 67, 69, 72, 76, 79]):
    f = midi(n); s0 = int(i * 0.55 * SR); nt = tt[:len(tt) - s0]; y = np.zeros_like(nt)
    for k in range(1, 14):
        fk = k * f * np.sqrt(1 + 0.0004 * k * k)
        if fk > 9000: break
        y += (1 / k ** 1.4) * np.exp(-nt * (0.6 + 0.35 * k)) * np.sin(2*np.pi*fk*nt + k)
    y *= np.minimum(1, nt / 0.006)
    mix[s0:] += stereo_place(y, 0.25 + 0.5 * (n - 53) / 26)
save("felt_piano.wav", mix)

# warm pad: detuned additive saws, spectrum opening and closing, Am9
dur = 10.0; tt = t(dur); mix = np.zeros((len(tt), 2))
kc = 3 + 9 * np.sin(np.pi * tt / dur) ** 2
for c in range(2):
    for v, n in enumerate([45, 52, 57, 60, 64, 67, 71]):
        for d in (-7, 0, 7):
            f = midi(n) * 2 ** ((d + (c * 2 - 1) * 2) / 1200)
            ph = np.random.default_rng(v * 100 + d + 50 + c).uniform(0, 1)
            y = np.zeros_like(tt)
            for k in range(1, 28):
                if f * k > 8000: break
                y += np.exp(-k / kc) / k * np.sin(2*np.pi*f*k*tt + ph * k)
            mix[:, c] += y
mix *= env_ar(tt, 2.5, 2.5, dur)[:, None]
save("warm_pad.wav", mix)

# bowed glass: modal partials with beating pairs and slow bow attack
dur = 9.0; tt = t(dur); mix = np.zeros((len(tt), 2))
for j, (n, start) in enumerate([(74, 0.0), (81, 2.2), (79, 4.6)]):
    f = midi(n); y = np.zeros_like(tt)
    for ratio, a in zip([1, 2.32, 4.25, 6.63], [1, 0.45, 0.22, 0.1]):
        for beat in (-0.35, 0.35):
            y += a * np.sin(2*np.pi*(f*ratio + beat*ratio)*tt + j)
    e = np.clip((tt - start) / 1.6, 0, 1) ** 2 * np.exp(-np.maximum(tt - start - 1.6, 0) * 0.35)
    mix += stereo_place(y * e * (1 + 0.15*np.sin(2*np.pi*4.2*tt)), [0.3, 0.7, 0.5][j])
save("bowed_glass.wav", mix)

# kalimba drops: modal tines on a pentatonic figure, ping-ponged
dur = 7.0; tt = t(dur); mix = np.zeros((len(tt), 2))
for i, n in enumerate([62, 69, 74, 72, 67, 74, 76, 69, 62, 79, 74]):
    f = midi(n); s0 = int(i * 0.5 * SR + rng.uniform(0, 0.04) * SR)
    nt = tt[:len(tt) - s0]
    y = np.sin(2*np.pi*f*nt) * np.exp(-nt * 2.2) + 0.3 * np.sin(2*np.pi*f*5.4*nt) * np.exp(-nt * 14) + 0.12 * np.sin(2*np.pi*f*8.9*nt) * np.exp(-nt * 30)
    y *= np.minimum(1, nt / 0.002)
    mix[s0:] += stereo_place(y, 0.2 if i % 2 else 0.8)
save("kalimba_drops.wav", mix)

# vox choir: formant-shaped harmonic voices (ah -> oo), slow vibrato, D major
dur = 9.0; tt = t(dur); mix = np.zeros((len(tt), 2))
def formant(fk, morph):
    a = [(700, 1220, 2600), (450, 800, 2830)]
    fs = [a[0][i] * (1 - morph) + a[1][i] * morph for i in range(3)]
    return sum(g / (1 + ((fk - F) / (F * 0.12)) ** 2) for F, g in zip(fs, (1.0, 0.5, 0.25)))
for c in range(2):
    for v, n in enumerate([50, 54, 57, 62, 66]):
        for d in (-6, 6):
            f = midi(n) * 2 ** ((d + c * 3) / 1200)
            y = np.zeros_like(tt)
            seg = len(tt) // 6
            for k in range(1, 40):
                if f * k > 6000: break
                amp_t = np.interp(tt, [0, dur], [formant(f * k, 0), formant(f * k, 1)]) / k ** 0.6
                y += amp_t * np.sin(2*np.pi*k*np.cumsum(1 + 0.004*np.sin(2*np.pi*(4.6 + v*0.3)*tt + v + c))/SR*f)
            mix[:, c] += y
mix *= env_ar(tt, 1.8, 2.0, dur)[:, None]
save("vox_choir.wav", mix)

# rain field: soft pink noise bed, droplets, distant rumble
from scipy.signal import lfilter
dur = 10.0; tt = t(dur); mix = np.zeros((len(tt), 2))
for c in range(2):
    w = rng.standard_normal(len(tt))
    pink = lfilter([0.049922035, -0.095993537, 0.050612699, -0.004408786], [1, -2.494956002, 2.017265875, -0.522189400], w)
    bed = lfilter([0.15], [1, -0.85], pink) * 0.6
    rumble = lfilter([0.002], [1, -0.998], rng.standard_normal(len(tt))) * (0.6 + 0.4*np.sin(2*np.pi*0.07*tt + c))
    drops = np.zeros_like(tt)
    for _ in range(140):
        s0 = rng.integers(0, len(tt) - 3000); f = rng.uniform(1800, 5200); dt = t(0.06)
        drops[s0:s0+len(dt)] += rng.uniform(0.1, 0.5) * np.sin(2*np.pi*f*(1 + 0.6*dt)*dt) * np.exp(-dt * 90)
    mix[:, c] = bed + rumble * 3 + drops
save("rain_field.wav", mix)

# music box: bright tines, slow lullaby in C
dur = 8.0; tt = t(dur); mix = np.zeros((len(tt), 2))
melody = [84, 88, 91, 86, 84, 79, 81, 84, 88, 86, 83, 84]
for i, n in enumerate(melody):
    f = midi(n); s0 = int(i * 0.6 * SR); nt = tt[:len(tt) - s0]
    y = np.sin(2*np.pi*f*nt) * np.exp(-nt * 1.6) + 0.35 * np.sin(2*np.pi*f*2.756*nt) * np.exp(-nt * 6) + 0.15 * np.sin(2*np.pi*f*5.404*nt) * np.exp(-nt * 12)
    y *= np.minimum(1, nt / 0.001)
    mix[s0:] += stereo_place(y, 0.35 + 0.3 * ((i * 7) % 5) / 4)
save("music_box.wav", mix)

# string swell: ensemble of bowed saws with vibrato, Cmaj7 spread
dur = 10.0; tt = t(dur); mix = np.zeros((len(tt), 2))
for c in range(2):
    for v, n in enumerate([48, 55, 64, 71, 74]):
        for d in (-5, 4):
            f = midi(n)
            y = additive(f, tt, [np.exp(-k / 9) / (k + 1) for k in range(26)], vib=0.004, vib_rate=5.2 + 0.3*v, phase_seed=v*7+c*3+d+10, detune=2 ** ((d + c * 2) / 1200))
            mix[:, c] += y
mix *= (np.clip(tt / 3.0, 0, 1) ** 1.5 * env_ar(tt, 0.01, 2.5, dur))[:, None]
mix[:, 0] = lfilter([0.35], [1, -0.65], mix[:, 0]); mix[:, 1] = lfilter([0.35], [1, -0.65], mix[:, 1])
save("string_swell.wav", mix)

# sub bloom: deep sines and soft triangle overtones that swell and fade, D
dur = 8.0; tt = t(dur); mix = np.zeros((len(tt), 2))
for c in range(2):
    y = np.zeros_like(tt)
    for n, a in [(26, 1.0), (38, 0.7), (45, 0.35), (50, 0.25), (57, 0.12)]:
        f = midi(n) * (1 + (c * 2 - 1) * (0.0015 if n >= 45 else 0.0))   # no beating in the sub range
        y += a * additive(f, tt, [1, 0, 1/9, 0, 1/25], phase_seed=n)   # same phase both sides: bass stays mono-safe
    mix[:, c] = y * (0.5 + 0.5 * np.sin(np.pi * tt / dur) ** 2) * env_ar(tt, 1.5, 1.5, dur)
save("sub_bloom.wav", mix)
print("factory samples written to", out)
