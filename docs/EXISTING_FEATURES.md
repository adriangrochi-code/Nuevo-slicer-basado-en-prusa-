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
| **Planchado de la interfaz superior del soporte** (`support_material_ironing`, con caudal, separación y velocidad propios): la interfaz superior se imprime sólida y se plancha antes de imprimir la pieza encima, para que la cara que apoya en el soporte salga más lisa | **Fork** | `Support/SupportCommon.cpp` (`iron_support_interface`), `GCode/ExtrusionOrder.cpp` (se imprime después de la interfaz) |
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
| **Arc overhangs** | **Fork** | Arcos concéntricos en voladizos no puenteables (`PerimeterGenerator.cpp`, opción `overhang_arcs`), con velocidad (`overhang_arcs_speed`), distancia máxima (`overhang_arcs_max_distance`) y solape entre arcos (`overhang_arcs_overlap`, en % del ancho de extrusión; 10 % por defecto, 0 % = arcos lado a lado como el Arc Overhang original) propios, cada uno con su calibración (columnas con una losa en voladizo, un valor por columna; el solape se pone en los volúmenes de cada columna). |
| **Relleno denso bajo superficies superiores** | **Fork** | `infill_dense`, `infill_dense_density` (`Fill.cpp`). |
| Vaciado y soportes SLA | Heredada | No aplica a FDM. |

## 3. Suite de calibraciones (fork)

Todas comparten un mecanismo: opciones de proyecto `calib_*` y un valor que cambia cada N mm de altura.

| Prueba | Modelo | Qué varía en el G-code | Verificación |
|---|---|---|---|
| Torre de temperatura | Un bloque por temperatura: el número en relieve al frente, puente, dos pilares y una espiga para los hilos, voladizo de 0° a 75° con marcas a 30°, 45° y 60°, y una espiga para quebrar (adhesión entre capas) | `M104`; espera inicial (`M109`) a la temperatura de la primera franja | GUI + G-code |
| Pressure advance | Torre cuadrada de pared fina | `M900` / `SET_PRESSURE_ADVANCE` / `M572` | G-code |
| Retracción | Dos columnas | Longitud de retracción | G-code |
| Velocidad volumétrica máxima | Cilindro de una pared | Velocidad real de la pared | G-code |
| VFA | Torre cuadrada de una pared | Velocidad de perímetro | G-code |
| Aceleración | Torre de pared fina | `M204` | G-code |
| Esquinas | Torre de pared fina | `M205 J` / `M205 X Y` / `SQUARE_CORNER_VELOCITY` / `M566` | G-code |
| Input shaping | Torre de pared fina | `SET_INPUT_SHAPER` / `M593` | G-code |

"G-code" = verificado leyendo el G-code generado por línea de comandos. Ninguna prueba se ha impreso todavía.

Pruebas por parches a lo largo de X (un valor por parche, un ajuste de sus volúmenes; tests en `test_calibration.cpp`):

| Prueba | Modelo | Ajuste |
|---|---|---|
| Alisado: flujo | Parches macizos de 20 mm de fondo y 2 mm de alto, con la cara de arriba alisada | `ironing_flowrate` (5 a 25 %) |
| Alisado: separación | Igual | `ironing_spacing` (0,05 a 0,25 mm) |
| Alisado: velocidad | Igual | `ironing_speed` (10 a 50 mm/s) |
| Arc overhangs: solape | Columnas con una losa en voladizo | `overhang_arcs_overlap` (0 a 40 %) |

**Precisión dimensional** (Calibración > Bloque de precisión dimensional y Corrección dimensional): un bloque de cuatro
escalones cuadrados (80, 50, 30 y 10 mm, 3 mm de alto cada uno) con agujeros de 4, 6, 8 y 10 mm. Con las medidas del
calibre, `libslic3r/DimensionalCorrection.cpp` ajusta una recta `medido = escala · diseño + desplazamiento`. La escala es
la contracción y crece con el tamaño; el desplazamiento es el borde, igual en cualquier tamaño. La cuenta va por X, por
Y, para los agujeros (con la escala de las medidas exteriores) y para las alturas, y tiene en cuenta los ajustes con los
que se imprimió el bloque. Escribe:

