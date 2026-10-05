# Auditoría inicial del código — Tisma Slicer

Fecha: 2026-10-05 · Rama auditada: `ccr-7e2013d9-qwmclz` (HEAD `96fef2f`)

Este documento corresponde a la Fase 0 de la especificación maestra. No se ha modificado código para
redactarlo. Todas las afirmaciones se verificaron en el repositorio; las que no se pudieron verificar se
marcan como **(sin verificar)**.

## 1. Versión real de la base

| Dato | Valor | Cómo se verificó |
|---|---|---|
| Base | **PrusaSlicer 2.9.6** (estable) | `git diff upstream_2.9.6 21cf648` vacío: el commit `21cf648` es idéntico a la etiqueta oficial `version_2.9.6` (`b028299c77…`) de `prusa3d/PrusaSlicer`. |
| `version.inc` | `SLIC3R_VERSION 2.9.6`, `SLIC3R_APP_NAME "Tisma Slicer"`, `SLIC3R_APP_KEY "PrusaSlicer"` | Lectura del archivo. |
| Historia anterior | Hubo un fork previo sobre 3.0.0-alpha12, abandonado. Sigue en la historia (`905ccb0`). | Mensaje del commit `21cf648`. |
| Licencia | AGPL-3.0 (archivo `LICENSE`). | Lectura del archivo. |

La etiqueta de upstream se guardó localmente como `upstream_2.9.6` para comparar.

## 2. Estructura del proyecto

```
src/
  libslic3r/        núcleo: geometría, laminado, relleno, soportes, G-code, configuración (244 archivos)
  slic3r/GUI/       interfaz wxWidgets + OpenGL (253 archivos)
  slic3r/Utils/     red, hosts de impresión, USB (fork)
  occt_wrapper/     lector STEP (OCCT) compilado como biblioteca dinámica aparte
  libvgcode/        visor de G-code (OpenGL)
  libseqarrange/    ordenación secuencial (usa z3)
  slic3r-arrange*/  colocación automática
  CLI/, PrusaSlicer.cpp   línea de comandos y arranque
deps/               recetas CMake de 28 dependencias externas (se descargan y compilan)
bundled_deps/       21 dependencias incluidas en el árbol
tests/              Catch2: libslic3r, fff_print, sla_print, slic3rutils, arrange, thumbnails…
resources/          iconos, perfiles de fabricantes, traducciones, shaders (GLSL 1.10, 1.40, ES)
```

Tamaños de referencia: `GLCanvas3D.cpp` 7976 líneas, `Plater.cpp` 7928, `GCode.cpp` 4198, `PrintObject.cpp` 3491.
Son los archivos más acoplados y los que más conflictos darán al fusionar con upstream.

## 3. Pipeline geométrico y de laminado (heredado)

1. **Importación** (`libslic3r/Format/`): STL, OBJ, 3MF, AMF, SVG, **STEP** (vía `occt_wrapper`, ver §6).
   Todo se convierte a `TriangleMesh` (`indexed_triangle_set`) dentro de `Model → ModelObject → ModelVolume`.
2. **Configuración por niveles**: preset global → objeto → volumen/modificador → rango de capas
   (`ModelConfig`, `PrintRegionConfig`). Es la base natural para refuerzos locales y densidad variable.
3. **Pintado sobre facetas** (`TriangleSelector`, `FacetsAnnotation`): soportes, costura, multimaterial y
   piel difusa. Se guarda en el 3MF como atributos por triángulo (`slic3rpe:custom_supports`, etc.).
   Estados: `NONE`, `ENFORCER`, `BLOCKER` y hasta 254 extrusores. Es reutilizable para condiciones de contorno.
4. **Laminado** (`PrintObjectSlice.cpp`): corte por planos Z → `ExPolygons` por capa y región.
5. **Perímetros** (`PerimeterGenerator`, clásico y Arachne), **relleno** (`Fill/`: rectilíneo, grid,
   gyroid, cúbico, **cúbico adaptativo**, rayo, panal 3D, etc.), **soportes** (normales y en árbol).
6. **G-code** (`GCode.cpp`, `GCode/`): ordenación de extrusiones, enfriamiento (`CoolingBuffer`),
   retracción/limpieza, arcos (`ArcWelder`), etiquetas de objeto, G-code binario (`libbgcode`).
7. **Procesado posterior y análisis** (`GCodeProcessor`): tiempos, estadísticas y datos para la vista previa.

El procesado de fondo lo hace `BackgroundSlicingProcess` con pasos invalidables (`PrintStep` / `PrintObjectStep`)
y TBB para el paralelismo.

## 4. Interfaz y renderizado

- **GUI**: wxWidgets 3.2 (fork de Prusa en Windows/macOS, sistema en Linux). Paneles: `MainFrame`, `Plater`,
  `Sidebar`, `Tab` (editores de presets), `TopBar`.
- **Escena 3D**: OpenGL directo (`GLCanvas3D`, `GLModel`, `GLShader`, `OpenGLManager`) con perfil core 3.2,
  modo de compatibilidad y OpenGL ES. Herramientas de edición (gizmos) e interfaz superpuesta con ImGui.
- **No hay ninguna capa de abstracción gráfica.** Las llamadas `gl*` están repartidas por la GUI. No existe
  código Vulkan (la única mención es una supresión de un aviso de fugas de memoria).
- **Trabajo asíncrono en la GUI**: `Jobs/` (`Worker`, `BoostThreadWorker`, `UIThreadWorker`, `ThreadSafeQueue`,
  `CancellableJob`, indicador de progreso en notificaciones). Reutilizable para FEA y optimización.

## 5. Cambios propios del fork (sobre 2.9.6)

