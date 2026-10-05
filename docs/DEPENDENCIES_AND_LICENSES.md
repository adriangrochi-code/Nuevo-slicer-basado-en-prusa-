# Dependencias y licencias — Tisma Slicer

Fecha: 2026-10-05. Fuente de versiones: URLs de descarga en `deps/+*/**.cmake` y cabeceras de `bundled_deps/`.
Las licencias marcadas **(sin verificar)** se indican de memoria y deben confirmarse leyendo el archivo de
licencia de la versión exacta antes de cualquier distribución. Esto no es asesoramiento legal.

## 1. Licencia del proyecto y obligaciones del fork

- PrusaSlicer se distribuye bajo **AGPL-3.0** (archivo `LICENSE`). Tisma Slicer es una obra derivada y **debe
  seguir siendo AGPL-3.0**: no se puede cambiar a otra licencia ni cerrar el código.
- Obligaciones al **distribuir binarios** (por ejemplo el zip de Windows de GitHub Actions): ofrecer el código
  fuente completo y correspondiente, incluidas las modificaciones, bajo AGPL-3.0, con los avisos de copyright.
- Cláusula de red (§13): si se ofrece el programa modificado a usuarios por red, también hay que ofrecerles el código.
- Un repositorio privado no incumple la licencia mientras no se distribuya. En cuanto se entregue un binario a
  terceros, aplican las obligaciones anteriores.
- Hay que conservar las cabeceras `///|/ Copyright (c) Prusa Research …` de los archivos heredados y los créditos
  de PrusaSlicer y Slic3r (ya están en la pantalla de inicio y en "Acerca de").
- **Marcas**: "Prusa" y "PrusaSlicer" son marcas de Prusa Research. El fork ya usa nombre, logo y carpeta de datos
  propios. Siguen apareciendo como identificadores internos (`SLIC3R_APP_KEY`, metadatos del 3MF) por
  compatibilidad, y en los perfiles del fabricante Prusa, que es lo habitual en otros forks.
- Código de **OrcaSlicer** (AGPL-3.0): se puede portar con atribución. El suite de calibraciones actual está
  inspirado en Orca, pero el código y los modelos son propios; no se copió código de Orca.

### Compatibilidad de licencias con AGPL-3.0

- Compatibles: MIT, BSD, ISC, zlib, BSL-1.0, Apache-2.0, MPL-2.0, LGPL-2.1+/LGPL-3.0, GPL-3.0(+), GPL-2.0-or-later.
- **Incompatibles**: GPL-2.0 *only* y la licencia original de OpenSSL/SSLeay (cláusula de publicidad).

## 2. Dependencias externas (`deps/`)

| Dependencia | Versión | Licencia | Notas |
|---|---|---|---|
| Boost | 1.83.0 | BSL-1.0 | |
| c-blosc | commit `8724c06` | BSD-3-Clause | Para OpenVDB. |
| CGAL | 5.6.2 | Núcleo LGPL-3.0+; **paquetes usados GPL-3.0+** (`Polygon_mesh_processing`, `Surface_mesh`, `Voronoi_diagram_2`…) | Compatible con AGPL-3.0. Impide relicenciar con licencia permisiva. |
| curl | 7.75.0 | curl (tipo MIT) | **Versión de 2021**, con vulnerabilidades conocidas. Actualizar. |
| Catch2 | 3.8.0 | BSL-1.0 | Solo tests. |
| cereal | 1.3.0 | BSD-3-Clause | Deshacer/rehacer. |
| Expat | 2.4.3 | MIT | Antigua; actualizar. |
| Eigen | 3.3.7 | MPL-2.0 | Base del álgebra lineal de un futuro FEA. |
| GLEW | 2.2.0 | BSD modificada + MIT | Solo OpenGL. |
| GMP | 6.2.1 | LGPL-3.0+ / GPL-2.0+ | Para CGAL. |
| libjpeg-turbo | 3.0.1 | IJG + BSD-3 + zlib | |
| libbgcode | commit `6f4ad7c` | AGPL-3.0 **(sin verificar)** | De Prusa. |
| MPFR | 3.1.6 | LGPL-3.0+ | Para CGAL. |
| NLopt | 2.5.0 | MIT; algunos algoritmos LGPL **(sin verificar cuáles se usan)** | Optimización (orientación, SLA). |
| NanoSVG | commit `abcd277` | zlib | |
| **OCCT** | **7.6.1** | LGPL-2.1 con excepción adicional | Ya se usa para STEP. Versión de 2022; la actual es 7.8/7.9. |
| OpenCSG | 1.4.2 | GPL-2.0 **o posterior (sin verificar)** | Si fuera GPL-2.0 *only* sería incompatible. Verificar `COPYING`. |
| OpenEXR | 2.5.5 | BSD-3-Clause | Para OpenVDB. |
| **OpenSSL** | **1.1.0l** | OpenSSL + SSLeay | **Riesgo**: sin soporte desde 2019 y licencia incompatible con GPL/AGPL. Pasar a OpenSSL 3.x (Apache-2.0) o al TLS del sistema. |
| OpenVDB | fork de Prusa `339ee88` | MPL-2.0 (versiones < 10) **(sin verificar versión)** | Vaciado y SLA. |
| libpng | 1.6.35 | libpng | Antigua (2018). |
| Qhull | 8.1-alpha3 | Qhull (permisiva) | |
| oneTBB | 2021.5.0 | Apache-2.0 | Paralelismo de todo el núcleo. |
| zlib | 1.3.1 | zlib | |
| heatshrink | 0.4.1 | ISC | G-code binario. |
| nlohmann/json | 3.12.0 | MIT | |
| wxWidgets | fork de Prusa `5462e7d` | wxWindows Library Licence (LGPL-2.0+ con excepción) | En Linux se usa la 3.2 del sistema. |
| z3 | 4.15.1 | MIT | Ordenación secuencial. |

