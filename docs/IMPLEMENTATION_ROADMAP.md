# Plan de implementación — Tisma Slicer

Fecha: 2026-10-05. Complejidad relativa: 1 (días) · 2 (1–2 semanas) · 3 (semanas) · 4 (meses) · 5 (proyecto en sí mismo).
Sustituye a `doc/TismaRoadmap.md` como plan de referencia (aquel queda como historial de lo ya hecho).

## 1. Análisis por funcionalidad

### Interfaz híbrida (básico / avanzado)
- **Existe**: modos Principiante / Experto / Avanzado de PrusaSlicer (filtran opciones); columna de navegación, Ajustes rápidos y tema oscuro (fork).
- **Reutilizable**: `NavRail` para los espacios de trabajo (Preparar, Laminar, Ingeniería, Estructuras, Configuración); modos de PrusaSlicer para básico/avanzado.
- **Falta**: espacios Ingeniería y Estructuras; barra superior estilo 3.0; páginas de ajustes con el nuevo estilo; corregir solapamiento de la columna.
- **Módulos a modificar**: `MainFrame`, `NavRail`, `Sidebar`, `Tab`. **Nuevos**: paneles de Ingeniería y Estructuras.
- **Dependencias**: ninguna. **Riesgos**: 11, 17. **Pruebas**: guion de humo con Xvfb. **Complejidad**: 2.

### Importación STEP y geometría CAD
- **Existe**: importación STEP con OCCT 7.6.1 y teselado configurable; el B-Rep se descarta.
- **Reutilizable**: `occt_wrapper`, `LoadStepDialog`, `Format/STEP.cpp`.
- **Falta**: guardar el STEP en el proyecto; id de cara por triángulo; retesselado; detección de errores geométricos (`BRepCheck`).
- **Módulos**: `occt_wrapper`, `Format/STEP.cpp`, `Format/3mf.cpp`, `ModelVolume`. **Nuevos**: `libslic3r/Cad/` (referencia al B-Rep y mapa cara↔triángulo).
- **Dependencias**: OCCT (actualizar a 7.8+). **Riesgos**: 5, 6, 12. **Pruebas**: STEP de referencia → número de caras y triángulos; ida y vuelta 3MF; retesselado conserva selecciones. **Complejidad**: 3.

### Pintado de condiciones de contorno
- **Existe**: `TriangleSelector`, `GLGizmoPainterBase`, guardado en 3MF.
- **Reutilizable**: todo el sistema de pintado y su serialización.
- **Falta**: modelo `EngineeringSetup`; gizmo nuevo con tipos de condición, dirección y magnitud; casos de carga; invalidación por hash.
- **Módulos**: `Model`, `Format/3mf.cpp`, `GLGizmosManager`. **Nuevos**: `Engineering.hpp/cpp`, `GLGizmoBoundaryConditions`.
- **Dependencias**: ninguna. **Riesgos**: 6, 12. **Pruebas**: serialización; selección inválida tras cambiar la malla. **Complejidad**: 3.

### FEA mecánico
- **Existe**: nada. Eigen, TBB y OpenVDB disponibles.
- **Falta**: voxelizado, ensamblado, solver, materiales ortótropos, resultados, visualización.
- **Nuevos**: `src/libtisma_fea/`; visualización de campos en `GLCanvas3D` (mapa de colores sobre la pieza).
- **Dependencias**: Eigen (ya); opcional AMGCL. **Riesgos**: 3, 4. **Pruebas**: barra a tracción, voladizo, patch test, convergencia, comparación con CalculiX. **Complejidad**: 4.

### FEA térmico
- **Existe**: nada. **Reutilizable**: la malla y el ensamblado del FEA mecánico (conducción estacionaria es más simple).
- **Falta**: condiciones térmicas, conductividad, resultados. **Dependencias**: ninguna nueva.
- **Riesgos**: 3. **Pruebas**: placa con temperaturas impuestas (solución analítica lineal). **Complejidad**: 3 (después del mecánico).

### Relleno adaptativo híbrido guiado por FEA
- **Existe**: cúbico adaptativo (geométrico); modificadores de densidad; relleno denso bajo techos (fork).
- **Reutilizable**: modificadores por volumen como primera forma de materializar el campo de densidad; pasos invalidables.
- **Falta**: campo de densidad/orientación desde tensiones; generación de modificadores internos; validación FEA de la pieza laminada; iteración.
- **Módulos**: `PrintObject` (paso nuevo), `Fill/` (v2). **Nuevos**: `src/libtisma_structures/`.
- **Dependencias**: `libtisma_fea`. **Riesgos**: 3, 4, 14. **Pruebas**: viga con carga conocida → más densidad en la zona de máximo momento; masa y rigidez comparadas con relleno uniforme; probetas físicas. **Complejidad**: 4.

