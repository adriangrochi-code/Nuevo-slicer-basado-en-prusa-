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
- **Estado**: hecho en la Fase 4, ver la sección 5.
- **Existía**: importación STEP con OCCT 7.6.1 y teselado configurable; el B-Rep se descartaba.
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

### Temperatura de trabajo (decisión del usuario, 2026-10-05)
- **Decisión**: no se hará conducción térmica. El análisis usa **una temperatura ambiente uniforme** para toda la
  pieza, como una cámara caliente (por ejemplo 130 °C), más las **cargas puntuales o por cara en N (componentes X, Y, Z)**
  y las caras fijas.
- **Efecto**: la temperatura cambia el módulo y la resistencia del material (tablas E(T), σ(T) por material, editables);
  dilatación térmica libre opcional más adelante. Aviso claro cuando la temperatura se acerca o supera la transición
  vítrea (PLA ≈ 55–60 °C, PETG ≈ 75–80 °C): a 130 °C ninguno de los dos sirve estructuralmente, y el resultado lo debe
  decir en lugar de dar un número engañoso.
- **Tabla de materiales** (pedido del usuario): todos los materiales de impresión habituales (PLA, PETG, ABS, ASA,
  PC, PA/PA-CF, TPU, PEI/PEKK, …) con módulo y resistencia en XY y entre capas, temperatura de transición vítrea /
  HDT y curva con la temperatura. Por defecto se toma el material del filamento elegido para imprimir
  (`filament_type` del perfil); se puede cambiar y editar. Los valores son de bibliografía y fichas técnicas, con su
  fuente, y se marcan como aproximados.
- **Veredicto**: además de los mapas de color, un resumen claro: «aguanta» (factor de seguridad ≥ el elegido),
  «fuera de carga» (tensión mayor que la resistencia a esa temperatura, con la zona marcada) o «fuera de temperatura»
  (temperatura de trabajo por encima del límite del material), sugiriendo materiales de la tabla que sí servirían.
- **Pruebas**: la misma barra a tracción a dos temperaturas da desplazamientos en la razón E(T1)/E(T2). **Complejidad**: 1
  (sobre el FEA mecánico).

### Relleno adaptativo híbrido guiado por FEA
- **Existe**: cúbico adaptativo (geométrico); modificadores de densidad; relleno denso bajo techos (fork).
- **Reutilizable**: modificadores por volumen como primera forma de materializar el campo de densidad; pasos invalidables.
- **Falta**: campo de densidad/orientación desde tensiones; generación de modificadores internos; validación FEA de la pieza laminada; iteración.
- **Módulos**: `PrintObject` (paso nuevo), `Fill/` (v2). **Nuevos**: `src/libtisma_structures/`.
- **Dependencias**: `libtisma_fea`. **Riesgos**: 3, 4, 14. **Pruebas**: viga con carga conocida → más densidad en la zona de máximo momento; masa y rigidez comparadas con relleno uniforme; probetas físicas. **Complejidad**: 4.
- **Objetivo pedido por el usuario (2026-10-05)**: «relleno mínimo necesario». Cada carga lleva un límite de
  deformación (mm o % del tamaño de la pieza, ya disponible en la Fase 5) además del factor de seguridad. El
  optimizador busca el relleno más ligero que cumpla ambos, para la mejor relación peso-resistencia.
  1. Modelo del material con relleno: paredes y techos como capa maciza; el interior con propiedades
     homogeneizadas según patrón y densidad (E_ef = E·(a·ρ + (1−a)·ρ²), coeficientes por patrón, a calibrar con
     probetas impresas).
  2. Versión 1: densidad uniforme mínima por bisección (y número de paredes) que cumple los límites.
  3. Versión 2: densidad por zonas siguiendo las tensiones (modificadores internos), menos material donde no trabaja.
  4. Validación con un segundo análisis de la estructura real laminada; resultado: peso, rigidez y factor de
     seguridad frente al relleno uniforme.

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

Las calibraciones restantes pueden intercalarse donde convenga. La temperatura de trabajo entra en la Fase 5.

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

## 5. Estado de la Fase 4 (geometría CAD)

