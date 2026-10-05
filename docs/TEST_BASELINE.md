# Línea base de pruebas — Tisma Slicer

Fecha: 2026-10-05 · Commit: `96fef2f` · Entorno: Ubuntu 24.04, GCC, Release, librerías del sistema
(`build-utils/build_linux_system_libs.sh`), 4 núcleos.

| Suite (CTest) | Casos | Resultado | Tiempo |
|---|---|---|---|
| `libslic3r_tests` | 244 (incluye `[CadSource]`, 5 casos, Fase 4) | Pasa | 6,0 s |
| `fff_print_tests` | 138 (incluye `[Calibration]`, 7 casos) | Pasa | ≈ 40 s |
| `sla_print_tests` | 44 | Pasa | 31,9 s |
| `slic3rutils_tests` | 31 | Pasa | 3,1 s |
| `arrange_tests` | 31 | Pasa | 2,5 s |
| `thumbnails_tests` | 8 | Pasa | 0,01 s |
| `libtisma_fea_tests` | 19 (`[FEA]`, Fase 5; más un caso de rendimiento oculto `[FEA_benchmark]`) | Pasa | ≈ 25 s |
| `libseqarrange_tests` | — | **Sin resultado**: se detuvo al llegar al límite de 2 h de ejecución sin terminar (usa el optimizador z3). Es código de upstream que el fork no modifica; queda pendiente ejecutarlo en una máquina más rápida | > 2 h |

Incluye las pruebas propias del fork (`[NonPlanar]`, `[USBPrinter]`, `[ArcOverhangs]`, `[DenseInfill]`).

Cómo reproducir:

```
build-utils/build_linux_system_libs.sh          # configura y compila (con --deps instala dependencias)
ctest --test-dir build --output-on-failure -j3
```

Avisos del compilador vistos durante la compilación (heredados de upstream, no del fork): variables sin usar en
`GLCanvas3D.cpp`, `GLGizmoMmuSegmentation.cpp` y `Plater.cpp`, y comparaciones con signo distinto en tests.

Las pruebas `[CadSource]` usan `tests/data/block_with_hole.step` (bloque de 20 × 20 × 10 mm con agujero de 8 mm,
7 caras), generado con OCCT para este proyecto. Si la biblioteca STEP no se puede cargar junto al ejecutable de
pruebas, los casos que la necesitan avisan y no comprueban nada.

Pendiente: pruebas automáticas de los Ajustes rápidos y de la GUI.