### Lattice 3D
- **Existe**: panal 3D y gyroid (2.5D, no lattice de barras).
- **Falta**: generación de celdas y barras de espesor variable como malla o como trayectorias; conexión con el shell.
- **Nuevos**: en `libtisma_structures`. **Dependencias**: posiblemente CGAL (ya presente) para operaciones booleanas.
- **Riesgos**: rendimiento del booleano, fabricabilidad de barras inclinadas. **Pruebas**: estanqueidad de la malla, voladizos máximos, FEA de una celda. **Complejidad**: 4.

### Refuerzo local
- **Existe**: modificadores (más perímetros, más densidad) definidos a mano.
- **Falta**: refuerzos automáticos alrededor de cargas y fijaciones (nervaduras, más paredes); colocación desde las condiciones de contorno.
- **Nuevos**: en `libtisma_structures`. **Riesgos**: 3. **Pruebas**: agujero con carga → anillo de paredes extra. **Complejidad**: 3.

### Optimización estructural
- **Existe**: NLopt (usado para orientación).
- **Falta**: objetivos y restricciones configurables, bucle FEA ↔ generación.
- **Riesgos**: 4, 14; los objetivos **deben definirse con el usuario**. **Complejidad**: 4.

### Non-planar slicing
- **Existe (fork)**: ondas y cónico, rampa, caudal uniforme, límites del eje Z, validación de pendiente y jacobiano, tests.
- **Falta**: detección de colisiones del cabezal; variante "Curved Z" sobre la superficie superior real; vista previa más fiel.
- **Módulos**: `NonPlanar.*`, perfil de impresora (geometría del cabezal). **Riesgos**: 7. **Pruebas**: casos de colisión sintéticos; impresión física progresiva. **Complejidad**: 3.

### Belt printer
- **Existe**: nada; el patrón de transformación del G-code de `NonPlanar` es reutilizable.
- **Falta**: transformación afín configurable, cama infinita, primera capa, soportes, vista previa, perfiles.
- **Nuevos**: `libslic3r/Manufacturing/BeltTransform`. **Riesgos**: 8. **Pruebas**: ida y vuelta de coordenadas; G-code de un cubo comparado con referencia. **Complejidad**: 3–4.

### Arc overhang
- **Existe (fork)**: arcos concéntricos en voladizos no puenteables, opción `overhang_arcs`, test.
- **Falta**: definición del comportamiento final con el usuario; comparación con la implementación de Steven McCulloch; velocidad y enfriamiento específicos.
- **Riesgos**: 15, 16. **Complejidad**: 2.

### Trackhead
- **Existe**: nada. **Bloqueado**: falta la definición del usuario.

### Calibraciones
- **Existe (fork)**: 8 pruebas.
- **Falta**: flujo (factor de flujo por objeto en `PrintRegionConfig`), patrón/línea de PA, tolerancias, números en relieve, aviso de prueba activa, tests automáticos.
- **Complejidad**: 2.

### Aceleración por hardware (Vulkan / OpenGL / CPU)
- **Existe**: OpenGL (core 3.2, compatibilidad, ES); CPU multihilo con TBB.
- **Falta**: abstracción gráfica; backend Vulkan; cómputo GPU; detección de capacidades.
- **Riesgos**: 2. **Pruebas**: igualdad de resultados CPU/GPU dentro de tolerancia; arranque con y sin Vulkan. **Complejidad**: 5.

### Concurrencia y proyectos
- **Existe**: `Jobs/Worker`, `BackgroundSlicingProcess`, TBB; 3MF con versión (`VERSION_3MF = 1`).
- **Falta**: tareas de FEA/estructuras como `Job`; archivo de ingeniería versionado en el 3MF.
- **Complejidad**: 2.

## 2. Fases (ajustadas tras la auditoría)

