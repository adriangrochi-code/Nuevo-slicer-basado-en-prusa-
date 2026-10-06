# Rendimiento del laminado (Fase 8)

## Cómo se mide

`build-utils/benchmark_slicing.sh [binario] [repeticiones] [carpeta]` lamina desde la línea de comandos un conjunto
fijo de modelos (Benchy normal, ×2 con gyroid, con soportes normales y orgánicos, conejo ×1,5, tornillo ×4) con el
perfil por defecto, e imprime la mediana del tiempo total y de cada etapa (a partir del registro de PrusaSlicer).
Guarda el G-code de la primera repetición para comparar dos versiones.

Regla: una optimización no debe cambiar el G-code o, si lo cambia, la diferencia se explica y se mide (filamento,
tiempo estimado, desplazamiento de las coordenadas).

Máquina de referencia: contenedor Linux de 4 núcleos, GCC 13, `-O3`. Los tiempos absolutos en Windows serán otros;
lo que importa es la relación.

## Perfil inicial (Benchy, callgrind)

| Parte | Instrucciones |
|---|---|
| Clipper (operaciones booleanas y offsets) en relleno, preparación del relleno y perímetros | 47 % |
| Visibilidad de la costura (rayos contra la malla, `ModelInfo::raycast_visibility`) | 21 % |
| Exportación del G-code | 12 % |

Con 4 núcleos el Benchy laminaba solo 2,2 veces más rápido que con uno. Dos etapas no escalaban nada:
«Processing external surfaces» (1,12 s) y «Bridge over infill» (0,9 s), el 42 % del tiempo total.

Causa: una sola llamada a `merge_bridges` en una capa del Benchy hacía el cierre morfológico (`closing_ex`) de
30 polígonos con 37 000 puntos y tardaba 917 ms. Los puntos vienen de la propagación de ondas de la expansión de
puentes y casi todos están alineados: al reducir el polígono expandido, Clipper generaba una cantidad enorme de
intersecciones.

## Optimizaciones aplicadas

| Cambio | Efecto en el G-code | Mejora |
|---|---|---|
| `merge_bridges` (`LayerRegion.cpp`): quitar los puntos a menos de `SCALED_EPSILON` (0,1 µm) del contorno simplificado (Douglas-Peucker) antes de `closing_ex` | Coordenadas de algunos puentes desplazadas 1–4 µm (≈1,6 % de las líneas del Benchy); filamento y tiempo estimado iguales; área cerrada cambia < 0,001 % | Llamada de 924 ms → 21 ms |

Probado y descartado:
- Repartir las capas de «Processing external surfaces» de una en una (`simple_partitioner`): no mejora, porque el
  coste está en una sola capa.
- Recorrer primero el hijo más cercano en los rayos de «primer impacto» del árbol AABB (visibilidad de la costura),
  con el mismo resultado exacto (G-code idéntico en los 6 casos): más lento (exportación 0,84 → 1,19 s en el Benchy),
  calcular la entrada en las dos cajas hijas cuesta más que la poda que gana.

## Resultados (mediana de 3, segundos)

| Caso | Antes | Después | Mejora |
|---|---|---|---|
| benchy | 4,81 | 2,75 | −43 % |
| benchy_x2_gyroid | 9,28 | 5,12 | −45 % |
| benchy_supports | 6,62 | 4,65 | −30 % |
| benchy_organic | 5,65 | 3,55 | −37 % |
| bunny_x1.5 | 1,54 | 1,46 | (sin puentes grandes) |
| screw_x4 | 0,78 | 0,78 | (sin puentes grandes) |

## Correcciones de upstream incorporadas

PrusaSlicer no publicó versiones 2.9.x después de la 2.9.6; las correcciones del núcleo están en `master` (3.0), con
las rutas y el código reorganizados. Se portaron las que aplican a la 2.9.6:

| Corrección (upstream) | Archivo |
|---|---|
| SPE-2783: cuelgue de los soportes en árbol sin capas de interfaz y sin balsa | `Support/TreeSupport.cpp` |
| SPE-3329: faltan capas debajo de un objeto flotante con balsa | `Support/SupportCommon.cpp` |
| Comportamiento indefinido al procesar `end_filament_gcode` | `GCode.cpp` |
| SPE-3488: aceleración de viajes cortos emitida con la aceleración de viaje desactivada (no volvía a la normal) | `GCode.cpp` |
| #15768: precalentamiento `M104` con una herramienta que no se interpreta o no existe (índice −1 en las temperaturas) | `GCode/GCodeProcessor.cpp` |
| Memoria: caché de trayectorias suavizadas por capa reservada de antemano y liberada al terminar cada capa | `GCode.cpp` |
| SPE-3866: sin torre de purga, un cambio de herramienta al empezar la capa iba precedido del viaje (con rampa) hasta la pieza siguiente, y la boquilla vieja goteaba encima (prueba en `test_multi.cpp`, falla sin la corrección) | `GCode.cpp` |
| SPE-3377: la velocidad dinámica del ventilador se limita a [`min_fan_speed`, `max_fan_speed`] (por debajo del mínimo, apagado) | `GCode/CoolingBuffer.cpp` |

Revisadas y no necesarias en la 2.9.6 (el código ya estaba corregido o no existe): SPE-3973 (casco convexo vacío),
SPE-3792 (anclaje de puentes), SPE-3853 (prefijo de cambio de herramienta), SPE-3691 (ralentización multi
herramienta), invalidación de `nozzle_diameter`. Las de la nueva arquitectura de la 3.0 (validación de
`layer_config_ranges`, `Print::update`, SPE-3760 en `PrintApply`, la excepción del hilo de laminado en segundo plano)
no aplican. SPE-3414 (el extrusor por defecto de una pieza totalmente pintada se cuenta como usado) en la 2.9.6 solo
afecta a la lista de extrusores usados (precalentamiento), no a los cambios de herramienta; queda pendiente.

## Perfil después de la optimización (Benchy)

Total: 52 600 millones de instrucciones (antes 60 800 millones).

| Parte | Instrucciones | Tiempo real (4 núcleos) |
|---|---|---|
| Visibilidad de la costura (750 000 rayos: 30 000 muestras × 25) | 24 % | ~0,7 s de 2,75 s |
| Perímetros extra (`PrintObject::make_perimeters`, dos offsets por iteración sobre contornos de alta resolución) | 12 % | ~0,25 s |
| Perímetros Arachne | 12 % | ~0,25 s |

## Caché de la visibilidad de la costura

`ModelInfo::Visibility` guarda su resultado (muestras, visibilidad, radio) en una caché de 16 entradas, con una clave
que resume las mallas y matrices de las piezas y volúmenes negativos, la transformación del objeto y los parámetros.
En la interfaz, cambiar un ajuste que solo afecta al G-code (temperaturas, velocidades, G-code personalizado) ya no
vuelve a lanzar los 750 000 rayos (~0,7 s en el Benchy). Resultado idéntico (prueba `[TismaCache]`, G-code del
Benchy igual byte a byte).

## Pendiente (propuestas)

- Compilación con LTO (`/GL /LTCG` en MSVC), medida en Windows.
- Migrar de Clipper 6 a Clipper2 (más rápido en offsets y booleanas). Es un cambio amplio en todo el núcleo y no
  está autorizado.
