# Job times by device (2026-11-04)

Measured on the QA container: 4 cores, Xeon 2.1 GHz, no GPU, engine dev bc900da. Phone, desktop and server columns
are estimates from the measured column (factors below), not measurements.

| Job | Measured (4-core host) | Phone high end (x1-2) | Phone mid range (x2-4) | Desktop 8-16 cores (x0.3-0.6) |
|---|---|---|---|---|
| Slice 20 mm cube | 0.05 s | < 1 s | < 1 s | < 0.1 s |
| Slice 204 800-triangle part, 2 h 46 min print | 0.8 s | 1-2 s | 2-4 s | 0.3-0.5 s |
| FEA 20 mm cube, normal | 32 s | 30 s-1 min | 1-2 min | 10-20 s |
| FEA 20 mm cube, high | 257 s | 4-9 min | 9-17 min | 1.5-2.5 min |
| CFD friction only | 0.01 s | < 1 s | < 1 s | < 0.1 s |
| CFD fast (16 cells), T part | 312 s | 5-10 min | 10-20 min | 1.5-3 min |
| CFD normal (24 cells), T part | 1998 s | 35-65 min | 65-130 min | 10-20 min |
| 3D scan | not measured (no COLMAP/GPU here) | capture only | capture only | needs an NVIDIA GPU |

The Android emulator here (no KVM) ran the FEA normal case for over 40 min: useless for timing.