| Tarea | Estado |
|---|---|
| Id de cara B-Rep por triángulo: el wrapper OCCT escribe el índice de cara (orden de `TopExp_Explorer`) en `stl_facet::extra`; la reparación de admesh mueve y borra facetas pero la etiqueta viaja con ellas; `TriangleMesh::from_facets(..., facet_tags)` la devuelve | Hecho, con prueba |
| `CadSource` en `ModelVolume` (`libslic3r/CadSource.hpp`): STEP compartido, sólido, desviaciones del teselado, cara por triángulo, resultado de `BRepCheck`. Solo vale mientras coincide con la malla (mismo número de triángulos); dividir o cortar la pieza lo descarta | Hecho |
| STEP guardado en el proyecto: `Metadata/Tisma_CAD/<clave>.step` + `Metadata/Tisma_cad.xml` (cara por triángulo en tramos `cara*n`). Entradas que otros laminadores ignoran; un 3MF sin ellas carga igual | Hecho, con prueba de ida y vuelta |
| Deshacer / rehacer: la pila guarda la clave del STEP y los tramos, el contenido queda en un registro de la sesión | Hecho |
| Volver a teselar (menú contextual de objeto y pieza): vuelve a leer el STEP guardado, recupera la posición actual (ajuste afín contra el teselado original, cubre mover, escalar y espejar) y reproyecta soportes, costura, multimaterial y piel difusa por cara | Hecho, con prueba |
| Detección de errores geométricos con `BRepCheck_Analyzer`: aviso al importar y comando «Comprobar geometría CAD» | Hecho; prueba con STEP válido (falta un STEP defectuoso de referencia) |
| Actualizar OCCT | No hecho: es opcional y cambiaría la receta de dependencias de Windows; se deja para cuando haga falta |

Decisiones y límites:

- Las caras pintadas enteras se copian exactas. Las pintadas en parte se reproyectan triángulo a triángulo (cada
  triángulo nuevo toma el estado de la zona pintada más cercana de la misma cara), sin subdividir; el aviso dice
  cuántas caras se aproximaron. Es suficiente para condiciones de contorno por cara (Fase 5).
- Máximo 65 534 caras por sólido con id (16 bits de `extra`); por encima las caras quedan sin id y se reproyectan
  por distancia.
- Un STEP de más de 256 MB no se guarda en el proyecto.
- Si la malla se editó (simplificar, reparar, editar vértices), volver a teselar se rechaza en lugar de adivinar.
- Los STEP se guardan dentro del 3MF: el proyecto crece lo que ocupa el STEP comprimido.

## 6. Estado de la Fase 5 (análisis estructural)

| Tarea | Estado |
|---|---|
| 5a. Núcleo en `src/libtisma_fea/`: tabla de 17 materiales con su variación con la temperatura, voxelizado, elementos hexaédricos con material transversalmente isótropo, gradiente conjugado sin matriz ensamblada (TBB), veredicto y materiales alternativos | Hecho, validado con soluciones analíticas (ver `docs/FEA.md`) |
| 5b. Configuración de ingeniería por objeto (`libslic3r/Engineering.hpp`): material, temperatura, factor de seguridad, apoyos por caras, cargas puntuales o por caras con límite de deformación (mm o %); guardada en el 3MF (`Metadata/Tisma_engineering.xml`) y en deshacer/rehacer; las caras CAD siguen a «Volver a teselar»; `build_analysis_input` pasa del objeto al análisis en coordenadas de impresión | Hecho, con pruebas |
| 5c. GUI (`GLGizmoEngineering`): el botón Ingeniería abre la vista 3D con el panel de análisis del objeto; material (por defecto el del filamento), temperatura y factor de seguridad; clic en caras (cara CAD completa o región plana) para apoyos y cargas, o en puntos para cargas con radio (3 mm por defecto); fuerza en N y límite de deformación; cálculo en segundo plano cancelable; mapa de colores sobre los vóxeles (seguridad, tensión, desplazamiento) con escala; veredicto y materiales alternativos; deshacer/rehacer | Hecho y probado en la GUI (bloque STEP: aguanta a 23 °C; a 130 °C «fuera de temperatura» con PEEK, PEKK, PEI, PSU y PA-CF propuestos; mapa de desplazamientos; Ctrl+Z) |
| Comparación con CalculiX | Pendiente |
| Pendiente de la 5c: el panel es alto en pantallas bajas; flechas de cargas por cara en el centroide; tabla de materiales editable | Pendiente |

Decisiones: solver propio sobre vóxeles con Eigen (sin dependencias nuevas); valores de material de fichas
técnicas y bibliografía, aproximados y editables más adelante; la pieza se considera maciza hasta la Fase 6.

## 7. Estado de la Fase 6 (relleno mínimo guiado por el análisis)

