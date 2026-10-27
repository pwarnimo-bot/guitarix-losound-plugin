# Prints an octave-band spectrum (dB relative to the loudest band) of WAV files.
import sys, wave
import numpy as np

edges = [31, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000]
print(f"{'':16s}" + "".join(f"{e:>7d}" for e in edges[:-1]))
for path in sys.argv[1:]:
    w = wave.open(path)
    x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16) / 32768
    X = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
    f = np.fft.rfftfreq(len(x), 1 / w.getframerate())
    b = np.array([X[(f >= lo) & (f < hi)].sum() for lo, hi in zip(edges, edges[1:])])
    db = 10 * np.log10(b / b.max() + 1e-20)
    print(f"{path.split('/')[-1][:-4]:16s}" + "".join(f"{v:7.1f}" for v in db))
