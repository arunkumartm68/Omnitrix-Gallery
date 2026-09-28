"""
Alien Museum - makes all of the museum's sounds from scratch (Python 3 + numpy + scipy).

    python Scripts/make_museum_sounds.py [output folder]        (default: SourceArt/Sounds)

Nothing is recorded, downloaded or copied from the show: every sound is synthesised here from
oscillators, noise, filters and envelopes - the aliens' voices (a glottal source shaped by formants,
roughened into growls, snarls, whispers...), footsteps, the signature moves, the display cases, the
glass, the Omnitrix and the holographic panel. Random seeds are fixed, so every run writes the same files.

Writes <name>.wav (mono, 16-bit, 44.1 kHz) and sounds.json for Scripts/import_museum_sounds.py:
  voices   V_<Alien>_<Kind>_<n>   an alien's Calls / Alerts / Efforts / Held (UAlienDataAsset::Sounds)
  library  SFX_<Name>_<n>         the museum's shared sounds (UMuseumSoundLibrary), name e.g. Move.Pounce.Land
Loops (a case's hum and home-world ambience, Stinkfly's wings, Cannonbolt's roll) loop seamlessly.

Personal fan project - keep the build private.
"""
import json
import os
import sys
import zlib

import numpy as np
from scipy import signal
from scipy.io import wavfile

SR = 44100
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Loudness each kind of sound is made at (RMS of its loudest 60 ms); the game's volumes mix from there.
LOUDNESS = {"voice": 0.20, "step": 0.16, "move": 0.22, "alien": 0.16, "case": 0.16, "loop": 0.12,
            "ui": 0.13, "glass": 0.20, "omnitrix": 0.17}


# ---------------------------------------------------------------------------------------------
# Building blocks
# ---------------------------------------------------------------------------------------------

def samples(dur):
    return max(1, int(round(dur * SR)))


def timeline(dur):
    return np.arange(samples(dur)) / SR


def curve(points, dur, log=False):
    """Per-sample values through (time, value) points; log = interpolate in log (frequencies)."""
    t = timeline(dur)
    ts = [p[0] for p in points]
    vs = [p[1] for p in points]
    if log:
        return np.exp(np.interp(t, ts, np.log(vs)))
    return np.interp(t, ts, vs)


def smooth_noise(n, rate, rng):
    """A random wobble in -1..1 that changes about `rate` times a second (jitter, drift, gusts)."""
    k = max(2, int(n / SR * rate) + 2)
    points = rng.uniform(-1.0, 1.0, k)
    x = np.linspace(0.0, k - 1.001, n)
    i = np.floor(x).astype(int)
    f = (1.0 - np.cos((x - i) * np.pi)) * 0.5
    return points[i] * (1.0 - f) + points[i + 1] * f


def noise(n, rng, color="white"):
    w = rng.standard_normal(n)
    if color == "pink":
        b = [0.049922035, -0.095993537, 0.050612699, -0.004408786]
        a = [1.0, -2.494956002, 2.017265875, -0.522189400]
        w = signal.lfilter(b, a, w)
    elif color == "brown":
        w = signal.lfilter([1.0], [1.0, -0.995], w)
        w -= np.mean(w)
    return w / (np.std(w) + 1e-12)


def butter(x, kind, freq, order=2):
    if np.isscalar(freq):
        freq = min(freq, SR * 0.45)
    else:
        freq = [max(10.0, freq[0]), min(freq[1], SR * 0.45)]
    return signal.sosfilt(signal.butter(order, freq, btype=kind, fs=SR, output="sos"), x)


def lowpass(x, freq, order=2):
    return butter(x, "lowpass", freq, order)


def highpass(x, freq, order=2):
    return butter(x, "highpass", freq, order)


def bandpass(x, lo, hi, order=2):
    return butter(x, "bandpass", [lo, hi], order)


def resonator(x, freq, bandwidth):
    """A resonance at freq Hz, bandwidth Hz wide, unity gain at its peak."""
    freq = min(freq, SR * 0.45)
    b, a = signal.iirpeak(freq, max(0.3, freq / bandwidth), fs=SR)
    return signal.lfilter(b, a, x)


