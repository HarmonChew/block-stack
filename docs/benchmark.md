# Headless baseline

Measured on this workstation with an AMD Ryzen 9 9950X, GCC 15.2, CMake 4.2.3, and the ordinary CMake build in `build/` (no `CMAKE_BUILD_TYPE` override). The tool ran one process and supplied neutral controller input every frame. Finished games were reset with deterministic derived seeds. These numbers are a local baseline, not a guaranteed throughput on other hardware or with an AI model attached.

| Independent games | Frames per game loop | Total logical frames | Wall time | Logical frames/s | Completed games/s |
|---:|---:|---:|---:|---:|---:|
| 1 | 100,000,000 | 100,000,000 | 1.608 s | 62,204,412 | 11,282.64 |
| 100 | 1,000,000 | 100,000,000 | 1.484 s | 67,374,473 | 12,011.52 |
| 1,000 | 100,000 | 100,000,000 | 1.511 s | 66,169,529 | 11,415.57 |

Reproduce a row with:

```sh
./build/project_name_benchmark --envs 1000 --frames 100000 --seed 42
```

The vector loop is sequential in one C++ thread. Each environment owns its simulation and RNG state; scheduling cannot change the result. This benchmark excludes Python observation conversion and agent inference time.