## 3. Dependencias incluidas (`bundled_deps/`)

| Dependencia | Licencia (detectada en cabeceras) |
|---|---|
| admesh | GPL-2.0-or-later ("as published by… either version 2 … or later" — **verificar "or later"**) |
| agg | Anti-Grain Geometry (permisiva) |
| ankerl (unordered_dense) | MIT |
| avrdude | GPL-2.0-or-later (verificar) |
| fast_float | Apache-2.0 / MIT / BSL |
| glu-libtess | SGI Free Software License B 2.0 |
| hidapi | a elegir: GPL-3.0, BSD o licencia original |
| imgui | MIT |
| int128 | BSL-1.0 |
| libigl | MPL-2.0 (algunas partes Apache-2.0) |
| libnest2d | LGPL-3.0 **(sin verificar)** |
| localesutils, hints | Prusa (AGPL-3.0, del propio proyecto) |
| miniz | MIT |
| prusa_fdm_mixer | MIT |
| qoi | MIT |
| semver | MIT |
| stb_dxt / stb_image | dominio público / MIT |
| tcbspan | BSL-1.0 |

## 4. Dependencias candidatas para la especificación (no incorporadas)

Ninguna se ha añadido. Antes de usar cualquiera hay que verificar la licencia de la versión concreta.

| Necesidad | Candidata | Licencia | Comentario |
|---|---|---|---|
| CAD / STEP | OCCT (ya presente) | LGPL-2.1 + excepción | Actualizar a 7.8+ y conservar el B-Rep en lugar de descartarlo. |
| Álgebra lineal | Eigen (ya presente) | MPL-2.0 | Suficiente para un FEM propio (matrices dispersas, gradiente conjugado). |
| Solvers iterativos | AMGCL | MIT **(sin verificar)** | Multimalla algebraica para sistemas grandes en CPU. |
| FEM completo | MFEM | BSD-3 **(sin verificar)** | Muy completo; dependencia pesada. |
| FEM completo | deal.II | LGPL-2.1+ o Apache-2.0 según versión **(sin verificar)** | Pesado; pensado para investigación. |
| Solver externo | CalculiX | GPL-2.0+ | Se ejecutaría como proceso aparte; útil para validar el solver propio. |
| Mallado tetraédrico | TetGen 1.6 | AGPL-3.0 **(sin verificar)** | Compatible si es AGPL; robusto. |
| Mallado tetraédrico | fTetWild | MPL-2.0 **(sin verificar)** | Tolera mallas defectuosas; lento. |
| Mallado | Gmsh | GPL-2.0-or-later **(sin verificar)** | Biblioteca grande. |
| Vulkan | Vulkan-Headers / Loader | Apache-2.0 / MIT | Se cargan en tiempo de ejecución. |
| Vulkan | volk, VulkanMemoryAllocator | MIT | Carga de funciones y memoria. |
| Shaders | glslang / shaderc | BSD-3 / Apache-2.0 | Compilación de GLSL a SPIR-V. |

Recomendación inicial (a decidir con el usuario): empezar con un **FEM propio sobre vóxeles con Eigen**, sin
dependencias nuevas, y usar CalculiX como referencia externa de validación. Ver `ARCHITECTURE.md`.