def swept_resonator(x, freqs, bandwidth, block=96):
    """A resonance whose frequency follows freqs (Hz per sample), filtered in short blocks."""
    y = np.zeros_like(x)
    state = np.zeros(2)
    for start in range(0, len(x), block):
        end = min(len(x), start + block)
        f = float(np.clip(freqs[min(len(freqs) - 1, (start + end) // 2)], 25.0, SR * 0.45))
        b, a = signal.iirpeak(f, max(0.3, f / bandwidth), fs=SR)
        y[start:end], state = signal.lfilter(b, a, x[start:end], zi=state)
    return y


def normalized(x):
    return x / (np.max(np.abs(x)) + 1e-12)


def saturate(x, drive):
    return np.tanh(drive * normalized(x)) / np.tanh(drive)


def crush(x, bits, rate):
    """Digital grit: sample-and-hold at `rate` Hz, quantised to `bits`."""
    hold = np.minimum((np.floor(np.arange(len(x)) * rate / SR) * SR / rate).astype(int), len(x) - 1)
    steps = 2 ** (bits - 1)
    return np.round(normalized(x)[hold] * steps) / steps


def pad(x, dur):
    return np.concatenate([x, np.zeros(samples(dur))])


def place(total, parts):
    """Mixes [(start seconds, sound)] into one buffer long enough for all of them."""
    end = max(samples(start) + len(part) for start, part in parts)
    out = np.zeros(max(samples(total), end))
    for start, part in parts:
        s = samples(start)
        out[s:s + len(part)] += fade(part, 0.0005, 0.008)
    return out


def mix(*parts):
    """Sums sounds of different lengths (the shorter ones end in silence)."""
    out = np.zeros(max(len(p) for p in parts))
    for p in parts:
        out[:len(p)] += fade(p, 0.0005, 0.008)
    return out


def fade(x, fade_in=0.003, fade_out=0.02):
    x = x.copy()
    a = min(len(x), samples(fade_in))
    b = min(len(x), samples(fade_out))
    x[:a] *= np.linspace(0.0, 1.0, a)
    x[len(x) - b:] *= np.linspace(1.0, 0.0, b)
    return x


def env(dur, points):
    """Amplitude envelope through (time, level) points."""
    return curve(points, dur)


def ar(dur, attack, release, hold_level=1.0):
    """Rises over `attack`, holds, falls over `release` (seconds)."""
    return curve([(0.0, 0.0), (attack, hold_level), (max(attack, dur - release), hold_level), (dur, 0.0)], dur)


def decay(dur, tau, attack=0.002):
    t = timeline(dur)
    return np.minimum(1.0, t / max(attack, 1e-6)) * np.exp(-t / tau)


def echo(x, delay, feedback, repeats, darken=3500.0):
    """Repeats that fade and dull (Echo Echo's voice, a big space)."""
    d = samples(delay)
    out = np.zeros(len(x) + d * repeats)
    out[:len(x)] += x
    tap = x
    for r in range(1, repeats + 1):
        tap = lowpass(tap, darken) * feedback
        out[d * r:d * r + len(tap)] += tap
    return out


def reverb(x, rng, rt60=0.8, wet=0.25, bright=6500.0, predelay=0.012):
    """A small synthetic room: decaying noise as the impulse response."""
    n = samples(rt60 * 1.1)
    t = np.arange(n) / SR
    ir = rng.standard_normal(n) * np.exp(-6.91 * t / rt60)
    ir = lowpass(ir, bright)
    ir[:samples(predelay)] = 0.0
    ir /= np.sqrt(np.sum(ir ** 2)) + 1e-12
    x = fade(x, 0.0005, 0.01)
    tail = signal.fftconvolve(x, ir)
    dry = np.concatenate([x, np.zeros(len(tail) - len(x))])
    return dry * (1.0 - wet) + tail * wet * (np.max(np.abs(x)) / (np.max(np.abs(tail)) + 1e-12))


def make_loop(x, overlap):
    """Seamless loop: the last `overlap` seconds cross-fade into the start."""
    c = samples(overlap)
    body = x[:-c].copy()
    ramp = np.linspace(0.0, 1.0, c)
    body[:c] = body[:c] * ramp + x[-c:] * (1.0 - ramp)
    return body


def finish(x, kind, loop=False, hp=35.0):
    """High-passed (no rumble below hearing), set to the kind's loudness, peaks limited softly."""
    # A loop is filtered as if it had already been playing, so its start joins its end without a click.
    x = highpass(np.tile(x, 2), hp)[len(x):] if loop else highpass(x, hp)
    w = min(len(x), samples(0.06))
    power = np.convolve(x ** 2, np.ones(w) / w, mode="same")
    # Its kind's loudness, unless that would push its peaks past 0.93 (peaky sounds end up a little quieter
    # rather than distorted; the game's per-sound volume evens that out).
    x = x * min(LOUDNESS[kind] / (np.sqrt(np.max(power)) + 1e-12), 0.93 / (np.max(np.abs(x)) + 1e-12))
    return x if loop else fade(x, 0.002, 0.012)


# ---------------------------------------------------------------------------------------------
# Voices: a glottal source shaped by formants
# ---------------------------------------------------------------------------------------------

# Formants (Hz, bandwidth) of vowels for a human-sized vocal tract; creatures scale them.
VOWELS = {
    "a": [(730, 90), (1090, 110), (2440, 160), (3400, 250)],
    "e": [(530, 70), (1840, 100), (2480, 160), (3500, 250)],
    "i": [(270, 60), (2290, 100), (3010, 170), (3700, 250)],
    "o": [(570, 80), (840, 90), (2410, 160), (3400, 250)],
    "u": [(300, 60), (870, 90), (2240, 160), (3300, 250)],
    "uh": [(520, 80), (1190, 100), (2390, 160), (3400, 250)],
    "m": [(250, 60), (1100, 300), (2300, 250), (3300, 300)],
}
GAINS = (0.0, -5.0, -11.0, -17.0)


def vowel(name, scale=1.0, width=1.0, gains=GAINS):
    return [(f * scale, bw * width * max(1.0, scale), g) for (f, bw), g in zip(VOWELS[name], gains)]


def glide(vowel_a, vowel_b, dur, scale=1.0, width=1.0):
    """Formants moving from one vowel to another over the sound."""
    out = []
    for (fa, bwa), (fb, _), g in zip(VOWELS[vowel_a], VOWELS[vowel_b], GAINS):
        out.append(([(0.0, fa * scale), (dur, fb * scale)], bwa * width * max(1.0, scale), g))
    return out


def voiced(dur, f0, rng, jitter=0.01, shimmer=0.06, tilt=1.0, rough=0.0, rough_rate=30.0, sub=0.0,
           vibrato=(0.0, 0.0), max_harmonics=90):
    """Harmonics of a pitch contour (Hz, number or per-sample array), falling off by `tilt`, with jitter,
    shimmer, an optional growl rattle (irregular pulses at rough_rate) and subharmonics."""
    n = samples(dur)
    f = np.full(n, float(f0)) if np.isscalar(f0) else f0[:n].astype(float)
    f = f * (1.0 + jitter * smooth_noise(n, 22.0, rng))
    if vibrato[0] > 0.0:
        f = f * (1.0 + vibrato[1] * np.sin(2 * np.pi * vibrato[0] * np.arange(n) / SR + rng.uniform(0, 6.28)))
    phase = 2 * np.pi * np.cumsum(f) / SR
    top = SR * 0.45
    count = int(min(max_harmonics, top / max(1.0, float(np.max(f)))))
    src = np.zeros(n)
    for k in range(1, count + 1):
        src += np.clip((top - k * f) / (0.04 * SR), 0.0, 1.0) * np.sin(k * phase) / k ** tilt
    if sub > 0.0:
        for k in range(1, count, 2):
            src += sub * np.clip((top - k * f / 2) / (0.04 * SR), 0.0, 1.0) * np.sin(k * phase / 2) / k ** tilt
    amp = 1.0 + shimmer * smooth_noise(n, 28.0, rng)
    if rough > 0.0:
        rate = rough_rate * (1.0 + 0.3 * smooth_noise(n, 7.0, rng))
        pulses = (0.5 + 0.5 * np.cos(2 * np.pi * np.cumsum(rate) / SR)) ** 3
        pulses *= 1.0 + 0.5 * smooth_noise(n, rough_rate * 0.7, rng)
        amp *= (1.0 - rough) + rough * 1.7 * pulses
    return normalized(src * amp)


def shape(source, formants, dur):
    out = np.zeros_like(source)
    for freq, width, gain in formants:
        g = 10.0 ** (gain / 20.0)
        if isinstance(freq, (list, tuple)):
            out += g * swept_resonator(source, curve(freq, dur, log=True), width)
        else:
            out += g * resonator(source, freq, width)
    return out


def creature(dur, f0, rng, formants, breath=0.1, breath_band=(700.0, 7000.0), drive=0.0, envelope=None, top=None, **kw):
    """A creature's voice: voiced source + breath noise through formants, optionally overdriven."""
    n = samples(dur)
    pitch = f0 if np.isscalar(f0) else f0
    src = voiced(dur, pitch, rng, **kw)
    if breath > 0.0:
        air = bandpass(noise(n, rng), breath_band[0], breath_band[1])
        src = src + breath * 1.2 * normalized(air) * (1.0 + 0.4 * smooth_noise(n, 15.0, rng))
    y = shape(src, formants, dur) + 0.08 * src
    if drive > 0.0:
        y = saturate(y, drive)
    if top:
        y = lowpass(y, top, 4)
    if envelope is not None:
        y = y * envelope
    return normalized(y)


def pitch(points, dur):
    return curve(points, dur, log=True)


def syllables(rng, count, make, gap=(0.02, 0.06)):
    """Several short sounds one after another (chatter, laughs, chirps)."""
    parts = []
    t = 0.0
    for i in range(count):
        part = make(i)
        parts.append((t, part))
        t += len(part) / SR + rng.uniform(*gap)
    return place(t, parts)


# ---------------------------------------------------------------------------------------------
# Other synthesis
# ---------------------------------------------------------------------------------------------

def modal(dur, modes, rng, strike=0.0015, strike_band=(2000.0, 12000.0), spread=0.0):
    """A struck object: decaying partials [(Hz, amplitude, decay s)] plus the strike's click."""
    t = timeline(dur)
    y = np.zeros_like(t)
    for freq, amp, tau in modes:
        f = freq * (1.0 + rng.uniform(-spread, spread))
        y += amp * np.sin(2 * np.pi * f * t + rng.uniform(0, 6.28)) * np.exp(-t / tau)
    click = bandpass(noise(samples(strike * 4), rng), *strike_band) * decay(strike * 4, strike)
    y[:len(click)] += 0.6 * normalized(click) * np.max(np.abs(y))
    return normalized(y)


def thud(dur, f_start, f_end, tau, rng, body_noise=0.35, noise_cut=1200.0):
    """A heavy impact: a sine that drops in pitch, plus a low noise burst."""
    t = timeline(dur)
    f = f_end + (f_start - f_end) * np.exp(-t / max(0.005, tau * 0.35))
    y = np.sin(2 * np.pi * np.cumsum(f) / SR) * decay(dur, tau, 0.001)
    y += body_noise * lowpass(noise(len(t), rng), noise_cut) * decay(dur, tau * 0.35, 0.001)
    return normalized(y)


def whoosh(dur, f_from, f_to, rng, width=0.9, points=None, color="pink"):
    """Air rushing past: noise through a band sweeping from f_from to f_to (Hz)."""
    x = noise(samples(dur), rng, color)
    y = swept_resonator(x, curve([(0.0, f_from), (dur, f_to)], dur, log=True), max(80.0, (f_from + f_to) * 0.5 * width))
    shape_points = points or [(0.0, 0.0), (dur * 0.45, 1.0), (dur, 0.0)]
    return normalized(y * env(dur, shape_points))


def tone(dur, freqs, harmonics=((1, 1.0),), envelope=None):
    """A pitched tone (freqs: Hz or per-sample array) with the given (multiple, level) harmonics."""
    n = samples(dur)
    f = np.full(n, float(freqs)) if np.isscalar(freqs) else freqs[:n]
    phase = 2 * np.pi * np.cumsum(f) / SR
    y = sum(level * np.sin(mult * phase) for mult, level in harmonics)
    return y * (envelope if envelope is not None else 1.0)


def fm(dur, carrier, ratio, index, envelope=None):
    """Frequency modulation: carrier Hz (or array), modulator at carrier*ratio, index (or array)."""
    n = samples(dur)
    c = np.full(n, float(carrier)) if np.isscalar(carrier) else carrier[:n]
    i = np.full(n, float(index)) if np.isscalar(index) else index[:n]
    cp = 2 * np.pi * np.cumsum(c) / SR
    mp = 2 * np.pi * np.cumsum(c * ratio) / SR
    y = np.sin(cp + i * np.sin(mp))
    return y * (envelope if envelope is not None else 1.0)


def clicks(dur, rate, rng, band=(1500.0, 6000.0), click_len=0.004, jitter=0.35, resonance=None):
    """A train of small clicks (chitter, creaks, patter)."""
    out = np.zeros(samples(dur))
    t = 0.0
    while t < dur:
        c = bandpass(noise(samples(click_len), rng), *band) * decay(click_len, click_len * 0.3, 0.0003)
        s = samples(t)
        out[s:s + len(c)] += c[:len(out) - s] * rng.uniform(0.5, 1.0)
        t += (1.0 / rate) * (1.0 + rng.uniform(-jitter, jitter))
    if resonance:
        out = sum(resonator(out, f, bw) for f, bw in resonance)
    return normalized(out)


def bubbles(dur, rate, rng, low=350.0, high=1300.0):
    """Bubbles: short sine chirps that rise as they pop."""
    out = np.zeros(samples(dur))
    t = rng.uniform(0.0, 1.0 / rate)
    while t < dur:
        length = rng.uniform(0.015, 0.045)
        f0 = rng.uniform(low, high)
        b = tone(length, curve([(0.0, f0), (length, f0 * rng.uniform(1.4, 2.2))], length, log=True),
                 envelope=decay(length, length * 0.35, 0.002))
        s = samples(t)
        out[s:s + len(b)] += b[:len(out) - s] * rng.uniform(0.3, 1.0)
        t += rng.exponential(1.0 / rate)
    return normalized(out)


def crackle(dur, density, rng, band=(1500.0, 9000.0)):
    """Random tiny pops (fire, leaves, grit)."""
    n = samples(dur)
    x = np.zeros(n)
    count = int(dur * density)
    pos = rng.integers(0, n, count)
    x[pos] = rng.uniform(-1.0, 1.0, count) * rng.uniform(0.2, 1.0, count) ** 2
    return normalized(bandpass(x, *band))


# ---------------------------------------------------------------------------------------------
# The aliens' voices
# ---------------------------------------------------------------------------------------------

def growl(rng, dur, f_lo, f_hi, scale, rough=0.7, rate=28.0, breath=0.2, drive=1.3):
    n = samples(dur)
    f0 = f_lo + (f_hi - f_lo) * (0.5 + 0.5 * smooth_noise(n, 1.5, rng))
    formants = vowel("uh", scale, 2.2)
    y = creature(dur, f0, rng, formants, breath=breath, breath_band=(300.0, 3500.0), drive=drive, jitter=0.05, shimmer=0.15,
                 tilt=0.9, rough=rough, rough_rate=rate, sub=0.45, top=3200.0, envelope=ar(dur, 0.18, 0.35))
    swell = 1.0 + 0.3 * smooth_noise(n, 2.0, rng)
    return normalized(y * swell)


def snarl(rng, dur, f_from, f_peak, scale, teeth=0.5):
    f0 = pitch([(0.0, f_from), (dur * 0.35, f_peak), (dur, f_from * 0.9)], dur)
    y = creature(dur, f0, rng, vowel("a", scale, 2.0), breath=0.3, breath_band=(900.0, 5000.0), drive=1.8,
                 jitter=0.06, shimmer=0.2, tilt=0.8, rough=0.5, rough_rate=42.0, sub=0.2, top=4500.0, envelope=ar(dur, 0.05, 0.25))
    hiss = bandpass(noise(samples(dur), rng), 3500, 8000) * ar(dur, 0.03, 0.3)
    return y + teeth * 0.25 * normalized(hiss) * np.max(np.abs(y))


def bark(rng, f_from, f_to, scale):
    dur = rng.uniform(0.22, 0.3)
    y = creature(dur, pitch([(0.0, f_from), (dur, f_to)], dur), rng, vowel("o", scale, 1.8), breath=0.3, drive=2.0,
                 jitter=0.04, rough=0.3, rough_rate=50.0, tilt=0.9, envelope=decay(dur, dur * 0.35, 0.006))
    return pad(y, 0.05)


def sniff_burst(rng, dur, rise=1.4):
    n = samples(dur)
    x = bandpass(noise(n, rng), 900, 6500)
    center = curve([(0.0, 1900.0), (dur, 1900.0 * rise)], dur, log=True)
    y = 0.5 * x + 1.2 * swept_resonator(x, center, 900.0)
    return normalized(y * env(dur, [(0.0, 0.0), (0.012, 1.0), (dur * 0.6, 0.8), (dur, 0.0)]))


def sniffs(rng, count=None):
    count = count or int(rng.integers(3, 6))
    return syllables(rng, count, lambda i: sniff_burst(rng, rng.uniform(0.055, 0.085) * (1.6 if i == count - 1 else 1.0)),
                     gap=(0.04, 0.07))


def whine(rng, f_lo, f_hi, scale):
    dur = rng.uniform(0.55, 0.8)
    f0 = pitch([(0.0, f_lo), (dur * 0.45, f_hi), (dur, f_lo * 0.95)], dur)
    return creature(dur, f0, rng, vowel("i", scale, 1.6), breath=0.12, jitter=0.01, tilt=1.8,
                    vibrato=(6.5, 0.025), envelope=ar(dur, 0.06, 0.2))


def huff(rng, scale):
    """A short grumpy exhale through the nose."""
    dur = rng.uniform(0.3, 0.45)
    air = bandpass(noise(samples(dur), rng), 300, 3000)
    air = normalized(shape(air, vowel("uh", scale, 2.0), dur) * decay(dur, dur * 0.4, 0.02))
    grunt = creature(dur, 75.0, rng, vowel("uh", scale, 2.2), breath=0.4, rough=0.6, rough_rate=30.0,
                     sub=0.4, drive=1.4, envelope=decay(dur, dur * 0.3, 0.01))
    return normalized(air + 0.6 * grunt)


def howl(rng, dur, scale):
    """A long rising-and-falling howl, nearly a pure tone, with a slow vibrato."""
    peak = rng.uniform(500.0, 545.0)
    f0 = pitch([(0.0, 290.0), (0.28, peak * 0.9), (0.55, peak), (dur - 0.45, peak * 1.03), (dur - 0.15, peak * 0.86), (dur, 380.0)], dur)
    formants = [([(0.0, 360.0), (0.5, 640.0), (dur - 0.3, 620.0), (dur, 420.0)], 160.0, 0.0),
                ([(0.0, 820.0), (0.5, 1100.0), (dur - 0.3, 1080.0), (dur, 880.0)], 200.0, -7.0),
                (2600.0 * scale, 320.0, -16.0)]
    return creature(dur, f0, rng, formants, breath=0.07, jitter=0.004, shimmer=0.04, tilt=1.7,
                    vibrato=(5.3, 0.012), envelope=env(dur, [(0.0, 0.0), (0.12, 0.8), (0.5, 1.0), (dur - 0.35, 0.95), (dur, 0.0)]))


def whisper(rng, dur_hint, scale=1.0):
    """Unvoiced syllables: breath through changing vowel shapes with s / sh / h consonants."""
    names = ["a", "e", "i", "o", "u", "uh"]

    def syllable(i):
        d = rng.uniform(0.09, 0.22)
        air = bandpass(noise(samples(d), rng, "pink"), 400, 9000)
        v = vowel(names[int(rng.integers(len(names)))], scale, 2.4)
        y = shape(air, v, d) * ar(d, 0.02, 0.06)
        kind = rng.uniform()
        if kind < 0.35:
            c = bandpass(noise(samples(0.06), rng), 4800, 9500) * ar(0.06, 0.01, 0.03)
            y = np.concatenate([0.8 * normalized(c) * np.max(np.abs(y)), y])
        elif kind < 0.6:
            c = bandpass(noise(samples(0.07), rng), 2300, 6000) * ar(0.07, 0.015, 0.03)
            y = np.concatenate([0.7 * normalized(c) * np.max(np.abs(y)), y])
        return y

    count = max(3, int(dur_hint / 0.2))
    y = syllables(rng, count, syllable, gap=(0.015, 0.07))
    breathy = lowpass(np.concatenate([np.zeros(samples(0.04)), y]), 2500)[:len(y)]
    return normalized(y + 0.35 * breathy)


def laugh(rng, count, f0, vowel_name, scale, rate=5.5, rough=0.5, breath=0.5, fall=0.9):
    def pulse(i):
        d = 1.0 / rate * 0.62
        f = f0 * (fall ** i) * rng.uniform(0.97, 1.03)
        return creature(d, f, rng, vowel(vowel_name, scale, 1.8), breath=breath, rough=rough, rough_rate=38.0,
                        jitter=0.03, tilt=1.1, envelope=decay(d, d * 0.45, 0.012))
    return syllables(rng, count, pulse, gap=(0.035, 0.06))


def chirp(rng, f_from, f_to, dur, trill=0.0, tilt=2.4, scale=1.6):
    f0 = pitch([(0.0, f_from), (dur, f_to)], dur)
    return creature(dur, f0, rng, [(2600.0, 1600.0, 0.0), (4200.0, 1800.0, -8.0)], breath=0.06, tilt=tilt,
                    jitter=0.006, vibrato=(38.0, trill), envelope=ar(dur, 0.008, dur * 0.4))


def hum(rng, dur, f0, scale, vibrato=(4.0, 0.01)):
    """A closed-mouth hum ('mmm')."""
    return creature(dur, f0, rng, vowel("m", scale, 1.3, gains=(0.0, -16.0, -14.0, -22.0)), breath=0.04, tilt=1.6,
                    jitter=0.01, vibrato=vibrato, top=1800.0 * scale, envelope=ar(dur, 0.05, 0.18))


def gasp(rng, scale):
    d = rng.uniform(0.28, 0.4)
    air = bandpass(noise(samples(d), rng), 500, 7000)
    return normalized(shape(air, vowel("a", scale, 2.0), d) * env(d, [(0.0, 0.0), (0.05, 1.0), (d * 0.7, 0.7), (d, 0.0)]))


def crystal_ring(rng, dur, base):
    ratios = [1.0, 2.76, 5.40, 8.93]
    return modal(dur, [(base * r, 1.0 / (1 + i), dur * (0.55 / (1 + 0.6 * i))) for i, r in enumerate(ratios)], rng, spread=0.01)


def electronic_chirp(rng, f_from, f_to, dur, index=2.0):
    c = curve([(0.0, f_from), (dur, f_to)], dur, log=True)
    y = fm(dur, c, 0.5, index, ar(dur, 0.006, dur * 0.5))
    y *= 0.6 + 0.4 * np.sin(2 * np.pi * 70.0 * timeline(dur))
    return y


def bleeps(rng, count):
    scale_notes = [587.3, 659.3, 784.0, 880.0, 987.8, 1174.7, 1318.5]

    def note(i):
        d = rng.uniform(0.05, 0.09)
        f = scale_notes[int(rng.integers(len(scale_notes)))]
        if rng.uniform() < 0.25:
            return fm(d, f, 0.05, 3.0, ar(d, 0.003, 0.02))
        return tone(d, f, ((1, 1.0), (3, 0.3), (5, 0.12)), ar(d, 0.003, 0.02))
    return crush(syllables(rng, count, note, gap=(0.015, 0.045)), 6, 16000.0)


def creak(rng, dur, rate_from, rate_to, woods=((380.0, 90.0), (820.0, 140.0), (1650.0, 260.0))):
    """Wood (or a vine) straining: a slowly varying train of clicks through wooden resonances."""
    n = samples(dur)
    rate = curve([(0.0, rate_from), (dur * 0.5, (rate_from + rate_to) * 0.7), (dur, rate_to)], dur)
    rate *= 1.0 + 0.25 * smooth_noise(n, 6.0, rng)
    phase = np.cumsum(rate) / SR
    ticks = np.zeros(n)
    idx = np.nonzero(np.diff(np.floor(phase)) > 0)[0]
    ticks[idx] = rng.uniform(0.4, 1.0, len(idx))
    y = lowpass(sum(resonator(ticks, f * rng.uniform(0.95, 1.05), bw) for f, bw in woods), 2600.0, 4)
    return normalized(y * ar(dur, 0.05, 0.15))


def rustle(rng, dur):
    y = crackle(dur, 900, rng, (2000.0, 9000.0)) + 0.3 * normalized(bandpass(noise(samples(dur), rng), 2500, 8000))
    return normalized(y * (0.6 + 0.4 * smooth_noise(samples(dur), 5.0, rng)) * ar(dur, 0.06, 0.2))


def stomach(rng, dur):
    n = samples(dur)
    y = normalized(lowpass(noise(n, rng, "brown"), 280)) * (0.7 + 0.3 * smooth_noise(n, 5.0, rng))
    for _ in range(int(rng.integers(3, 6))):
        d = rng.uniform(0.04, 0.09)
        f = rng.uniform(160.0, 300.0)
        g = tone(d, curve([(0.0, f), (d, f * 1.7)], d, log=True), envelope=ar(d, 0.01, 0.03))
        s = int(rng.integers(0, max(1, n - samples(d))))
        y[s:s + len(g)] += 0.8 * g
    return normalized(y * ar(dur, 0.08, 0.2))


def buzz(rng, dur, f0, flutter=20.0):
    n = samples(dur)
    f = f0 * (1.0 + 0.03 * smooth_noise(n, 1.2, rng))
    y = voiced(dur, f, rng, jitter=0.004, shimmer=0.2, tilt=0.8, max_harmonics=40)
    y = shape(y, [(900.0, 700.0, 0.0), (2600.0, 1200.0, -8.0)], dur) + 0.3 * y
    wing = bandpass(noise(n, rng), 300, 3000) * (0.5 + 0.5 * np.sin(2 * np.pi * np.cumsum(f) / SR)) ** 2
    y = y + 0.25 * normalized(wing) * np.max(np.abs(y))
    return normalized(y * (1.0 + 0.25 * np.sin(2 * np.pi * flutter * np.arange(n) / SR + rng.uniform(0, 6.28))))


def voice_set(alien, kind, count, make):
    """Registers `count` variations of an alien's voice: V_<alien>_<kind>_<n>."""
    for i in range(count):
        SOUNDS.append(dict(file=f"V_{alien}_{kind}_{i + 1}", group="voice", alien=alien, kind=kind, make=make, index=i))


SOUNDS = []

# Wildmutt (Vulpimancer): a big eyeless beast - growls, snarls, barks, and sniffs with the gills on his neck.
voice_set("Wildmutt", "Growl", 3, lambda r, i: growl(r, r.uniform(1.3, 1.9), 58.0, 84.0, 0.82))
voice_set("Wildmutt", "Sniff", 2, lambda r, i: sniffs(r))
voice_set("Wildmutt", "Snarl", 2, lambda r, i: snarl(r, r.uniform(0.7, 1.0), 115.0, 160.0, 0.85))
voice_set("Wildmutt", "Bark", 2, lambda r, i: syllables(r, 1 + i, lambda k: bark(r, 380.0, 250.0, 0.9), gap=(0.08, 0.14)))
voice_set("Wildmutt", "Effort", 2, lambda r, i: snarl(r, r.uniform(0.35, 0.5), 130.0, 175.0, 0.85, teeth=0.7))
voice_set("Wildmutt", "Whine", 2, lambda r, i: whine(r, 620.0, 960.0, 1.1))

# Benwolf (Loboan): a werewolf - deep growls and snarls, a huff, and the howl that is his weapon.
voice_set("Benwolf", "Growl", 3, lambda r, i: growl(r, r.uniform(1.4, 2.0), 68.0, 95.0, 0.76, rough=0.6, rate=32.0))
voice_set("Benwolf", "Sniff", 1, lambda r, i: sniffs(r, 4))
voice_set("Benwolf", "Snarl", 2, lambda r, i: snarl(r, r.uniform(0.8, 1.1), 100.0, 145.0, 0.78))
voice_set("Benwolf", "Effort", 2, lambda r, i: snarl(r, r.uniform(0.35, 0.5), 110.0, 150.0, 0.78, teeth=0.8))
voice_set("Benwolf", "Huff", 2, lambda r, i: huff(r, 0.78))

# Ghostfreak (Ectonurite): raspy whispers, a hollow moan and a creepy laugh, all with a ghostly echo.
voice_set("Ghostfreak", "Whisper", 3, lambda r, i: reverb(whisper(r, r.uniform(1.0, 1.6)), r, 1.3, 0.35))
voice_set("Ghostfreak", "Moan", 1, lambda r, i: reverb(
    creature(1.9, pitch([(0.0, 195.0), (0.8, 150.0), (1.9, 165.0)], 1.9), r, vowel("u", 1.0, 2.2), breath=0.55,
             tilt=1.8, jitter=0.02, vibrato=(3.0, 0.045), envelope=ar(1.9, 0.4, 0.6))
    + 0.15 * tone(1.9, 440.0, envelope=ar(1.9, 0.6, 0.6)), r, 1.8, 0.45))
voice_set("Ghostfreak", "Laugh", 2, lambda r, i: reverb(laugh(r, 5 + i, 104.0, "e", 1.0, rough=0.6, breath=0.65), r, 1.2, 0.35))
voice_set("Ghostfreak", "Hiss", 2, lambda r, i: reverb(mix(gasp(r, 1.0), 0.5 * normalized(bandpass(noise(samples(0.4), r), 3000, 8000) * ar(0.4, 0.05, 0.25))), r, 1.0, 0.3))

# Four Arms (Tetramand): a huge chest - grunts, a battle roar, a deep chuckle, strained effort.
voice_set("FourArms", "Grunt", 2, lambda r, i: creature(0.32 + 0.1 * i, 96.0, r, vowel("uh", 0.85, 1.8), breath=0.2,
                                                         rough=0.3, rough_rate=34.0, tilt=1.1, drive=1.5, envelope=decay(0.32 + 0.1 * i, 0.14, 0.02)))
voice_set("FourArms", "Roar", 2, lambda r, i: creature(0.95, pitch([(0.0, 140.0), (0.3, 178.0), (0.95, 138.0)], 0.95), r,
                                                        vowel("a", 0.85, 2.0), breath=0.2, rough=0.5, rough_rate=30.0, sub=0.3,
                                                        tilt=0.9, drive=1.8, top=4000.0, envelope=ar(0.95, 0.06, 0.28)))
voice_set("FourArms", "Chuckle", 1, lambda r, i: laugh(r, 4, 98.0, "uh", 0.85, rate=5.0, rough=0.3, breath=0.3, fall=0.96))
voice_set("FourArms", "Effort", 3, lambda r, i: creature(0.36, 122.0, r, vowel("uh", 0.85, 1.4), breath=0.4, rough=0.65,
                                                          rough_rate=36.0, tilt=1.0, drive=2.0, envelope=ar(0.36, 0.04, 0.12)))

# XLR8 (Kineceleran): a quick raptor - chirps and trills, a sharp screech.
voice_set("XLR8", "Chirp", 3, lambda r, i: syllables(r, 2 + i % 2, lambda k: chirp(r, r.uniform(950.0, 1250.0), r.uniform(1350.0, 1650.0), r.uniform(0.07, 0.11), trill=0.03), gap=(0.03, 0.07)))
voice_set("XLR8", "Screech", 2, lambda r, i: creature(0.36, pitch([(0.0, 1300.0), (0.12, 1750.0), (0.36, 1250.0)], 0.36), r,
                                                       [(2300.0, 700.0, 0.0), (3600.0, 900.0, -4.0)], breath=0.3, rough=0.3,
                                                       rough_rate=60.0, tilt=1.0, drive=2.0, envelope=ar(0.36, 0.01, 0.15)))
voice_set("XLR8", "Effort", 2, lambda r, i: chirp(r, 700.0, 520.0, 0.12, tilt=1.4))

# Diamondhead (Petrosapien): a deep hum that rings through his crystal body.
voice_set("Diamondhead", "Hum", 3, lambda r, i: hum(r, 1.2, r.uniform(84.0, 96.0), 0.8) * 0.8
          + 0.5 * normalized(resonator(hum(r, 1.2, 90.0, 0.8), 740.0 * r.uniform(0.95, 1.05), 18.0))
          + 0.35 * crystal_ring(r, 1.2, 1480.0 * r.uniform(0.95, 1.05)))
voice_set("Diamondhead", "Clang", 2, lambda r, i: crystal_ring(r, 1.0, r.uniform(620.0, 760.0))
          + 0.6 * pad(creature(0.3, 110.0, r, vowel("uh", 0.85, 1.6), breath=0.25, rough=0.3, envelope=decay(0.3, 0.12, 0.02)), 0.7))
voice_set("Diamondhead", "Effort", 2, lambda r, i: pad(creature(0.32, 118.0, r, vowel("uh", 0.85, 1.6), breath=0.3, rough=0.45,
                                                                 drive=1.6, envelope=ar(0.32, 0.03, 0.12)), 0.5)
          + 0.4 * crystal_ring(r, 0.82, 1900.0))

# Upgrade (Galvanic Mechamorph): living technology - bleeps, a falling bwoop, glitches.
voice_set("Upgrade", "Bleeps", 3, lambda r, i: bleeps(r, int(r.integers(3, 7))))
voice_set("Upgrade", "Bwoop", 2, lambda r, i: crush(tone(0.34, curve([(0.0, 1400.0), (0.34, 380.0)], 0.34, log=True),
                                                          ((1, 1.0), (3, 0.35), (5, 0.15)), ar(0.34, 0.005, 0.1)), 7, 18000.0))
voice_set("Upgrade", "Glitch", 2, lambda r, i: np.tile(crush(fm(0.035, r.uniform(700.0, 1200.0), 1.41, 4.0, ar(0.035, 0.002, 0.01)), 5, 12000.0), 4))
voice_set("Upgrade", "Happy", 2, lambda r, i: syllables(r, 3, lambda k: tone(0.07, [784.0, 987.8, 1318.5][k], ((1, 1.0), (2, 0.25)), ar(0.07, 0.004, 0.03)), gap=(0.01, 0.02)))

# Grey Matter (Galvan): tiny and clever - a squeaky mutter, a curious "hm?", a startled "eep".
def mutter(r, d):
    """One of Grey Matter's tiny syllables."""
    return creature(d, pitch([(0.0, r.uniform(560.0, 700.0)), (d, r.uniform(560.0, 760.0))], d), r,
                    vowel(["a", "e", "i", "o", "uh"][int(r.integers(5))], 1.35, 1.2), breath=0.1, tilt=1.2, envelope=ar(d, 0.01, 0.03))


voice_set("GreyMatter", "Chatter", 3, lambda r, i: syllables(r, int(r.integers(6, 10)), lambda k: mutter(r, r.uniform(0.06, 0.1)), gap=(0.015, 0.045)))
voice_set("GreyMatter", "Query", 1, lambda r, i: hum(r, 0.28, 520.0, 1.35, vibrato=(0.0, 0.0)) * env(0.28, [(0.0, 0.0), (0.05, 1.0), (0.28, 0.0)]))
voice_set("GreyMatter", "Eep", 2, lambda r, i: chirp(r, 900.0, 1350.0, 0.12, tilt=1.6))

# Stinkfly (Lepidopterran): an insect - chitters, a squeal; his wings buzz as he hovers (a loop).
def chitter(r):
    """Stinkfly's insect chitter: rapid clicks through his shell's resonances over a buzzy squeak."""
    d = r.uniform(0.4, 0.6)
    ticks = clicks(d, r.uniform(18.0, 26.0), r, (1500.0, 7000.0), resonance=((2900.0, 500.0), (4800.0, 900.0))) * ar(d, 0.03, 0.15)
    return mix(ticks, 0.3 * creature(0.35, 310.0, r, vowel("i", 1.2, 1.6), breath=0.2, rough=0.4, rough_rate=45.0, envelope=ar(0.35, 0.02, 0.1)))


voice_set("Stinkfly", "Chitter", 3, lambda r, i: chitter(r))
voice_set("Stinkfly", "Squeal", 2, lambda r, i: creature(0.4, pitch([(0.0, 700.0), (0.15, 1100.0), (0.4, 800.0)], 0.4), r,
                                                          vowel("i", 1.3, 1.5), breath=0.25, rough=0.35, rough_rate=55.0, drive=1.8, envelope=ar(0.4, 0.02, 0.15)))
voice_set("Stinkfly", "Buzz", 2, lambda r, i: buzz(r, 0.7, 230.0, 26.0) * ar(0.7, 0.05, 0.25))

# Ripjaws (Piscciss Volann): a fish-man - gurgling growls, hisses, snapping jaws, gasping out of water.
voice_set("Ripjaws", "Gurgle", 2, lambda r, i: lowpass(growl(r, 1.1, 78.0, 95.0, 0.9, rough=0.55, rate=13.0, breath=0.3)
                                                        + 0.5 * bubbles(1.1, 14.0, r, 300.0, 900.0), 3800))
voice_set("Ripjaws", "Hiss", 1, lambda r, i: bandpass(noise(samples(0.55), r), 2000, 7500) * ar(0.55, 0.04, 0.3))
voice_set("Ripjaws", "Snap", 2, lambda r, i: mix(place(0.2, [(0.0, modal(0.08, [(1100.0, 1.0, 0.03), (2600.0, 0.6, 0.015)], r)),
                                                              (0.06, modal(0.08, [(1000.0, 0.9, 0.03), (2400.0, 0.5, 0.015)], r))]),
                                                   0.3 * normalized(bandpass(noise(samples(0.3), r), 2500, 7000) * ar(0.3, 0.05, 0.2))))
voice_set("Ripjaws", "Effort", 2, lambda r, i: snarl(r, 0.45, 120.0, 150.0, 0.9, teeth=0.9))
voice_set("Ripjaws", "Gasp", 2, lambda r, i: syllables(r, 3, lambda k: gasp(r, 0.9), gap=(0.12, 0.2)))

# Cannonbolt (Arburian Pelarota): a big, slow, good-natured ball - "hm-hm", "whoa!", "oof".
voice_set("Cannonbolt", "Hmm", 3, lambda r, i: syllables(r, 2, lambda k: hum(r, 0.2, [92.0, 84.0][k], 0.8, (0.0, 0.0)), gap=(0.05, 0.09)))
voice_set("Cannonbolt", "Whoa", 2, lambda r, i: creature(0.55, pitch([(0.0, 130.0), (0.22, 225.0), (0.55, 160.0)], 0.55), r,
                                                          glide("uh", "o", 0.55, 0.85), breath=0.2, rough=0.2, tilt=1.1, envelope=ar(0.55, 0.04, 0.18)))
voice_set("Cannonbolt", "Oof", 2, lambda r, i: creature(0.22, pitch([(0.0, 125.0), (0.22, 95.0)], 0.22), r, vowel("uh", 0.85, 1.8),
                                                         breath=0.45, rough=0.3, envelope=decay(0.22, 0.08, 0.008)))

# Wildvine (Florauna): a walking plant - creaking stems, rustling leaves, a woody groan.
voice_set("Wildvine", "Creak", 2, lambda r, i: creak(r, r.uniform(0.8, 1.3), 14.0, 42.0))
voice_set("Wildvine", "Rustle", 2, lambda r, i: rustle(r, r.uniform(0.5, 0.8)))
voice_set("Wildvine", "Effort", 2, lambda r, i: creak(r, 0.4, 30.0, 60.0) + 0.4 * rustle(r, 0.4))
voice_set("Wildvine", "Groan", 2, lambda r, i: creak(r, 0.9, 9.0, 16.0, woods=((260.0, 60.0), (600.0, 110.0), (1250.0, 200.0))))

# Upchuck (Gourmand): always hungry - a happy "mmm", burps, a rumbling stomach, a giggle.
voice_set("Upchuck", "Mmm", 2, lambda r, i: hum(r, 0.75, r.uniform(185.0, 215.0), 1.1, (5.0, 0.03)))
voice_set("Upchuck", "Burp", 1, lambda r, i: lowpass(creature(0.55, pitch([(0.0, 115.0), (0.55, 88.0)], 0.55), r, glide("o", "uh", 0.55, 1.0),
                                                              breath=0.3, rough=0.8, rough_rate=24.0, sub=0.3, drive=1.6, envelope=ar(0.55, 0.03, 0.2)), 3000))
voice_set("Upchuck", "Rumble", 1, lambda r, i: stomach(r, 0.9))
voice_set("Upchuck", "Huh", 2, lambda r, i: creature(0.26, pitch([(0.0, 200.0), (0.26, 295.0)], 0.26), r, vowel("uh", 1.1, 1.5),
                                                      breath=0.15, envelope=ar(0.26, 0.02, 0.1)))
voice_set("Upchuck", "Giggle", 2, lambda r, i: laugh(r, 4, 265.0, "e", 1.15, rate=7.0, rough=0.1, breath=0.25, fall=0.97))
voice_set("Upchuck", "Effort", 2, lambda r, i: creature(0.25, 170.0, r, vowel("uh", 1.1, 1.5), breath=0.3, rough=0.3, envelope=ar(0.25, 0.02, 0.1)))

# Ditto (Splixson): small and cheerful - bright "hey!" calls and giggles.
voice_set("Ditto", "Hey", 2, lambda r, i: creature(0.3, pitch([(0.0, 380.0), (0.1, 470.0), (0.3, 360.0)], 0.3), r, glide("e", "i", 0.3, 1.2),
                                                    breath=0.1, tilt=1.1, envelope=ar(0.3, 0.02, 0.1)))
voice_set("Ditto", "Giggle", 2, lambda r, i: laugh(r, 5, 440.0, "i", 1.2, rate=7.5, rough=0.05, breath=0.2, fall=0.98))
voice_set("Ditto", "Whoa", 2, lambda r, i: creature(0.45, pitch([(0.0, 400.0), (0.18, 560.0), (0.45, 420.0)], 0.45), r, glide("uh", "o", 0.45, 1.2),
                                                     breath=0.12, tilt=1.1, envelope=ar(0.45, 0.03, 0.15)))
voice_set("Ditto", "Effort", 2, lambda r, i: creature(0.14, 420.0, r, vowel("uh", 1.2, 1.4), breath=0.2, envelope=decay(0.14, 0.05, 0.008)))

# Echo Echo (Sonorosian): a walking amplifier - electronic chirps whose voice echoes.
voice_set("EchoEcho", "Chirp", 3, lambda r, i: echo(syllables(r, 2, lambda k: electronic_chirp(r, [1100.0, 780.0][k], [1250.0, 640.0][k], 0.11), gap=(0.02, 0.04)), 0.12, 0.5, 3))
voice_set("EchoEcho", "Squeal", 2, lambda r, i: echo(electronic_chirp(r, 1500.0, 2100.0, 0.25, index=4.0), 0.1, 0.55, 3))
voice_set("EchoEcho", "Blip", 2, lambda r, i: echo(electronic_chirp(r, 900.0, 700.0, 0.07), 0.09, 0.5, 2))
voice_set("EchoEcho", "Down", 2, lambda r, i: echo(electronic_chirp(r, 1300.0, 520.0, 0.22), 0.12, 0.5, 3))

# Heatblast (Pyronite, classic collection only): a crackling fiery grunt and laugh.
def fiery_laugh(r):
    ha = laugh(r, 3, 120.0, "a", 1.0, rate=5.0, rough=0.4, breath=0.4)
    return mix(ha, 0.5 * crackle(len(ha) / SR, 60, r))


voice_set("Heatblast", "Crackle", 2, lambda r, i: fiery_laugh(r))
voice_set("Heatblast", "Roar", 1, lambda r, i: creature(0.8, pitch([(0.0, 150.0), (0.25, 190.0), (0.8, 150.0)], 0.8), r, vowel("a", 1.0, 2.0),
                                                         breath=0.5, rough=0.5, drive=2.4, envelope=ar(0.8, 0.05, 0.25)))


# ---------------------------------------------------------------------------------------------
# The museum's shared sounds (UMuseumSoundLibrary)
# ---------------------------------------------------------------------------------------------

def sfx(name, kind, count, make, loop=False):
    """Registers `count` variations of a library sound: SFX_<name with dots as underscores>_<n>."""
    for i in range(count):
        SOUNDS.append(dict(file=f"SFX_{name.replace('.', '_')}_{i + 1}", group=kind, name=name, kind=kind, make=make, index=i, loop=loop))


# ---- the aliens appearing, picked up, flying home ----
def materialize(r, i):
    d = 0.95
    parts = [(0.0, thud(0.4, 110.0, 55.0, 0.15, r, 0.1) * 0.5)]
    for k, mult in enumerate([3, 4, 5, 6, 8, 10]):
        f = 196.0 * mult
        start = 0.05 + 0.06 * k
        length = d - start
        parts.append((start, tone(length, f * (1.0 + 0.002 * k), envelope=decay(length, 0.35, 0.02) * (1.0 + 0.3 * np.sin(2 * np.pi * 13.0 * timeline(length)))) / (1 + 0.25 * k)))
    parts.append((0.0, whoosh(0.8, 1800.0, 6500.0, r, 0.6) * 0.5))
    return reverb(place(d, parts), r, 0.7, 0.2)


sfx("Alien.Materialize", "alien", 2, materialize)
sfx("Alien.PickUp", "alien", 2, lambda r, i: whoosh(0.32, 500.0, 2200.0, r, 0.8) + 0.3 * pad(tone(0.12, 1318.5, envelope=decay(0.12, 0.05)), 0.2))
sfx("Alien.Return", "alien", 2, lambda r, i: whoosh(0.6, 2400.0, 600.0, r, 0.8, [(0.0, 0.0), (0.2, 1.0), (0.6, 0.0)]))

# ---- footsteps ----
sfx("Step.Light", "step", 4, lambda r, i: pad(bandpass(noise(samples(0.03), r), 1500, 6000) * decay(0.03, 0.008, 0.001), 0.02))
sfx("Step.Medium", "step", 4, lambda r, i: thud(0.12, 160.0, 90.0, 0.035, r, 0.5, 1800.0))
sfx("Step.Heavy", "step", 4, lambda r, i: thud(0.35, 80.0, 45.0, 0.1, r, 0.6, 900.0) + 0.3 * pad(crackle(0.2, 120, r, (400.0, 3000.0)) * decay(0.2, 0.06), 0.15))
sfx("Step.Claw", "step", 4, lambda r, i: thud(0.1, 150.0, 95.0, 0.03, r, 0.4, 1500.0)
    + 0.5 * place(0.1, [(0.0, modal(0.03, [(3400.0 * r.uniform(0.9, 1.1), 1.0, 0.006), (5200.0, 0.5, 0.004)], r)),
                        (r.uniform(0.012, 0.025), modal(0.03, [(3000.0 * r.uniform(0.9, 1.1), 0.8, 0.006)], r))])[:samples(0.1)])
sfx("Step.Squish", "step", 3, lambda r, i: normalized(lowpass(noise(samples(0.12), r), 1200) * decay(0.12, 0.04, 0.005))
    + 0.4 * tone(0.12, curve([(0.0, 220.0), (0.12, 140.0)], 0.12, log=True), envelope=decay(0.12, 0.035, 0.005)))
sfx("Step.Crystal", "step", 4, lambda r, i: thud(0.1, 180.0, 110.0, 0.03, r, 0.3) + 0.35 * crystal_ring(r, 0.1, r.uniform(2200.0, 2800.0))[:samples(0.1)])
sfx("Step.Metal", "step", 4, lambda r, i: modal(0.12, [(r.uniform(820.0, 900.0), 1.0, 0.03), (2050.0, 0.6, 0.02), (3300.0, 0.4, 0.012)], r)
    + 0.6 * thud(0.12, 140.0, 90.0, 0.025, r, 0.3))
sfx("Step.Rustle", "step", 3, lambda r, i: rustle(r, 0.14) + 0.4 * thud(0.14, 130.0, 80.0, 0.03, r, 0.3))
sfx("Step.Water", "step", 4, lambda r, i: normalized(lowpass(noise(samples(0.2), r), 3500) * decay(0.2, 0.05, 0.004)) + 0.6 * bubbles(0.2, 25.0, r, 500.0, 1400.0))


# ---- signature moves ----
def armor_clicks(r, count, spacing):
    return place(count * spacing + 0.05, [(k * spacing * r.uniform(0.8, 1.2), modal(0.05, [(r.uniform(900.0, 2200.0), 1.0, 0.015), (r.uniform(2600.0, 4200.0), 0.5, 0.008)], r)) for k in range(count)])


sfx("Move.Roll.Curl", "move", 2, lambda r, i: mix(normalized(armor_clicks(r, 5, 0.03)), 0.7 * thud(0.2, 120.0, 70.0, 0.06, r)))
sfx("Move.Roll.Uncurl", "move", 2, lambda r, i: mix(normalized(armor_clicks(r, 4, 0.04)), 0.3 * creature(0.25, 100.0, r, vowel("uh", 0.85), breath=0.3, envelope=decay(0.25, 0.1))))
sfx("Move.Roll.Loop", "move", 1, lambda r, i: make_loop(lowpass(noise(samples(2.2), r, "brown"), 260) * (0.75 + 0.25 * np.sin(2 * np.pi * 6.5 * timeline(2.2)))
                                                        + 0.25 * crackle(2.2, 160, r, (900.0, 4000.0)), 0.2), loop=True)
sfx("Move.Roll.Bounce", "move", 3, lambda r, i: thud(0.4, 140.0, 70.0, 0.1, r, 0.5)
    + 0.5 * modal(0.4, [(620.0, 1.0, 0.14), (1340.0, 0.7, 0.09), (2350.0, 0.4, 0.05)], r, spread=0.05)
    + 0.25 * modal(0.4, [(1800.0, 1.0, 0.12), (3100.0, 0.6, 0.08)], r))
sfx("Move.Dash.Zip", "move", 3, lambda r, i: mix(whoosh(0.28, 3200.0, 900.0, r, 0.7, [(0.0, 0.0), (0.03, 1.0), (0.28, 0.0)]),
                                                0.4 * tone(0.08, curve([(0.0, 450.0), (0.08, 2400.0)], 0.08, log=True), envelope=ar(0.08, 0.005, 0.04))))
sfx("Move.Dash.Skid", "move", 2, lambda r, i: (normalized(bandpass(noise(samples(0.45), r), 1500, 5500)) + 0.6 * crackle(0.45, 400, r)) * decay(0.45, 0.16, 0.01))
sfx("Move.Flare.Surge", "move", 2, lambda r, i: whoosh(0.9, 250.0, 1400.0, r, 1.2, color="brown") + 0.5 * crackle(0.9, 90, r) * ar(0.9, 0.1, 0.4))
sfx("Move.Flare.Fireball", "move", 2, lambda r, i: whoosh(0.5, 1500.0, 400.0, r, 1.0) + 0.3 * crackle(0.5, 120, r))
sfx("Move.Flare.Impact", "move", 2, lambda r, i: thud(0.5, 120.0, 60.0, 0.12, r, 0.7, 2500.0) + 0.6 * crackle(0.5, 200, r) * decay(0.5, 0.2))
sfx("Move.Flex.Aura", "move", 3, lambda r, i: thud(0.3, 90.0, 60.0, 0.1, r, 0.2) * 0.6 + 0.4 * whoosh(0.3, 300.0, 1200.0, r, 1.0))
sfx("Move.Stomp", "move", 2, lambda r, i: reverb(thud(1.0, 62.0, 38.0, 0.35, r, 0.8, 700.0)
                                                   + 0.35 * normalized(lowpass(noise(samples(1.0), r, "brown"), 140)) * decay(1.0, 0.4)
                                                   + 0.3 * crackle(1.0, 90, r, (500.0, 4000.0)) * decay(1.0, 0.3), r, 0.6, 0.15))
sfx("Move.Crystal.Charge", "move", 2, lambda r, i: crackle(0.4, 220, r, (2000.0, 9000.0)) * env(0.4, [(0.0, 0.0), (0.4, 1.0)])
    + 0.3 * tone(0.4, curve([(0.0, 900.0), (0.4, 1800.0)], 0.4, log=True), envelope=env(0.4, [(0.0, 0.0), (0.4, 1.0)])))


def crystal_burst(r, i):
    parts = [(0.0, thud(0.5, 110.0, 55.0, 0.14, r, 0.5))]
    t = 0.0
    for k in range(26):
        parts.append((t, crystal_ring(r, 0.3, r.uniform(1800.0, 4200.0)) * r.uniform(0.2, 0.6)))
        t += 0.02 * (1.0 - k / 30.0) + r.uniform(0.0, 0.01)
    shimmer = sum(tone(1.4, f, envelope=decay(1.4, 0.5, 0.05)) for f in (2093.0, 2637.0, 3136.0)) * 0.3
    parts.append((0.05, shimmer))
    return reverb(place(1.5, parts), r, 0.9, 0.2)


sfx("Move.Crystal.Burst", "move", 2, crystal_burst)
sfx("Move.Phase.Out", "move", 2, lambda r, i: reverb(whoosh(0.6, 400.0, 2600.0, r, 0.8, [(0.0, 0.0), (0.5, 1.0), (0.6, 0.0)])
                                                      + 0.3 * tone(0.6, curve([(0.0, 720.0), (0.6, 300.0)], 0.6, log=True), envelope=ar(0.6, 0.1, 0.2)), r, 1.4, 0.4))
sfx("Move.Phase.In", "move", 2, lambda r, i: reverb(mix(whoosh(0.5, 2600.0, 500.0, r, 0.8, [(0.0, 0.0), (0.1, 1.0), (0.5, 0.0)]),
                                                         0.25 * whisper(r, 0.4, 1.0)), r, 1.2, 0.4))


def scream(r, i):
    parts = []
    for k in range(4):
        d = 0.17
        c = curve([(0.0, 1050.0), (d, 1250.0)], d, log=True)
        pulse = fm(d, c, 0.2, 4.0, ar(d, 0.01, 0.06)) * (0.6 + 0.4 * np.sin(2 * np.pi * 45.0 * timeline(d)))
        parts.append((k * 0.2, pulse))
    return echo(place(0.85, parts), 0.11, 0.45, 3)


sfx("Move.Scream", "move", 2, scream)
sfx("Move.HowlStart", "move", 2, lambda r, i: place(0.5, [(0.0, normalized(bandpass(noise(samples(0.3), r), 500, 5000)) * env(0.3, [(0.0, 0.0), (0.25, 1.0), (0.3, 0.0)]) * 0.6),
                                                          (0.22, snarl(r, 0.28, 90.0, 130.0, 0.78, teeth=0.4))]))


def sonic_howl(r, i):
    d = 2.0
    voice = howl(r, d, 0.8)
    t = timeline(d)
    f0 = pitch([(0.0, 290.0), (0.28, 470.0), (0.55, 520.0), (d, 400.0)], d)
    ring = 0.22 * np.sin(3 * 2 * np.pi * np.cumsum(f0) / SR) * (0.5 + 0.5 * np.sin(2 * np.pi * 30.0 * t)) * env(d, [(0.0, 0.0), (0.3, 1.0), (d - 0.3, 1.0), (d, 0.0)])
    parts = [(0.0, voice + ring * np.max(np.abs(voice)))]
    for k in range(8):   # the eight sonic rings leaving his jaw
        parts.append((0.12 + 0.18 * k, 0.35 * (thud(0.16, 110.0, 70.0, 0.05, r, 0.1) + 0.5 * whoosh(0.16, 700.0, 2000.0, r, 0.8)) * np.max(np.abs(voice))))
    return reverb(place(d, parts), r, 1.1, 0.25)


sfx("Move.Howl", "move", 2, sonic_howl)
sfx("Move.Clone.Split", "move", 2, lambda r, i: place(0.5, [(0.0, modal(0.08, [(420.0, 1.0, 0.03)], r)), (0.06, modal(0.08, [(520.0, 1.0, 0.03)], r)),
                                                            (0.0, tone(0.45, curve([(0.0, 600.0), (0.45, 1800.0)], 0.45, log=True), envelope=ar(0.45, 0.02, 0.2) * (0.6 + 0.4 * np.sin(2 * np.pi * 20.0 * timeline(0.45)))) * 0.5)]))
sfx("Move.Clone.Merge", "move", 2, lambda r, i: place(0.5, [(0.0, tone(0.4, curve([(0.0, 1800.0), (0.4, 600.0)], 0.4, log=True), envelope=ar(0.4, 0.05, 0.1)) * 0.5),
                                                            (0.38, modal(0.1, [(380.0, 1.0, 0.035)], r))]))
sfx("Move.Spit.Chomp", "move", 2, lambda r, i: syllables(r, 3, lambda k: (bandpass(noise(samples(0.07), r), 700, 5000) + crackle(0.07, 800, r)) * decay(0.07, 0.025, 0.002), gap=(0.06, 0.1)))
sfx("Move.Spit.Gulp", "move", 2, lambda r, i: place(0.4, [(0.0, tone(0.18, curve([(0.0, 190.0), (0.18, 75.0)], 0.18, log=True), envelope=decay(0.18, 0.07, 0.004))),
                                                          (0.12, 0.6 * tone(0.12, curve([(0.0, 160.0), (0.12, 90.0)], 0.12, log=True), envelope=decay(0.12, 0.04, 0.004)))]))
sfx("Move.Spit.Swell", "move", 2, lambda r, i: stomach(r, 0.75) * env(0.75, [(0.0, 0.3), (0.75, 1.0)]))
sfx("Move.Spit.Spit", "move", 2, lambda r, i: place(0.2, [(0.0, normalized(lowpass(noise(samples(0.07), r), 2500) * decay(0.07, 0.02, 0.001))),
                                                          (0.03, 0.6 * creature(0.13, 185.0, r, vowel("u", 1.1), breath=0.3, envelope=decay(0.13, 0.05)))]))
sfx("Move.Energy.Pop", "move", 3, lambda r, i: place(0.45, [(0.0, normalized(bandpass(noise(samples(0.4), r), 200, 3500) * decay(0.4, 0.09, 0.002))),
                                                            (0.0, 0.6 * fm(0.18, curve([(0.0, 2000.0), (0.18, 300.0)], 0.18, log=True), 1.5, 2.0, decay(0.18, 0.08, 0.002))),
                                                            (0.02, 0.2 * crackle(0.4, 300, r, (3000.0, 10000.0)) * decay(0.4, 0.15))]))
sfx("Move.Pounce.Sniff", "move", 2, lambda r, i: sniffs(r, 3))
sfx("Move.Pounce.Leap", "move", 3, lambda r, i: whoosh(0.35, 600.0, 2500.0, r, 0.9, [(0.0, 0.0), (0.12, 1.0), (0.35, 0.0)]))
sfx("Move.Land", "move", 3, lambda r, i: thud(0.3, 110.0, 55.0, 0.07, r, 0.6, 1500.0) + 0.2 * pad(crackle(0.15, 200, r, (3000.0, 8000.0)) * decay(0.15, 0.05), 0.15))
sfx("Move.Splash", "move", 3, lambda r, i: normalized(lowpass(noise(samples(0.6), r), 4500) * decay(0.6, 0.12, 0.004)) + 0.8 * bubbles(0.6, 40.0, r, 400.0, 1500.0)
    + 0.3 * pad(modal(0.1, [(r.uniform(2500.0, 3500.0), 1.0, 0.02)], r), 0.5))
sfx("Move.Vine.Lash", "move", 3, lambda r, i: place(0.3, [(0.0, whoosh(0.22, 400.0, 3500.0, r, 0.9, [(0.0, 0.0), (0.18, 1.0), (0.22, 0.0)])),
                                                          (0.2, modal(0.06, [(r.uniform(2500.0, 3500.0), 1.0, 0.008), (5500.0, 0.6, 0.005)], r, strike=0.0008))]))
sfx("Move.Vine.Retract", "move", 2, lambda r, i: whoosh(0.4, 2500.0, 500.0, r, 0.9) + 0.4 * creak(r, 0.4, 25.0, 40.0))
sfx("Move.Seed.Throw", "move", 2, lambda r, i: whoosh(0.25, 800.0, 1800.0, r, 1.0))
sfx("Move.Seed.Pop", "move", 3, lambda r, i: place(0.4, [(0.0, modal(0.08, [(r.uniform(350.0, 450.0), 1.0, 0.03), (900.0, 0.5, 0.02)], r)),
                                                         (0.01, (crackle(0.35, 400, r, (800.0, 6000.0)) + 0.4 * normalized(bandpass(noise(samples(0.35), r), 600, 3000))) * decay(0.35, 0.08))]))
def melt_down(r, i):
    """Upgrade melting: a gloopy liquid-metal slurp sliding down, bubbling, with a digital glitch."""
    slurp = normalized(swept_resonator(noise(samples(0.6), r), curve([(0.0, 1300.0), (0.6, 180.0)], 0.6, log=True), 160.0)
                       * (0.6 + 0.4 * np.abs(smooth_noise(samples(0.6), 18.0, r))) * ar(0.6, 0.03, 0.2))
    return mix(slurp, 0.3 * bubbles(0.6, 20.0, r, 200.0, 600.0), 0.15 * bleeps(r, 2))


sfx("Move.Melt.Down", "move", 2, melt_down)
sfx("Move.Melt.Up", "move", 2, lambda r, i: place(0.8, [(0.0, swept_resonator(noise(samples(0.6), r), curve([(0.0, 200.0), (0.6, 1300.0)], 0.6, log=True), 160.0) * ar(0.6, 0.1, 0.1)),
                                                        (0.5, 0.5 * tone(0.25, 1318.5, ((1, 1.0), (2, 0.2)), decay(0.25, 0.08)))]))
sfx("Move.Scurry.Hop", "move", 3, lambda r, i: clicks(0.18, 45.0, r, (1500.0, 5000.0)) * ar(0.18, 0.005, 0.05) * 0.7
    + 0.3 * pad(chirp(r, 1000.0, 1300.0, 0.06), 0.12))
sfx("Move.Fly.TakeOff", "move", 2, lambda r, i: buzz(r, 0.9, 190.0, 30.0) * env(0.9, [(0.0, 0.2), (0.4, 1.0), (0.9, 0.0)]))
sfx("Move.Fly.Land", "move", 2, lambda r, i: buzz(r, 0.5, 170.0, 20.0) * env(0.5, [(0.0, 0.7), (0.5, 0.0)]) + 0.5 * thud(0.5, 150.0, 90.0, 0.04, r, 0.3))

# ---- the display cases ----
sfx("Case.Place", "case", 2, lambda r, i: place(1.0, [(0.0, thud(0.35, 95.0, 55.0, 0.1, r, 0.5)),
                                                      (0.05, normalized(bandpass(noise(samples(0.4), r), 2000, 6500)) * decay(0.4, 0.12, 0.01) * 0.3),
                                                      (0.1, tone(0.7, curve([(0.0, 60.0), (0.7, 120.0)], 0.7, log=True), ((1, 1.0), (2, 0.5), (3, 0.3), (4, 0.15)), ar(0.7, 0.2, 0.2)) * 0.4),
                                                      (0.7, tone(0.3, 880.0, envelope=decay(0.3, 0.1)) * 0.25), (0.78, tone(0.3, 1318.5, envelope=decay(0.3, 0.12)) * 0.25)]))
sfx("Case.Remove", "case", 2, lambda r, i: place(0.95, [(0.0, tone(0.8, curve([(0.0, 130.0), (0.8, 38.0)], 0.8, log=True), ((1, 1.0), (2, 0.5), (3, 0.35)), ar(0.8, 0.02, 0.3)) * 0.6),
                                                        (0.0, crackle(0.6, 300, r, (2000.0, 9000.0)) * decay(0.6, 0.2) * 0.5),
                                                        (0.1, sum(tone(0.7, curve([(0.0, f), (0.7, f * 0.5)], 0.7, log=True), envelope=decay(0.7, 0.25)) for f in (1568.0, 1976.0, 2349.0)) * 0.15)]))
sfx("Case.Arm", "case", 1, lambda r, i: place(0.3, [(0.0, tone(0.08, 990.0, ((1, 1.0), (3, 0.3), (5, 0.1)), ar(0.08, 0.004, 0.02))),
                                                    (0.14, tone(0.08, 990.0, ((1, 1.0), (3, 0.3), (5, 0.1)), ar(0.08, 0.004, 0.02)))]))
sfx("Case.Grab", "case", 2, lambda r, i: modal(0.12, [(r.uniform(1100.0, 1300.0), 1.0, 0.02), (2600.0, 0.4, 0.01)], r) + 0.5 * thud(0.12, 150.0, 100.0, 0.03, r, 0.2))
sfx("Case.Release", "case", 2, lambda r, i: thud(0.25, 115.0, 70.0, 0.06, r, 0.4) + 0.2 * modal(0.25, [(1500.0, 1.0, 0.03)], r))
sfx("Case.Resize", "case", 2, lambda r, i: place(0.14, [(k * 0.04, modal(0.03, [(r.uniform(1800.0, 2400.0), 1.0, 0.006)], r)) for k in range(3)]))


# ---- loops: a case's hum and each home world's ambience (quiet, heard up close through the glass) ----
def case_hum(r, dur):
    t = timeline(dur)
    hum_ = sum(level * np.sin(2 * np.pi * f * t) for f, level in ((60.0, 0.5), (120.0, 1.0), (180.0, 0.35), (240.0, 0.2)))
    hum_ *= 1.0 + 0.1 * smooth_noise(len(t), 3.0, r)
    return hum_ + 0.35 * lowpass(noise(len(t), r, "pink"), 900)


def ambience(name, make):
    def build(r, i):
        dur = 8.0
        return make_loop(normalized(make(r, dur)) + 0.25 * normalized(case_hum(r, dur)), 0.5)
    sfx(name, "loop", 1, build, loop=True)


sfx("Case.Hum", "loop", 1, lambda r, i: make_loop(case_hum(r, 4.5), 0.4), loop=True)
# Stinkfly hovers all the time: his wings buzz softly (UAlienDataAsset::Sounds.Loop, louder while he flies).
sfx("Loop.Stinkfly.Wings", "loop", 1, lambda r, i: make_loop(buzz(r, 3.2, 172.0, 22.0), 0.3), loop=True)
ambience("Amb.Bubbles", lambda r, d: lowpass(noise(samples(d), r, "brown"), 500) * 0.5 + bubbles(d, 3.5, r))
ambience("Amb.Embers", lambda r, d: lowpass(noise(samples(d), r, "brown"), 300) * 0.6 + crackle(d, 35, r) * 1.5)
ambience("Amb.Mist", lambda r, d: lowpass(swept_resonator(noise(samples(d), r, "pink"), 350.0 + 400.0 * (0.5 + 0.5 * smooth_noise(samples(d), 0.3, r)), 250.0), 1600.0, 4)
         * (0.6 + 0.4 * smooth_noise(samples(d), 0.25, r)) + 0.08 * tone(d, 1050.0 * (1.0 + 0.05 * smooth_noise(samples(d), 0.2, r)), envelope=np.clip(smooth_noise(samples(d), 0.15, r), 0.0, 1.0)))


def sparkles(r, d):
    out = np.zeros(samples(d))
    t = r.uniform(0.0, 0.5)
    while t < d:
        c = crystal_ring(r, 1.2, r.uniform(2500.0, 5000.0)) * r.uniform(0.2, 1.0)
        s = samples(t)
        out[s:s + len(c)] += c[:len(out) - s]
        t += r.exponential(0.8)
    return out + 0.05 * normalized(bandpass(noise(samples(d), r), 5000, 11000))


ambience("Amb.Sparkles", sparkles)


def insects(r, d):
    out = np.zeros(samples(d))
    t = 0.0
    while t < d:
        burst = r.uniform(0.2, 0.4)
        chirps_ = tone(burst, r.uniform(4200.0, 5200.0), envelope=(0.5 + 0.5 * np.sin(2 * np.pi * r.uniform(14.0, 22.0) * timeline(burst))) ** 4 * ar(burst, 0.02, 0.05))
        s = samples(t)
        out[s:s + len(chirps_)] += chirps_[:len(out) - s] * r.uniform(0.3, 1.0)
        t += burst + r.uniform(0.4, 1.4)
    return out + 0.4 * rustle(r, d) * 0.3


ambience("Amb.Spores", insects)
ambience("Amb.Pulses", lambda r, d: tone(d, 100.0, ((1, 1.0), (2, 0.4), (3, 0.2))) * 0.3
         + place(d, [(r.uniform(0.0, d - 0.3), tone(0.08, r.choice([880.0, 1174.7, 1568.0]), envelope=ar(0.08, 0.005, 0.03)) * 0.5) for _ in range(6)])[:samples(d)])

# ---- the glass (tap the glass) ----
sfx("Glass.Tap", "glass", 4, lambda r, i: modal(0.25, [(1150.0, 1.0, 0.09), (2230.0, 0.7, 0.07), (3480.0, 0.5, 0.05), (5110.0, 0.35, 0.04), (6900.0, 0.2, 0.03)], r, spread=0.05)
    + 0.6 * thud(0.25, 260.0, 220.0, 0.04, r, 0.15, 2000.0))
sfx("Glass.Knock", "glass", 3, lambda r, i: modal(0.45, [(800.0, 1.0, 0.18), (1600.0, 0.7, 0.14), (2700.0, 0.5, 0.1), (4100.0, 0.3, 0.07)], r, spread=0.05)
    + 1.0 * thud(0.45, 200.0, 150.0, 0.08, r, 0.3, 1500.0)
    + 0.15 * place(0.45, [(r.uniform(0.05, 0.2), modal(0.05, [(r.uniform(2000.0, 3500.0), 1.0, 0.01)], r)) for _ in range(3)])[:samples(0.45)])

# ---- the Omnitrix ----
sfx("Omnitrix.Wake", "omnitrix", 1, lambda r, i: place(0.3, [(0.0, tone(0.12, 1318.5, envelope=ar(0.12, 0.01, 0.06))), (0.09, tone(0.18, 1760.0, envelope=ar(0.18, 0.01, 0.1)))]))
sfx("Omnitrix.Activate", "omnitrix", 1, lambda r, i: place(0.6, [(0.0, modal(0.06, [(2400.0, 1.0, 0.012), (4100.0, 0.6, 0.008)], r)),
                                                                 (0.03, bandpass(tone(0.32, curve([(0.0, 120.0), (0.32, 260.0)], 0.32, log=True), ((1, 1.0), (2, 0.7), (3, 0.5), (4, 0.35), (5, 0.25))), 300, 3000) * ar(0.32, 0.03, 0.08) * 0.5),
                                                                 (0.33, thud(0.2, 220.0, 150.0, 0.04, r, 0.2) * 0.7),
                                                                 (0.33, tone(0.25, 880.0, ((1, 1.0), (2, 0.3)), decay(0.25, 0.1)) * 0.3)]))
sfx("Omnitrix.Dial", "omnitrix", 3, lambda r, i: modal(0.04, [(3200.0 * r.uniform(0.95, 1.05), 1.0, 0.006), (1100.0, 0.6, 0.008)], r, strike=0.0005))
sfx("Omnitrix.Select", "omnitrix", 1, lambda r, i: tone(0.1, 1760.0, ((1, 1.0), (2, 0.2)), ar(0.1, 0.004, 0.05)))


def slam(r, i):
    d = 1.7
    rise = fm(0.38, curve([(0.0, 200.0), (0.38, 1900.0)], 0.38, log=True), 1.5, curve([(0.0, 3.0), (0.38, 0.5)], 0.38), env(0.38, [(0.0, 0.0), (0.38, 1.0)]))
    chord = sum(tone(1.25, f, envelope=decay(1.25, 0.45, 0.005) * (0.7 + 0.3 * np.sin(2 * np.pi * 17.0 * timeline(1.25)))) for f in (523.3, 784.0, 1046.5, 1318.5))
    parts = [(0.0, modal(0.1, [(1800.0, 1.0, 0.04), (3300.0, 0.6, 0.03), (5200.0, 0.4, 0.02)], r) + thud(0.1, 90.0, 60.0, 0.04, r, 0.3)),
             (0.02, 0.5 * rise), (0.02, 0.4 * whoosh(0.38, 400.0, 4000.0, r, 0.8, [(0.0, 0.0), (0.38, 1.0)])),
             (0.4, 0.45 * chord), (0.4, 0.6 * whoosh(0.9, 5000.0, 300.0, r, 0.9, [(0.0, 1.0), (0.9, 0.0)])),
             (0.4, 0.7 * tone(0.8, curve([(0.0, 95.0), (0.8, 40.0)], 0.8, log=True), envelope=decay(0.8, 0.3, 0.005)))]
    return reverb(place(d, parts), r, 1.0, 0.25)


sfx("Omnitrix.Slam", "omnitrix", 1, slam)
sfx("Omnitrix.Beep", "omnitrix", 1, lambda r, i: tone(0.09, 1250.0, ((1, 1.0), (3, 0.3), (5, 0.12)), ar(0.09, 0.003, 0.02)))
sfx("Omnitrix.Timeout", "omnitrix", 1, lambda r, i: tone(1.0, curve([(0.0, 1200.0), (1.0, 180.0)], 1.0, log=True), ((1, 1.0), (3, 0.3)), ar(1.0, 0.01, 0.3) * (0.7 + 0.3 * np.sin(2 * np.pi * 14.0 * timeline(1.0))))
    + 0.6 * pad(thud(0.3, 100.0, 50.0, 0.1, r), 0.7))
sfx("Omnitrix.Denied", "omnitrix", 1, lambda r, i: place(0.3, [(0.0, crush(tone(0.1, 180.0, ((1, 1.0), (3, 0.5), (5, 0.3)), ar(0.1, 0.003, 0.02)), 6, 11000.0)),
                                                               (0.15, crush(tone(0.1, 160.0, ((1, 1.0), (3, 0.5), (5, 0.3)), ar(0.1, 0.003, 0.02)), 6, 11000.0))]))
sfx("Omnitrix.Ready", "omnitrix", 1, lambda r, i: place(0.45, [(0.0, tone(0.15, 1046.5, envelope=ar(0.15, 0.005, 0.08))), (0.1, tone(0.15, 1318.5, envelope=ar(0.15, 0.005, 0.08))),
                                                               (0.2, tone(0.25, 1568.0, envelope=ar(0.25, 0.005, 0.15)))]))
sfx("Omnitrix.Close", "omnitrix", 1, lambda r, i: place(0.3, [(0.0, tone(0.12, 1760.0, envelope=ar(0.12, 0.01, 0.06))), (0.09, tone(0.18, 1318.5, envelope=ar(0.18, 0.01, 0.1)))]))

# ---- the holographic panel ----
sfx("UI.Open", "ui", 1, lambda r, i: sum(tone(0.45, curve([(0.0, f * 0.7), (0.45, f)], 0.45, log=True), envelope=ar(0.45, 0.12, 0.2)) for f in (1046.5, 1318.5, 1568.0))
    + 0.5 * whoosh(0.45, 1200.0, 5000.0, r, 0.7))
sfx("UI.Close", "ui", 1, lambda r, i: sum(tone(0.35, curve([(0.0, f), (0.35, f * 0.7)], 0.35, log=True), envelope=ar(0.35, 0.02, 0.2)) for f in (1046.5, 1318.5, 1568.0))
    + 0.4 * whoosh(0.35, 4000.0, 1000.0, r, 0.7))
sfx("UI.Hover", "ui", 1, lambda r, i: tone(0.035, 2400.0, envelope=decay(0.035, 0.01, 0.001)))
sfx("UI.Click", "ui", 1, lambda r, i: tone(0.07, curve([(0.0, 1600.0), (0.07, 1200.0)], 0.07), ((1, 1.0), (2, 0.25)), decay(0.07, 0.025, 0.001))
    + 0.3 * modal(0.07, [(4200.0, 1.0, 0.004)], r))
sfx("UI.Page", "ui", 1, lambda r, i: whoosh(0.22, 900.0, 3500.0, r, 0.7) + 0.4 * pad(tone(0.06, 1568.0, envelope=decay(0.06, 0.02)), 0.16))
sfx("UI.Confirm", "ui", 1, lambda r, i: place(0.3, [(0.0, tone(0.12, 880.0, ((1, 1.0), (2, 0.2)), ar(0.12, 0.004, 0.06))), (0.1, tone(0.18, 1318.5, ((1, 1.0), (2, 0.2)), ar(0.18, 0.004, 0.1)))]))
sfx("UI.Error", "ui", 1, lambda r, i: place(0.3, [(0.0, tone(0.09, 190.0, ((1, 1.0), (3, 0.5), (5, 0.3)), ar(0.09, 0.004, 0.02))),
                                                  (0.13, tone(0.09, 170.0, ((1, 1.0), (3, 0.5), (5, 0.3)), ar(0.09, 0.004, 0.02)))]))


# ---------------------------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------------------------

def main():
    out_dir = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(ROOT, "SourceArt", "Sounds")
    os.makedirs(out_dir, exist_ok=True)
    manifest = []
    for entry in SOUNDS:
        rng = np.random.default_rng(zlib.crc32(entry["file"].encode()))
        x = np.asarray(entry["make"](rng, entry["index"]), dtype=float)
        x = finish(x, entry["kind"] if entry["group"] != "voice" else "voice", loop=entry.get("loop", False))
        wavfile.write(os.path.join(out_dir, entry["file"] + ".wav"), SR, np.round(np.clip(x, -1.0, 1.0) * 32767).astype(np.int16))
        record = {k: v for k, v in entry.items() if k not in ("make", "index")}
        record["length"] = round(len(x) / SR, 3)
        manifest.append(record)
    with open(os.path.join(out_dir, "sounds.json"), "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=1)
    voices = sum(1 for m in manifest if m["group"] == "voice")
    print(f"SOUNDS {len(manifest)} written to {out_dir} ({voices} voices, {len(manifest) - voices} library)")


if __name__ == "__main__":
    main()
