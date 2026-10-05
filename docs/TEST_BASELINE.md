# Línea base de pruebas — Tisma Slicer

Fecha: 2026-10-05 · Commit: `96fef2f` · Entorno: Ubuntu 24.04, GCC, Release, librerías del sistema
(`build-utils/build_linux_system_libs.sh`), 4 núcleos.

| Suite (CTest) | Casos | Resultado | Tiempo |
|---|---|---|---|
| `libslic3r_tests` | 239 | Pasa | 5,4 s |
| `fff_print_tests` | 132 | Pasa | 36,9 s |
| `sla_print_tests` | 44 | Pasa | 31,9 s |
| `slic3rutils_tests` | 31 | Pasa | 3,1 s |
| `arrange_tests` | 31 | Pasa | 2,5 s |
| `thumbnails_tests` | 8 | Pasa | 0,01 s |
| `libseqarrange_tests` | — | **Sin resultado**: seguía ejecutándose tras más de 20 min (usa el optimizador z3); se registrará cuando termine | > 20 min |

Incluye las pruebas propias del fork (`[NonPlanar]`, `[USBPrinter]`, `[ArcOverhangs]`, `[DenseInfill]`).

Cómo reproducir:

```
build-utils/build_linux_system_libs.sh          # configura y compila (con --deps instala dependencias)
ctest --test-dir build --output-on-failure -j3
```

Avisos del compilador vistos durante la compilación (heredados de upstream, no del fork): variables sin usar en
`GLCanvas3D.cpp`, `GLGizmoMmuSegmentation.cpp` y `Plater.cpp`, y comparaciones con signo distinto en tests.

Pendiente: pruebas automáticas de las calibraciones, de los Ajustes rápidos y de la GUI.