| Fase | Contenido | Criterio de aceptación |
|---|---|---|
| 0 | Auditoría (este conjunto de documentos) | Documentos revisados por el usuario. |
| 1 | Arquitectura | Decisiones de §3 tomadas; `ARCHITECTURE.md` aprobado. |
| 2 | **Consolidación** | CRLF de `GLCanvas3D.cpp` restaurado; OpenSSL/curl/expat/libpng actualizados o justificados; línea base de tests en verde (o fallos documentados); tests de calibraciones; aviso de calibración activa; columna de navegación corregida; compilación de Windows verificada. |
| 3 | Interfaz híbrida | Espacios de trabajo en la columna; modo básico idéntico en comportamiento a PrusaSlicer. |
| 4 | Geometría CAD | STEP guardado en el proyecto, id de cara por triángulo, retesselado. |
| 5 | FEA mecánico | Condiciones de contorno pintadas; solver validado con casos analíticos; mapa de tensiones. |
| 6 | Relleno adaptativo v1 | Flujo preliminar → modificadores por densidad → validación FEA, con probeta física. |
| 7 | Fabricación avanzada | Colisiones en no planar; impresora de cinta con una máquina de referencia. |
| 8 | Aceleración | Decisión sobre Vulkan con mediciones; cómputo GPU opcional. |
| 9 | Estabilización | Pruebas, benchmarks, documentación, versión pública (con las obligaciones de la AGPL). |

Las calibraciones restantes y el FEA térmico pueden intercalarse donde convenga.

## 3. Estado de la Fase 2 (consolidación)

| Tarea | Estado | Commit |
|---|---|---|
| Restaurar CRLF de `GLCanvas3D.cpp` | Hecho (diff contra upstream: 28 líneas) | `004aabd` |
| OpenSSL 4.0.1, curl 8.21.0, expat 2.8.2, libpng 1.6.58 | Hecho; verificado en Linux; libpng y curl con Schannel los verifica el CI de Windows | `d909f72` |
| Tests automáticos de calibraciones `[Calibration]` | Hecho: 7 casos, 1359 comprobaciones | `57e6d09` |
| Aviso de calibración activa con "Desactivar" | Hecho y probado en la GUI | `1676169` |
| Columna de navegación sin solapes en ventanas bajas | Hecho y probado en la GUI | `1676169` |
| CI de Windows | **Nunca había pasado**: faltaba `-DCMAKE_CONFIGURATION_TYPES=Release` (FindOpenVDB pedía bibliotecas Debug). Corregido; pendiente de confirmar con una ejecución completa | `8111e4f` |
| Línea base de tests | 6 de 7 suites pasan; `libseqarrange_tests` sigue sin terminar tras > 1 h | — |

## 4. Estado de la Fase 3 (interfaz híbrida)

| Tarea | Estado |
|---|---|
| Espacios de trabajo en la columna: Preparar (vista 3D), Laminar (vista previa), Ingeniería, Estructuras, Calibración, Dispositivo, Printables, Configuración (menú: impresora, filamento, proceso, preferencias, asistente) | Hecho y probado en la GUI |
| Modo básico como PrusaSlicer: Ingeniería y Estructuras solo en modo Experto; al salir de Experto con uno de ellos abierto se vuelve a Preparar | Hecho y probado en la GUI (Principiante y Experto) |
| Páginas de Ingeniería y Estructuras: flujo previsto con la fase de cada paso, marcado como no disponible, sin controles simulados | Hecho |
| Barra superior: nombre del proyecto (con `*` si hay cambios) como la pestaña de proyecto de la 3.0; al pulsarlo se vuelve a la plataforma. Se mantiene la barra de menús del sistema (atajos de teclado y menú nativo de Windows) | Hecho y probado en la GUI |
| Páginas de ajustes con títulos planos (separador + título violeta) en lugar de recuadros, solo en las pestañas de ajustes; el resto de grupos no cambia | Hecho; probado en Proceso, Impresora y G-code personalizado |
| Verificación en Windows | Pendiente del CI |

## 5. Decisiones pendientes (para el usuario)

| Decisión | Propuesta de partida (no decidida) |
|---|---|
| Licencia | AGPL-3.0 obligatoria (obra derivada). |
| Plataformas del primer lanzamiento | **Decidido: Windows** (CI `build_windows.yml`); Linux después. |
| Solver FEM | Propio sobre vóxeles con Eigen; CalculiX como referencia de validación. |
| Materiales iniciales | PLA y PETG con propiedades ortótropas de bibliografía, editables. |
| Objetivos de optimización | Configurables; el usuario elige el primero (p. ej. rigidez a igual masa). |
| Estrategia de iteración | Manual en v1. |
| Trackhead | Esperando referencias. |
| Arc Overhang definitivo | Revisar la versión actual con impresiones de prueba. |
| Máquinas de cinta | Esperando especificaciones. |
| Alcance no planar | Mantener ondas/cónico + colisiones; Curved Z después. |
| Diseño visual | Estilo PrusaSlicer 3.0 con paleta Tisma (en curso). |
