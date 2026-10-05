# Funcionalidades existentes — Tisma Slicer

Fecha: 2026-10-05. "Heredada" = viene de PrusaSlicer 2.9.6 sin cambios. "Fork" = añadida o modificada en Tisma.

## 1. Modo básico (todo heredado salvo lo indicado)

| Función | Estado | Dónde |
|---|---|---|
| Importar STL, OBJ, 3MF, AMF, SVG (relieve) | Heredada | `libslic3r/Format/` |
| Importar STEP (teselado al cargar) | Heredada | `occt_wrapper/`, `Format/STEP.cpp`, `GUI/LoadStepDialog` |
| Mover, rotar, escalar, apoyar cara, cortar, simplificar, medir, texto, SVG | Heredada | `GUI/Gizmos/` |
| Orientación y colocación automáticas, varias camas | Heredada | `slic3r-arrange*`, `MultipleBeds` |
| Perfiles de impresora, filamento y proceso; asistente; perfiles de fabricantes | Heredada | `Preset*`, `resources/profiles/` |
| Altura de capa fija, variable y adaptativa | Heredada | `Slicing.cpp`, `GLCanvas3D` (edición de capas) |
| Perímetros clásicos y Arachne | Heredada | `PerimeterGenerator.cpp`, `Arachne/` |
| Más de 15 patrones de relleno (rectilíneo, grid, gyroid, cúbico, **cúbico adaptativo**, rayo, panal 3D…) | Heredada | `Fill/` |
| Soportes normales y en árbol; pintado de soportes | Heredada | `Support/`, `GLGizmoFdmSupports` |
| Brim, skirt, balsa | Heredada | `Brim.cpp` |
| Velocidades, aceleraciones, enfriamiento, temperaturas | Heredada | `GCode.cpp`, `CoolingBuffer` |
| Vista previa por capas y trayectorias (libvgcode) | Heredada | `libvgcode/`, `GCodeViewer` |
| G-code ASCII y binario, macros, post-procesado | Heredada | `GCode*`, `libbgcode` |
| Multimaterial, torre de purga, pintado por colores | Heredada | `MultiMaterialSegmentation`, `WipeTower*` |
| Piel difusa (pintable), planchado, costura pintable | Heredada | `Fill/`, `GCode/SeamPlacer` |
| Envío a OctoPrint, PrusaLink, Moonraker, Duet…; Prusa Connect; Printables | Heredada | `Utils/`, `WebView*` |
| **Impresión directa por USB** (protocolo de host de Marlin) | **Fork** | `Utils/USBPrinter.*`, `GUI/USBPrintDialog.*` |
| **Ajustes rápidos** (favoritos por categoría en la barra lateral) | **Fork** | `GUI/QuickSettings.*` |
| **Columna de navegación y tema oscuro** estilo PrusaSlicer 3.0, identidad Tisma | **Fork** | `GUI/NavRail.*`, `GUI/TismaTheme.hpp`, `Sidebar.cpp` |
| **Suite de calibraciones** (8 pruebas) | **Fork** | `GUI/CalibrationDialog.*`, `GCode.cpp` (`emit_calibration_step`) |

## 2. Funciones avanzadas existentes

| Función | Estado | Detalle |
|---|---|---|
| Modificadores por volumen y por rango de capas | Heredada | Cualquier ajuste de región por zona: base para refuerzos locales. |
| Pintado sobre facetas | Heredada | `TriangleSelector` (soportes, costura, colores, piel difusa); persistente en 3MF. |
| **Capas no planares** (ondas, cónicas, rampa, caudal uniforme, límites de Z) | **Fork** | `NonPlanar.*`; deforma la malla, lamina y transforma el G-code. Ver `doc/NonPlanarSlicer.md`. |
| **Arc overhangs** | **Fork** | Arcos concéntricos en voladizos no puenteables (`PerimeterGenerator.cpp`, opción `overhang_arcs`). |
| **Relleno denso bajo superficies superiores** | **Fork** | `infill_dense`, `infill_dense_density` (`Fill.cpp`). |
| Vaciado y soportes SLA | Heredada | No aplica a FDM. |

## 3. Suite de calibraciones (fork)

Todas comparten un mecanismo: opciones de proyecto `calib_*` y un valor que cambia cada N mm de altura.

| Prueba | Modelo | Qué varía en el G-code | Verificación |
|---|---|---|---|
| Torre de temperatura | Bloques con puente y pin | `M104` | GUI + G-code |
| Pressure advance | Torre cuadrada de pared fina | `M900` / `SET_PRESSURE_ADVANCE` / `M572` | G-code |
| Retracción | Dos columnas | Longitud de retracción | G-code |
| Velocidad volumétrica máxima | Cilindro de una pared | Velocidad real de la pared | G-code |
| VFA | Torre cuadrada de una pared | Velocidad de perímetro | G-code |
| Aceleración | Torre de pared fina | `M204` | G-code |
| Esquinas | Torre de pared fina | `M205 J` / `M205 X Y` / `SQUARE_CORNER_VELOCITY` / `M566` | G-code |
| Input shaping | Torre de pared fina | `SET_INPUT_SHAPER` / `M593` | G-code |

"G-code" = verificado leyendo el G-code generado por línea de comandos. Ninguna prueba se ha impreso todavía.

**Faltan** respecto a OrcaSlicer: flujo (pasadas 1 y 2; necesita factor de flujo por objeto), patrón y línea de
PA, tolerancias, y la numeración en relieve de los bloques.

## 4. Pruebas automáticas del fork

`[NonPlanar]` (libslic3r y fff_print), `[USBPrinter]` (slic3rutils), `[ArcOverhangs]` y `[DenseInfill]` (fff_print).
**No hay pruebas** de Ajustes rápidos, columna de navegación ni calibraciones (pendiente: test del G-code de cada modo).