`git diff upstream_2.9.6 HEAD`: 323 archivos, +13 775 / −8 670 líneas. Gran parte son iconos SVG recoloreados y
**un cambio de finales de línea en `GLCanvas3D.cpp`** (CRLF → LF, 15 942 líneas). El cambio real en ese archivo
son 28 líneas.

| Commit | Contenido | Archivos principales |
|---|---|---|
| `b1caff1` | Compilación en Linux con librerías del sistema | `build-utils/build_linux_system_libs.sh` |
| `61648f1`, `2b44b45`, `c855081` | Capas no planares, impresión USB, arc overhangs, relleno denso | `NonPlanar.*`, `Utils/USBPrinter.*`, `PerimeterGenerator.cpp`, `Fill.cpp`, `PrintObjectSlice.cpp`, tests |
| `615e5a5` | Estilo oscuro y compilación de Windows en GitHub Actions | `ImGuiWrapper.cpp`, `.github/workflows/build_windows.yml` |
| `5c2d25f`, `61de907` | Identidad Tisma: logo, paleta, nombre, pantalla de inicio | iconos, `GUI_App.cpp`, `libslic3r.h`, `resources-src/branding/` |
| `ad39815` | Ajustes rápidos (favoritos) en la barra lateral | `QuickSettings.*`, `Sidebar.cpp`, `Tab.cpp` |
| `6fcc1c7`, `d2b4912` | Columna de navegación, escena y panel derecho estilo PrusaSlicer 3.0 | `NavRail.*`, `TopBar.*`, `MainFrame.*`, `Sidebar.*`, `TismaTheme.hpp` |
| `96fef2f` | Suite de 8 calibraciones | `CalibrationDialog.*`, `GCode.cpp`, `PrintConfig.*`, `Plater.*` |

Identificadores que **se mantienen como "PrusaSlicer" a propósito**: `SLIC3R_APP_KEY` (catálogos de traducción,
generador en el 3MF), nombres de iconos (`PrusaSlicer.svg`…), etiquetas de metadatos `slic3rpe:` del 3MF.
La carpeta de datos es propia (`TismaSlicer`).

## 6. Estado de las áreas de la especificación

| Área | Estado actual |
|---|---|
| STEP | **Existe** importación vía OCCT 7.6.1 (`STEPCAFControl_Reader` + `BRepMesh_IncrementalMesh`), con desviación lineal y angular configurables (`LoadStepDialog`). La geometría B-Rep **se descarta** tras teselar: no hay retesselado ni referencias a caras. |
| Pintado de regiones | **Existe** (`TriangleSelector`), persistente en 3MF, ligado a triángulos de la malla (no a caras CAD). |
| FEA | **No existe.** |
| Relleno adaptativo | Existe el **cúbico adaptativo geométrico** (más denso cerca de las superficies, no guiado por cargas). Existe densidad por modificador. |
| Lattice 3D | **No existe** (hay panal 3D y gyroid como patrones 2.5D). |
| Refuerzo local | Parcial: modificadores con más perímetros / densidad; relleno denso bajo techos (fork). |
| No planar | **Existe en el fork** (deformación de malla + transformación del G-code; ver `doc/NonPlanarSlicer.md`). Sin detección de colisiones con la geometría del cabezal. |
| Belt printer | **No existe.** |
| Arc overhang | **Existe en el fork** (arcos concéntricos en voladizos no puenteables). |
| Trackhead | **No existe**; definición pendiente del usuario. |
| Vulkan | **No existe**; todo es OpenGL. |
| Calibraciones | **Existe en el fork**: temperatura, PA, retracción, velocidad volumétrica, VFA, aceleración, esquinas, input shaping. Falta flujo (requiere factor de flujo por objeto). |
| Impresión USB | **Existe en el fork** (protocolo de host de Marlin). |

## 7. Pruebas existentes

Catch2 3.8 con CTest. Suites: `libslic3r_tests`, `fff_print_tests`, `sla_print_tests`, `slic3rutils_tests`,
`arrange_tests`, `thumbnails`, `cpp17`. El fork añade `[NonPlanar]`, `[USBPrinter]`, `[ArcOverhangs]` y
`[DenseInfill]`. Resultado de la ejecución completa en este entorno: ver §8.

**No hay pruebas automáticas de la GUI.** Las verificaciones de interfaz de este fork se hicieron a mano
(Xvfb + capturas).

## 8. Línea base de compilación y pruebas

Entorno: Ubuntu 24.04, GCC, librerías del sistema (`build-utils/build_linux_system_libs.sh`), compilación
Release. La aplicación compila y arranca. Resultados de `ctest`: ver `docs/TEST_BASELINE.md`.

## 9. Hallazgos que conviene corregir pronto

1. **Finales de línea de `GLCanvas3D.cpp`** cambiados a LF: cada fusión con upstream dará conflicto en todo el
   archivo. Conviene restaurar CRLF (cambio mecánico, sin efecto funcional).
2. **OpenSSL 1.1.0l** en `deps/`: versión sin soporte y con licencia OpenSSL/SSLeay, que tiene problemas de
   compatibilidad con la GPL/AGPL. Ver `DEPENDENCIES_AND_LICENSES.md`.
3. **La columna de navegación** solapa el botón inferior (Ajustes) con los superiores si la ventana es baja.
4. **Las opciones de calibración** viven en la configuración del proyecto; si el usuario carga un modelo propio
   sobre una prueba de calibración sin empezar un proyecto nuevo, la prueba sigue activa. Falta un aviso visible.
5. El flujo de calibración en la GUI se verificó solo con la torre de temperatura. Las otras siete pruebas se
   verificaron por línea de comandos (G-code) pero no imprimiéndolas.