| Tarea | Estado |
|---|---|
| 6a. Paredes y relleno en el análisis (capa maciza + relleno homogeneizado por patrón), masa estimada; optimizador de relleno uniforme mínimo y por zonas | Hecho, con pruebas (ver `docs/FEA.md`) |
| 6b. El análisis lee perímetros, capas sólidas, densidad, patrón y modificadores del perfil y del objeto; `apply_infill` escribe la densidad y los modificadores de zona | Hecho, con prueba de ida y vuelta |
| 6c. GUI: casilla «Pieza impresa», masa estimada, vista «Relleno» (sin la capa maciza), sección «Relleno mínimo» (búsqueda en segundo plano, comparación con el relleno actual, aplicar uniforme o por zonas); el botón Estructuras abre esa sección; panel con desplazamiento | Hecho y probado en la GUI (bloque STEP, FS 6: actual 2,7 g; uniforme 45 % 3,3 g; por zonas 10 % + 23 % / 50 % 2,9 g; aplicado y laminado: el relleno cambia por zonas) |
| Validación con las trayectorias reales laminadas (voxelizar el G-code) | Pendiente |
| Calibración de los exponentes del relleno y del lattice con probetas impresas | Pendiente (necesita ensayos físicos) |
| 6d. Refuerzos locales (más perímetros y relleno alrededor de apoyos y cargas, relleno más ligero del resto) y lattice 3D (barras verticales y a 45°, sin soportes, diámetro variable según la tensión) en el núcleo | Hecho, con pruebas (ver `docs/FEA.md`) |
| 6e. Aplicar refuerzos (modificador con perímetros y relleno) y lattice (relleno 0 % + modificador macizo con las barras); reconocerlos al volver a analizar; secciones «Refuerzos locales» y «Lattice 3D» en Ingeniería con vista de las barras | Hecho y probado en la GUI (bloque STEP: lattice generado, aplicado y laminado; refuerzo calculado). Con la carga de prueba de 400 N (más de lo realista para esa pieza, el usuario indica 100 N como máximo) el lattice resultó casi tan pesado como la pieza maciza y el refuerzo no ahorró material: el panel lo dice |

Observaciones: el laminador convierte en sólidas las áreas de relleno pequeñas y añade puentes en los bordes de las
zonas; el modelo no lo tiene en cuenta, así que la masa real puede ser algo mayor que la estimada.

## 8. Dirección del usuario (05-10-2026) y fases siguientes

Orden indicado por el usuario: primero modernizar (render, rendimiento, bugs), después el laminado no planar y, como
últimas etapas, el servicio en la nube.

| Fase | Contenido | Estado |
|---|---|---|
| 7 | Render moderno: luz por píxel y sombras (técnicas de PrusaSlicer 3.0), ver `docs/RENDER.md` | 7a y 7b hechas; 7c (oclusión ambiental) y 7d (PBR) sin autorizar |
| 8 | Rendimiento del laminado y bugs conocidos: banco de pruebas (`build-utils/benchmark_slicing.sh`), perfil, optimizaciones medidas sin cambiar el G-code, bugs de upstream 2.9.6 y propios | En curso |
| 9 | Laminado no planar | Pendiente |
| 10 | Laminado en la nube: el mismo motor sin interfaz (la línea de comandos ya lamina sin GUI) en los servidores propios, con cola de trabajos | Arquitectura propuesta en `docs/CLOUD_ARCHITECTURE.md` (i5 central y pesado, dos i7 livianos), sin implementar |
| 11 | App de teléfono y FEA en la nube: subir el modelo, elegir perfil y material, analizar, laminar y descargar o enviar el G-code; suscripción (idea inicial: 10 USD/mes con un número de trabajos) | Planes propuestos en `docs/CLOUD_ARCHITECTURE.md` (créditos; Básico 10 USD, Business con varios usuarios), sin decidir |
| — | Aceleración del FEA: multigrid y después GPU con Vulkan (precisión mixta) | Propuesta, sin autorizar |
| — | Detección de fallas con cámara (servidor de Obico como servicio aparte, cámara en la impresión por USB, panel de granja en Business) | Propuesta; verificar la licencia de los pesos del modelo |
| — | Rediseño del panel derecho (ajustes rápidos de impresión, filamento e impresora con buscador) | Propuesta, sin autorizar |
| — | Impresoras de cinta (método de Cura BlackBelt reimplementado; FEA con capas inclinadas) | Propuesta, esperando modelo de impresora y ángulo |

Notas para las fases 10 y 11 (para decidir más adelante, no implementadas):
- Licencia: con la AGPL, el código del servidor modificado que se ofrece por red debe estar disponible para sus
  usuarios. Es compatible con el modelo: el código sigue abierto y se cobra el cómputo, el almacenamiento y la
  comodidad, no el programa.
- Capacidad: medir con el banco de pruebas cuántos trabajos por hora soporta cada servidor (un Benchy lamina en ~5 s
  con 4 núcleos; un análisis FEA con optimización de relleno tarda de segundos a minutos según la malla) para fijar el
  precio y las cuotas.
- Pendiente de decidir: precio y cuotas, cuentas y pago, almacenamiento y privacidad de los modelos, envío a la
  impresora (PrusaConnect, OctoPrint, Bambu…), plataforma de la app.

## 9. Decisiones pendientes (para el usuario)

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
