"""Synthesises the mod's sound effects (cartoon-style, 22 kHz mono WAV)."""
import os, sys, wave
import numpy as np

SR = 22050
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "out")
os.makedirs(OUT, exist_ok=True)
rng = np.random.RandomState(1234)


def t_axis(sec):
    return np.arange(int(SR * sec)) / SR


def lowpass(x, cutoff):
    a = np.exp(-2 * np.pi * cutoff / SR)
    y = np.zeros_like(x)
    acc = 0.0
    for i, v in enumerate(x):
        acc = (1 - a) * v + a * acc
        y[i] = acc
    return y


def highpass(x, cutoff):
    return x - lowpass(x, cutoff)


def env(t, attack, decay):
    return np.minimum(1.0, t / max(attack, 1e-4)) * np.exp(-t / decay)


def save(name, x, peak=0.85):
    x = np.tanh(x * 1.4) / np.tanh(1.4)
    x = x / max(1e-9, np.abs(x).max()) * peak
    data = (x * 32767).astype("<i2").tobytes()
    with wave.open(os.path.join(OUT, name), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(data)
    print(name, f"{len(x) / SR:.2f}s")


def gunshot():
    t = t_axis(0.55)
    noise = rng.randn(len(t))
    crack = highpass(noise, 1800) * env(t, 0.0005, 0.018) * 1.0
    body = lowpass(noise, 2200) * env(t, 0.001, 0.045) * 1.2
    f = 95 * np.exp(-t / 0.08) + 45
    thump = np.sin(2 * np.pi * np.cumsum(f) / SR) * env(t, 0.002, 0.09) * 1.1
    tail = lowpass(noise, 700) * env(t, 0.02, 0.22) * 0.35
    x = crack + body + thump + tail
    # short slap-back echo like a street
    d = int(0.085 * SR)
    x[d:] += x[:-d] * 0.22
    return x


def click(t0, pitch, length, total):
    t = t_axis(total)
    x = np.zeros_like(t)
    m = t >= t0
    tt = t[m] - t0
    x[m] = (np.sin(2 * np.pi * pitch * tt) * 0.6 + highpass(rng.randn(m.sum()), 3000) * 0.7) * env(tt, 0.0003, length)
    return x


def equip():
    total = 0.36
    t = t_axis(total)
    swish = highpass(rng.randn(len(t)), 900) * np.exp(-((t - 0.05) / 0.035) ** 2) * 0.35
    return swish + click(0.10, 2600, 0.012, total) + click(0.19, 1900, 0.016, total) * 1.1


def holster():
    total = 0.28
    t = t_axis(total)
    swish = lowpass(highpass(rng.randn(len(t)), 500), 4000) * np.exp(-((t - 0.07) / 0.05) ** 2) * 0.5
    return swish + click(0.16, 1500, 0.02, total) * 0.8


def tick():
    t = t_axis(0.06)
    return np.sin(2 * np.pi * 1750 * t) * env(t, 0.001, 0.012) * 0.35


def cycle():
    t = t_axis(0.12)
    f = 900 + 900 * (t / 0.12)
    return np.sin(2 * np.pi * np.cumsum(f) / SR) * env(t, 0.002, 0.03) * 0.5


def rocket():
    """Rocket launch: a low cartoon "fwoomp", a short blast, and the motor's hiss falling away."""
    t = t_axis(1.7)
    noise = rng.randn(len(t))
    f = 105 * np.exp(-t / 0.10) + 38
    thump = np.sin(2 * np.pi * np.cumsum(f) / SR) * env(t, 0.003, 0.16) * 1.3
    blast = lowpass(noise, 1600) * env(t, 0.001, 0.07) * 1.4
    # the hiss: noise through a low-pass that closes as the rocket goes away
    cut = 4200 * np.exp(-t / 0.55) + 700
    a = np.exp(-2 * np.pi * cut / SR)
    y = np.zeros_like(noise)
    acc = 0.0
    for i, v in enumerate(noise):
        acc = (1 - a[i]) * v + a[i] * acc
        y[i] = acc
    hiss = highpass(y, 350) * np.minimum(1.0, t / 0.05) * np.exp(-np.maximum(t - 0.12, 0) / 0.45) * 0.9
    x = thump + blast + hiss
    d = int(0.11 * SR)
    x[d:] += x[:-d] * 0.18
    return x


def shotgun():
    """A shotgun blast: a hard crack, a deep chest-thump, a wide roar of noise and a long rolling tail with
    two street echoes - much bigger and lower than the handgun"""
    t = t_axis(1.6)
    noise = rng.randn(len(t))
    crack = highpass(noise, 1500) * env(t, 0.0004, 0.022) * 1.3
    roar = lowpass(noise, 1800) * env(t, 0.001, 0.085) * 1.6
    f = 70 * np.exp(-t / 0.10) + 34
    thump = np.sin(2 * np.pi * np.cumsum(f) / SR) * env(t, 0.002, 0.16) * 1.8
    tail = lowpass(noise, 520) * env(t, 0.03, 0.42) * 0.55
    x = crack + roar + thump + tail
    for delay, gain in ((0.095, 0.30), (0.23, 0.16)):
        d = int(delay * SR)
        x[d:] += lowpass(x[:-d], 2500) * gain
    return x


def stroke(t0, length, total, bright):
    """one slide of the pump: a short scrape of metal and wood"""
    t = t_axis(total)
    x = np.zeros_like(t)
    m = (t >= t0) & (t < t0 + length + 0.02)
    tt = t[m] - t0
    shape = np.sin(np.pi * np.clip(tt / length, 0, 1)) ** 0.7
    x[m] = lowpass(highpass(rng.randn(m.sum()), 600), bright) * shape * 0.55
    return x


def pump():
    """racking a pump-action: back (a scrape and a solid clack), then forward (a scrape and a sharp lock); the clacks land on the
    animation's stroke ends (src/shotgun.inc PumpOffset: kSgRackBack 0.14 s, kSgRackHome 0.32 s)"""
    total = 0.50
    x = stroke(0.00, 0.13, total, 5200) + click(0.140, 1150, 0.030, total) * 1.3 + click(0.145, 2400, 0.012, total) * 0.8
    x = x + stroke(0.205, 0.105, total, 6000) + click(0.320, 1500, 0.022, total) * 1.4 + click(0.325, 3100, 0.010, total)
    return x


def shell():
    """a spent plastic shell landing: a hollow tock, then two little bounces"""
    total = 0.45
    x = np.zeros_like(t_axis(total))
    for t0, g, p in ((0.0, 1.0, 900), (0.13, 0.55, 980), (0.22, 0.30, 1040)):
        x = x + click(t0, p, 0.020, total) * g
    return x


def bite():
    """a bite of a donut: the teeth meeting through soft cake - a muffled thump, a short soft crunch of the icing, a tiny tear"""
    t = t_axis(0.30)
    noise = rng.randn(len(t))
    f = 140 * np.exp(-t / 0.05) + 70
    thump = np.sin(2 * np.pi * np.cumsum(f) / SR) * env(t, 0.003, 0.05) * 0.9
    crunch = lowpass(highpass(noise, 900), 3200) * env(np.maximum(t - 0.012, 0), 0.004, 0.035) * 0.7
    tear = lowpass(highpass(noise, 400), 1500) * env(np.maximum(t - 0.07, 0), 0.02, 0.06) * 0.35 * (t > 0.07)
    return thump + crunch + tear


def chew():
    """one chew of a mouthful: soft and squashy, a closed mouth"""
    t = t_axis(0.20)
    noise = rng.randn(len(t))
    squash = lowpass(highpass(noise, 250), 900) * np.sin(np.pi * np.clip(t / 0.16, 0, 1)) ** 1.5 * 0.8
    f = 110 + 30 * np.sin(2 * np.pi * 6 * t)
    body = np.sin(2 * np.pi * np.cumsum(f) / SR) * env(t, 0.03, 0.06) * 0.35
    return squash + body


def gulp():
    """a big swallow: a little click and a falling, throaty 'glk'"""
    t = t_axis(0.32)
    f = 330 * np.exp(-t / 0.07) + 105
    tone = np.sin(2 * np.pi * np.cumsum(f) / SR) * env(t, 0.008, 0.09) * 0.9
    tone += np.sin(2 * np.pi * np.cumsum(f * 2.02) / SR) * env(t, 0.008, 0.05) * 0.25
    click_ = lowpass(highpass(rng.randn(len(t)), 1200), 4000) * env(t, 0.0005, 0.006) * 0.5
    return lowpass(tone, 1600) + click_



def pssht():
    """a can opened: the tab's click, then the hiss of the gas and a fizz of bubbles"""
    t = t_axis(0.75)
    click_ = lowpass(highpass(rng.randn(len(t)), 1500), 6000) * env(t, 0.0003, 0.004) * 0.9
    hiss = highpass(lowpass(rng.randn(len(t)), 9000), 2500) * env(t - 0.012, 0.004, 0.16) * (t > 0.012) * 0.75
    fizz = np.zeros(len(t))
    for i in range(70):   # (tiny bubble pops)
        t0 = 0.05 + rng.rand() * 0.65
        fizz += np.sin(2 * np.pi * (2500 + 2500 * rng.rand()) * (t - t0)) * env(t - t0, 0.0002, 0.002) * (t > t0) * 0.12 * np.exp(-t0 / 0.35)
    return click_ + hiss + highpass(fizz, 1800)


def burp():
    """a big, satisfied burp: a low, rough voice (95 Hz, jittery glottal pulses) through an 'oh' mouth, the pitch sagging"""
    t = t_axis(0.70)
    f0 = 98 - 22 * t + 6 * np.sin(2 * np.pi * 7.0 * t) + 3 * rng.randn(len(t)).cumsum() / np.sqrt(np.arange(1, len(t) + 1))
    ph = np.cumsum(f0) / SR
    src = 2 * (ph - np.floor(ph)) - 1   # (a sawtooth: the buzz)
    src *= 1 + 0.35 * np.sin(2 * np.pi * 23 * t + rng.rand())   # (roughness)
    def band(x, lo, hi):
        return lowpass(highpass(x, lo), hi)
    v = band(src, 380, 650) * 1.0 + band(src, 800, 1150) * 0.55 + band(src, 2300, 2900) * 0.12
    v += lowpass(rng.randn(len(t)), 900) * 0.10
    e = np.clip(t / 0.04, 0, 1) * np.clip((0.70 - t) / 0.25, 0, 1)
    return lowpass(v * e, 3200)


def crunch():
    """an aluminium can crushed in a fist: crinkles and a few metallic pings over 0.4 s"""
    t = t_axis(0.45)
    x = np.zeros(len(t))
    for i in range(16):
        t0 = 0.02 + 0.30 * (i / 16) ** 0.8 + 0.01 * rng.rand()
        a = 0.5 + 0.5 * rng.rand()
        x += highpass(lowpass(rng.randn(len(t)), 7000), 1400) * env(t - t0, 0.0005, 0.010) * (t > t0) * a
        if i % 4 == 0:
            for f in (1900, 3100, 4700):
                x += np.sin(2 * np.pi * f * (1 + 0.05 * rng.randn()) * (t - t0)) * env(t - t0, 0.0003, 0.02) * (t > t0) * 0.15
    return x


def clank():
    """an empty can hitting the pavement: a hollow ping (inharmonic partials) and a tick"""
    t = t_axis(0.30)
    x = np.zeros(len(t))
    for f, a, d in ((1180, 1.0, 0.07), (2730, 0.6, 0.05), (4310, 0.35, 0.03), (6020, 0.2, 0.02)):
        x += np.sin(2 * np.pi * f * t + rng.rand()) * env(t, 0.0003, d) * a
    x += highpass(rng.randn(len(t)), 2000) * env(t, 0.0002, 0.003) * 0.8
    return x


def ahh():
    """a satisfied 'ahh' after a long drink: a breathy, falling vowel"""
    t = t_axis(0.65)
    f0 = 135 - 40 * t
    ph = np.cumsum(f0) / SR
    src = (2 * (ph - np.floor(ph)) - 1) * 0.6 + rng.randn(len(t)) * 0.35
    def band(x, lo, hi):
        return lowpass(highpass(x, lo), hi)
    v = band(src, 650, 900) + band(src, 1050, 1350) * 0.6 + band(src, 2400, 2800) * 0.15
    e = np.clip(t / 0.06, 0, 1) * np.clip((0.65 - t) / 0.35, 0, 1)
    return v * e


def hiccup():
    """a drunk hiccup: a sharp glottal 'hic' - a voiced blip that jumps up in pitch, cut off"""
    t = t_axis(0.22)
    f0 = 160 + 260 * np.clip((t - 0.01) / 0.03, 0, 1)
    ph = np.cumsum(f0) / SR
    src = 2 * (ph - np.floor(ph)) - 1
    def band(x, lo, hi):
        return lowpass(highpass(x, lo), hi)
    v = band(src, 300, 600) + band(src, 1900, 2600) * 0.6 + highpass(rng.randn(len(t)), 3000) * 0.15
    e = np.clip(t / 0.006, 0, 1) * np.exp(-np.clip(t - 0.03, 0, None) / 0.03)
    return v * e


def whoa():
    """'whoa-oa': a wobbly voiced glide, the vowel going o -> a, as he tips over"""
    t = t_axis(0.85)
    f0 = 190 + 70 * np.sin(np.pi * t / 0.85) - 50 * t + 12 * np.sin(2 * np.pi * 6.5 * t)
    ph = np.cumsum(f0) / SR
    src = (2 * (ph - np.floor(ph)) - 1) * 0.8 + rng.randn(len(t)) * 0.08
    u = np.clip(t / 0.85, 0, 1)
    def band(x, lo, hi):
        return lowpass(highpass(x, lo), hi)
    o = band(src, 380, 560) + band(src, 700, 950) * 0.5
    a = band(src, 650, 900) + band(src, 1050, 1350) * 0.6
    v = o * (1 - u) + a * u
    e = np.clip(t / 0.05, 0, 1) * np.clip((0.85 - t) / 0.25, 0, 1)
    return v * e


def thud():
    """a body landing on the pavement: a low thump with a slap on top"""
    t = t_axis(0.45)
    thump = np.sin(2 * np.pi * (58 + 40 * np.exp(-t / 0.03)) * t) * env(t, 0.002, 0.12)
    body = lowpass(rng.randn(len(t)), 400) * env(t, 0.001, 0.09) * 1.2
    slap = highpass(lowpass(rng.randn(len(t)), 5000), 900) * env(t, 0.0005, 0.015) * 0.6
    return thump + body + slap


def groan():
    """a groan on the ground: a low, rough 'uhhh' that sags"""
    t = t_axis(0.9)
    f0 = 105 - 25 * t + 4 * np.sin(2 * np.pi * 5 * t)
    ph = np.cumsum(f0) / SR
    src = 2 * (ph - np.floor(ph)) - 1
    src *= 1 + 0.25 * np.sin(2 * np.pi * 19 * t)
    def band(x, lo, hi):
        return lowpass(highpass(x, lo), hi)
    v = band(src, 450, 700) + band(src, 1100, 1400) * 0.45 + band(src, 2300, 2700) * 0.1 + lowpass(rng.randn(len(t)), 800) * 0.05
    e = np.clip(t / 0.08, 0, 1) * np.clip((0.9 - t) / 0.4, 0, 1)
    return v * e


def snore():
    """a big snore: a rattling, low in-breath (the soft palate flapping ~30 times a second), a breathy out-breath"""
    t = t_axis(1.5)
    inb = (t < 0.8)
    flap = (np.sin(2 * np.pi * 31 * t) > 0.3).astype(float)
    src = lowpass(rng.randn(len(t)), 700) * (0.4 + 0.6 * flap) + np.sin(2 * np.pi * 62 * t) * 0.3 * flap
    e_in = np.clip(t / 0.25, 0, 1) * np.clip((0.8 - t) / 0.15, 0, 1) * inb
    out = highpass(lowpass(rng.randn(len(t)), 2500), 300) * 0.35
    e_out = np.clip((t - 0.85) / 0.1, 0, 1) * np.clip((1.5 - t) / 0.4, 0, 1)
    return src * e_in + out * e_out

def pat():
    """a pat on a big belly: a soft, deep thump of a hand on a shirt"""
    t = t_axis(0.16)
    f = 95 * np.exp(-t / 0.04) + 60
    thump = np.sin(2 * np.pi * np.cumsum(f) / SR) * env(t, 0.002, 0.035)
    cloth = lowpass(highpass(rng.randn(len(t)), 300), 2200) * env(t, 0.001, 0.012) * 0.45
    return thump + cloth


if __name__ == "__main__":
    save("sfx_shot.wav", gunshot(), 0.9)
    save("sfx_equip.wav", equip(), 0.7)
    save("sfx_holster.wav", holster(), 0.55)
    save("sfx_tick.wav", tick(), 0.22)
    save("sfx_cycle.wav", cycle(), 0.35)
    save("sfx_rocket.wav", rocket(), 0.9)
    save("sfx_shotgun.wav", shotgun(), 0.95)
    save("sfx_pump.wav", pump(), 0.75)
    save("sfx_shell.wav", shell(), 0.45)
    save("sfx_bite.wav", bite(), 0.70)
    save("sfx_chew.wav", chew(), 0.35)
    save("sfx_gulp.wav", gulp(), 0.60)
    save("sfx_pat.wav", pat(), 0.55)
    save("sfx_pssht.wav", pssht(), 0.65)
    save("sfx_burp.wav", burp(), 0.85)
    save("sfx_crunch.wav", crunch(), 0.60)
    save("sfx_clank.wav", clank(), 0.55)
    save("sfx_ahh.wav", ahh(), 0.45)
    save("sfx_hiccup.wav", hiccup(), 0.55)
    save("sfx_whoa.wav", whoa(), 0.55)
    save("sfx_thud.wav", thud(), 0.85)
    save("sfx_groan.wav", groan(), 0.55)
    save("sfx_snore.wav", snore(), 0.60)