- en el filamento: `filament_shrinkage_compensation_xy` y `_z` (las de PrusaSlicer);
- en la configuración de impresión: `xy_size_compensation`, `xy_hole_compensation` (nueva: los agujeros aparte, con la
  misma clave y sentido que en Bambu Studio, `src/libslic3r/PrintConfig.cpp` de bambulab/BambuStudio) y
  `elefant_foot_compensation`.

El desplazamiento de las alturas (primeras capas) se muestra pero no se aplica: va al desfase Z o a la calibración de
primera capa. Lo que la recta no explica se muestra como el ruido de la impresora y del calibre: es el límite de la
precisión alcanzable. `xy_hole_compensation` agranda o achica los agujeros de cada isla después de la compensación de
tamaño (`PrintObjectSlice.cpp`). Con varias regiones en la capa solo los agranda: achicarlos necesitaría elegir qué
región rellena.

Las pruebas están en el grupo **Calibrar** de las herramientas de la placa (barra superior), una herramienta cada una,
más "Aplicar resultado"; también en Más > Calibración, y los modelos solos con clic derecho en la cama.

**Solo las que sirven a la impresora en uso** (`calibration_applies` en `CalibrationDialog.cpp`):

| Prueba | Se oculta con | Por qué (fuente) |
|---|---|---|
| Pressure advance | Bambu Lab; firmware que no es Klipper, RepRapFirmware ni Marlin | Bambu Lab la hace sola: todos los modelos de `resources/printers` de Bambu Studio (commit `47a13ee`) declaran `auto_pa_cali_thumbnail_image` (calibración automática de dinámica de flujo). Los comandos de la prueba son de esos tres firmwares. |
| Input shaping (vibraciones) | Bambu Lab; firmware que no es Klipper, RepRapFirmware ni Marlin 2 | Bambu Lab: "Auto-Calibration ... automatically adjusts key printer settings, such as bed leveling, nozzle height, vibration compensation and motor noise" ([Printer Calibration Guide](https://wiki.bambulab.com/en/general/printer-calibration)). `SET_INPUT_SHAPER` es de Klipper, `M593` de RepRapFirmware y de Marlin 2. |
| Esquinas | Bambu Lab; firmware que no es Klipper, RepRapFirmware ni Marlin | Bambu Lab no documenta un comando para el jerk o la desviación de unión (`M205`) y Bambu Studio no tiene esta prueba (`src/libslic3r/Calib.hpp`, `CalibMode`, commit `47a13ee`: PA, flujo, temperatura, velocidad volumétrica, VFA y retracción). Los comandos de la prueba son de esos tres firmwares (`SQUARE_CORNER_VELOCITY`, `M566`, `M205`). |
| Offset Z de la primera capa | Bambu Lab; Prusa CORE One, MK4/S, MK3.9/S, XL | Bambu Lab: la altura de la boquilla (misma guía). Prusa: "The Prusa CORE One, and the Original Prusa MK4/S, MK3.9/S, and XL are equipped with a LoadCell, which will calibrate the first layer automatically" ([Live adjust Z](https://help.prusa3d.com/article/live-adjust-z_112427)). |

La marca sale del perfil de sistema del que viene la impresora (`Orca_BBL`, `PrusaResearch`) o, para una impresora de
un proyecto, de `printer_notes` (`PRINTER_VENDOR_ORCA_BBL`, `PRINTER_VENDOR_PRUSA3D`) o de un `printer_model` que empieza
con "Bambu Lab"; los modelos de Prusa con LoadCell son `COREONE*`, `MK4*`, `MK3.9*` y `XL*`. El flujo, la velocidad volumétrica
máxima y la retracción se mantienen en las Bambu Lab: Bambu Studio también las ofrece a mano.

**Faltan** respecto a OrcaSlicer: flujo (pasadas 1 y 2; necesita factor de flujo por objeto), patrón y línea de
PA, tolerancias, y la numeración en relieve de los bloques.

## 4. Pruebas automáticas del fork

`[NonPlanar]` (libslic3r y fff_print), `[USBPrinter]` (slic3rutils), `[ArcOverhangs]` y `[DenseInfill]` (fff_print),
`[DimensionalCorrection]` (libslic3r y fff_print), `[Ironing]` (fff_print).
**No hay pruebas** de Ajustes rápidos, columna de navegación ni calibraciones (pendiente: test del G-code de cada modo).
