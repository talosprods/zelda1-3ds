#!/usr/bin/env python3
"""
Synthesizes the HOME-menu banner jingle (cia-assets/banner-audio.wav).

An original ~2.6 s chiptune flourish: pulse-wave arpeggio up a major chord,
a held top note with vibrato, and a triangle-wave bass underneath. Written
for this project so the CIA carries no Nintendo audio (the banner used to
play the game's own Triforce fanfare).

3DS banner audio must stay under ~3 seconds, or the whole banner silently
fails to render (DEVELOPMENT.md, "Banner jingle silently breaks").

Usage: python3 tools/gen-jingle.py   (standard library only)
"""
import math
import os
import struct
import wave

RATE = 44100
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "cia-assets", "banner-audio.wav")


def note(n):
    """MIDI note number -> Hz."""
    return 440.0 * 2 ** ((n - 69) / 12.0)


def pulse(phase, duty):
    return 1.0 if (phase % 1.0) < duty else -1.0


def triangle(phase):
    p = phase % 1.0
    return 4 * p - 1 if p < 0.5 else 3 - 4 * p


# (start seconds, duration seconds, midi note, duty)
LEAD = [
    (0.00, 0.11, 72, 0.25),   # C5
    (0.11, 0.11, 76, 0.25),   # E5
    (0.22, 0.11, 79, 0.25),   # G5
    (0.33, 0.11, 84, 0.25),   # C6
    (0.44, 0.11, 83, 0.125),  # B5
    (0.55, 0.11, 79, 0.125),  # G5
    (0.66, 0.16, 81, 0.25),   # A5
    (0.82, 1.70, 86, 0.50),   # D6, held
]
BASS = [
    (0.00, 0.66, 48),         # C3
    (0.66, 0.16, 53),         # F3
    (0.82, 1.70, 50),         # D3
]
ECHO_DELAY = 0.18
LENGTH = 2.6
FADE = 0.5


def envelope(t, dur, attack=0.004, release=0.06):
    if t < attack:
        return t / attack
    if t > dur - release:
        return max(0.0, (dur - t) / release)
    return 1.0


def render():
    n = int(LENGTH * RATE)
    left = [0.0] * n
    right = [0.0] * n

    for start, dur, midi, duty in LEAD:
        f = note(midi)
        held = dur > 1.0
        for i in range(int(start * RATE), min(n, int((start + dur) * RATE))):
            t = i / RATE - start
            vib = 1.0 + (0.006 * math.sin(2 * math.pi * 5.5 * t) * min(1.0, t * 3) if held else 0.0)
            decay = math.exp(-t * (1.2 if held else 6.0))
            s = 0.22 * pulse(f * vib * t, duty) * envelope(t, dur) * decay
            left[i] += s
            right[i] += s * 0.8
            # quieter, delayed copy on the other side for width
            j = i + int(ECHO_DELAY * RATE)
            if j < n:
                right[j] += s * 0.35
                left[j] += s * 0.15

    for start, dur, midi in BASS:
        f = note(midi)
        for i in range(int(start * RATE), min(n, int((start + dur) * RATE))):
            t = i / RATE - start
            s = 0.30 * triangle(f * t) * envelope(t, dur, release=0.1)
            left[i] += s
            right[i] += s

    fade_start = LENGTH - FADE
    frames = bytearray()
    for i in range(n):
        t = i / RATE
        g = 1.0 if t < fade_start else max(0.0, 1.0 - (t - fade_start) / FADE)
        for v in (left[i], right[i]):
            v = max(-1.0, min(1.0, v * g))
            frames += struct.pack("<h", int(v * 32000))
    return bytes(frames)


def main():
    path = os.path.normpath(OUT)
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(render())
    print("Wrote %s (%.1f s)" % (path, LENGTH))


if __name__ == "__main__":
    main()
