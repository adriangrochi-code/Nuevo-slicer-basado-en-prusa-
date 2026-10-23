# Funciones de firmware hechas en el G-code (estudio)

Fecha: 2026-10-23. Pregunta: ¿se puede hacer el pressure advance (Linear Advance en Marlin) en el post-procesado del
G-code, para las impresoras cuyo firmware no lo tiene? ¿Y otras funciones de firmwares nuevos para firmwares viejos?

Respuesta corta, según una simulación (no hay todavía impresiones reales):

- **Extrusión directa: sí**, con los valores de fábrica de Marlin, sin alargar la impresión.
- **Bowden: sí si se afloja el límite del extrusor en las uniones** (`M205 E15` con jerk clásico, `M205 J0.08` con
  junction deviation); con los valores de fábrica de Marlin 2 falla y además hace más lenta la impresión.

Abajo el porqué, el método, los números y lo que falta medir. Este documento es un estudio: Tisma todavía no tiene el
post-procesador.

## 1. Por qué hace falta

Marlin trae Linear Advance apagado: en `Configuration_adv.h` de Marlin 2.1.x la línea `//#define LIN_ADVANCE` está
comentada, así que el firmware de muchas impresoras Marlin baratas no lo tiene (y en placas de 8 bits muchas veces no
entra). Sin pressure advance el plástico sale tarde al acelerar y sigue saliendo al frenar: esquinas abultadas,
comienzos de línea flacos y costuras marcadas. Klipper y RepRapFirmware (`M572`) tienen pressure advance propio: no lo
necesitan.

## 2. La física (y por qué el G-code alcanza)

- El filamento entre el engranaje y la boquilla se comporta como un resorte: Marlin dice que Linear Advance «handles
  this free filament length as a spring, where K is a spring constant»
  ([marlinfw.org, Linear Advance](https://marlinfw.org/docs/features/lin_advance.html)). Modelo: lo que sale por la
  boquilla, D, sigue a lo que empujó el extrusor, E, con un retardo τ: `dD/dt = (E − D) / τ`.
- Klipper hace `pa_position = nominal_position + pressure_advance · nominal_velocity`
  ([klipper3d.org, Kinematics](https://www.klipper3d.org/Kinematics.html)). Con `pressure_advance = τ` la boquilla
  deposita exactamente lo nominal: `E = D + τ·dD/dt`. Valores típicos según Klipper: «between 0.050 and 1.000 (the high
  end usually only with bowden extruders)» ([Pressure advance](https://www.klipper3d.org/Pressure_Advance.html)).
- El firmware mueve el extrusor un poco por delante: `K · (área de la línea / área del filamento) · v`, donde v es la
  velocidad del cabezal en ese punto. **Ese adelanto depende solo de la velocidad del cabezal**, y la velocidad la
  decide el planificador del firmware de forma determinista (trapecios con aceleración y un límite en cada unión). Si el
  slicer reproduce ese planificador, sabe la velocidad en cada punto y puede escribir el adelanto en el G-code.

## 3. El método

1. Planificar el G-code como lo hará el firmware: velocidades en las uniones, rampas de aceleración y de frenada.
2. Cortar las rampas en trozos iguales de al menos 1 mm (lo que va a velocidad constante queda entero). Más cortos no:
   con junction deviation, Marlin 2 limita la velocidad de los movimientos de menos de 1 mm casi en línea
   (`JD_HANDLE_SMALL_SEGMENTS`: un trozo de 0,5 mm a 500 mm/s² no pasa de 87 mm/s).
3. Dar a cada trozo `E = nominal + adelanto al final − adelanto al comienzo`, con
   `adelanto = K · (área de la línea / área del filamento) · velocidad planificada en ese punto`. Al acelerar empuja de
   más; al frenar empuja de menos o retrae un poco.
4. Volver a planificar los trozos con su E nuevo (el E también cuenta en las uniones) y repetir (3 vueltas).

## 4. La simulación

`tools/research/pressure_advance_sim.py` (Python con NumPy):

```
python3 tools/research/pressure_advance_sim.py [--k s] [--jerk-e mm/s] [--accel mm/s2] [--jd mm] [--piece mm]
```

- Firmware: planificador de trapecios de Marlin con dos variantes de las uniones:
  - jerk clásico en X, Y y E (Marlin 1.x; Marlin 2 con `CLASSIC_JERK`): X/Y 10 mm/s y `DEFAULT_EJERK 5.0`;
  - junction deviation (`--jd`, lo que usa Marlin 2 por defecto): la fórmula de `src/module/planner.cpp` de Marlin
    2.1.x, con E dentro del vector de la unión y la regla de `JD_HANDLE_SMALL_SEGMENTS`. `JUNCTION_DEVIATION_MM` vale
    0,013 mm en el `Configuration.h` de Marlin y 0,08 mm en el ejemplo de Ender-3 de
    [MarlinFirmware/Configurations](https://github.com/MarlinFirmware/Configurations).
- Aceleración 500 mm/s² (`DEFAULT_ACCELERATION` del ejemplo de Ender-3; el de Marlin es 3000).
- Boquilla: el modelo de resorte del punto 2. Línea de 0,45 × 0,2 mm, filamento de 1,75 mm.
- Se mide el ancho depositado a lo largo del camino (1,00 = el planeado), sin el primer y el último milímetro.
- Casos: cuadrado de 20 mm a 60 y a 100 mm/s, y una línea recta 30 → 120 → 30 mm/s.
- El pressure advance del firmware (ideal) da 1,00 en todos los casos; es la referencia.

### Extrusión directa (K = 0,05 s), aceleración 500

Ancho mínimo–máximo (error RMS). El tiempo no cambia (±0,5 %) en ningún caso emulado.

| Firmware | Caso | Sin PA | Emulado en el G-code | Movimientos |
|---|---|---|---|---|
| Jerk clásico, E 5 | Cuadrado a 60 mm/s | 0,45–2,78 (0,285) | 0,89–1,11 (0,025) | 5 → 35 |
| | Cuadrado a 100 mm/s | 0,45–2,98 (0,369) | 0,89–1,09 (0,025) | 5 → 98 |
| | Línea 30 → 120 mm/s | 0,51–1,82 (0,190) | 0,98–1,10 (0,014) | 3 → 31 |
| Junction deviation 0,013 (Marlin 2 por defecto) | Cuadrado a 60 mm/s | 0,45–3,98 (0,361) | 0,79–1,11 (0,044) | 5 → 35 |
| | Cuadrado a 100 mm/s | 0,45–4,24 (0,438) | 0,79–1,13 (0,042) | 5 → 98 |
| | Línea 30 → 120 mm/s | 0,51–1,82 (0,190) | 0,98–1,10 (0,014) | 3 → 31 |
| Junction deviation 0,08 | Cuadrado a 60 mm/s | 0,45–2,81 (0,287) | 0,89–1,11 (0,026) | 5 → 35 |
| | Cuadrado a 100 mm/s | 0,45–2,99 (0,370) | 0,89–1,09 (0,025) | 5 → 98 |
| | Línea 30 → 120 mm/s | 0,51–1,82 (0,190) | 0,98–1,10 (0,014) | 3 → 31 |

**El emulado queda dentro de ±11 % (−21 % en el peor punto con junction deviation 0,013); sin PA el ancho llega a 3 y
4 veces el planeado.**
Con trozos de 0,5 mm y jerk clásico mejora un poco (0,012 / 0,020 / 0,016) con el doble de movimientos.

### Bowden (K = 0,5 s)

Error RMS del ancho y, entre paréntesis, cuánto más tarda la impresión.

| Firmware | Cuadrado 60 mm/s | Cuadrado 100 mm/s | Línea 30 → 120 |
|---|---|---|---|
| Sin PA (jerk clásico) | 0,500 | 0,592 | 0,567 |
| Jerk clásico, E 5 (de fábrica) | 0,736 (+60 %) | 0,455 (+131 %) | 0,424 (+38 %) |
| Jerk clásico, **E 15** | 0,030 | 0,084 (+6 %) | 0,012 |
| Jerk clásico, E 5, **aceleración 250** | 0,023 (+2 %) | 0,154 (+36 %) | 0,193 (+20 %) |
| Junction deviation 0,013 (Marlin 2 por defecto) | 0,383 (+39 %) | 0,324 (+79 %) | 0,201 (+18 %) |
| Junction deviation 0,03 | 0,052 (+1 %) | 0,177 (+28 %) | 0,015 |
| Junction deviation **0,08** | 0,034 (+1 %) | 0,036 (+1 %) | 0,015 |

Con los valores de fábrica el emulado en Bowden **falla**: el planificador del firmware frena para respetar el límite
del extrusor en las uniones, la velocidad ya no es la prevista, el adelanto queda mal puesto (aparecen anchos negativos,
retracciones en medio de la línea) y la impresión se alarga.

## 5. Las condiciones

1. **El límite del extrusor en las uniones.** El adelanto cambia la velocidad del extrusor en
   `K · (área de la línea / área del filamento) · aceleración` donde empieza o termina una rampa, y en el doble donde
   una aceleración pasa directo a frenada (movimientos cortos, esquinas). Ese salto tiene que caber en lo que el
   firmware deja cambiar de golpe:
   - jerk clásico: el jerk E (`M205 E`);
   - junction deviation: E es un eje más del vector de la unión; para trozos en línea, de la fórmula de `planner.cpp`
     sale un salto admitido de ≈ `√(8 · aceleración · junction deviation)`: 7,2 mm/s con 0,013 mm y 500 mm/s²,
     17,9 mm/s con 0,08 mm. Se cambia con `M205 J` (0,01 a 0,3 mm según
     [marlinfw.org, M205](https://marlinfw.org/docs/gcode/M205.html)).

   Con línea de 0,45 × 0,2 mm (área de la línea / área del filamento = 0,037) a 500 mm/s²: directa K = 0,05 pide
   0,9 mm/s (1,9 en las esquinas), entra con los valores de fábrica; Bowden K = 0,5 pide 9,4 (18,7 en las esquinas), no
   entra en jerk E 5 ni en junction deviation 0,013. Qué puede hacer el post-procesador, por orden:
   - subir el límite al comenzar la impresión (`M205 E…` o `M205 J…`) y volverlo al final. Ojo: `M205 J` también deja
     tomar más rápido las esquinas en X/Y (más vibración); `M205 E` solo toca el extrusor;
   - bajar la aceleración (`M204`) donde no entra;
   - compensar solo una parte allí (mejor un poco de PA que un planificador que frena). Como el post-procesador tiene el
     planificador, sabe exactamente dónde pasa.
2. **El planificador del slicer tiene que ser el del firmware**: jerk clásico o junction deviation, aceleraciones y
   límites (`machine_max_*` del perfil de la impresora). El `GCodeProcessor` de PrusaSlicer ya planifica así para
   estimar tiempos (`src/libslic3r/GCode/GCodeProcessor.cpp`): jerk clásico, `machine_max_junction_deviation` con E en
   el vector de la unión y la regla de `JD_HANDLE_SMALL_SEGMENTS`. Se reutiliza, igualando lo que difiere de Marlin: divide
   ese vector por el largo en X/Y/Z, mientras que Marlin lo normaliza contando E, y para trozos en línea con distinto E
   esa diferencia decide si la unión frena. Los valores reales de cada impresora se leen con `M503`.
3. **Más movimientos**: de 7 a 20 veces más líneas en los tramos que aceleran (tabla). Marlin guarda 16 movimientos
   planificados (`BLOCK_BUFFER_SIZE 16`) y, si esa cola se vacía hasta la mitad, alarga los movimientos de menos de
   20 ms (`SLOWDOWN`, `DEFAULT_MINSEGMENTTIME 20000`): un trozo de 1 mm a 100 mm/s dura 10 ms. Desde la tarjeta SD la
   cola debería mantenerse llena; por USB a 115200 baudios y en placas de 8 bits puede vaciarse. **Falta medirlo en una
   impresora real.**
4. **K por filamento**, calibrado con la prueba de pressure advance de Tisma, escribiendo el adelanto en el G-code en
   vez de `M900`.

### Comparado con lo que ya existe

- `PressureEqualizer` de PrusaSlicer (`max_volumetric_extrusion_rate_slope_*`) suaviza los cambios bruscos de caudal
  entre líneas de distinta velocidad; no corrige la presión dentro de cada aceleración. Es complementario.
- [gcode_cflow](https://github.com/gluap/gcode_cflow) (post-procesador de un tercero) busca lo mismo con otro modelo; no
  se copió código: sirve de antecedente.

## 6. Otras funciones de firmwares nuevos

| Función del firmware | ¿En el G-code? | Estado en Tisma |
|---|---|---|
| Arcos G2/G3 | Sí: no usarlos | Ya existe: `arc_fitting` desactivado |
| Retracción del firmware (G10/G11) | Sí: retracción del slicer | Ya existe: `use_firmware_retraction` desactivado |
| Cambio de color M600 | Sí: pausa con el G-code de la impresora | Ya existe: `color_change_gcode` / `pause_print_gcode` |
| Cancelar objetos (M486, Klipper EXCLUDE_OBJECT) | En el host | Ya existe: `gcode_label_objects` (comentarios para OctoPrint) |
| Desfase Z (M851, babystep) | Sí | Ya existe: `z_offset` |
| Corrección de escuadra (M852, `SKEW_CORRECTION`) | Sí: transformación afín en X/Y de todo el G-code | Factible y simple; falta una calibración que mida la escuadra |
| Nivelación con malla (`AUTO_BED_LEVELING_*`, `MESH_BED_LEVELING`) | Parcial: modular Z en las primeras capas con una malla medida a mano | Factible; poco beneficio frente a nivelar la cama y riesgo de choque si la malla está mal |
| Pressure advance (`LIN_ADVANCE`) | **Sí** (este estudio) | Por hacer |
| Input shaping (Marlin 2.1.2 `INPUT_SHAPING_X/Y`, Klipper) | En teoría, suavizando la trayectoria | **No recomendado**: el método conocido en G-code (filtered B-splines de Ulendo) es comercial y no verificamos si tiene patente; además hay que medir las resonancias de cada impresora |

Sobre input shaping en G-code: Ulendo vende la técnica de filtered B-splines como software
([Additive Manufacturing](https://www.additivemanufacturing.media/articles/solving-3d-printings-hidden-problem-vibration),
[3Printr](https://3printr.com/filtered-b-splines-ulendos-answer-to-high-speed-3d-printers-1765670)) y hay un trabajo
académico de compensación por software
([HKUST](https://researchportal.hkust.edu.hk/en/publications/software-based-compensation-of-vibration-induced-errors-of-a-comm/)).
Antes de hacer algo parecido hay que revisar patentes.

## 7. Si se implementa

1. Opción de impresora «Pressure advance en el G-code» (para firmwares sin `LIN_ADVANCE`) y K por filamento.
2. Post-procesador en `libslic3r/GCode/` que reutilice el planificador del `GCodeProcessor`, con la comprobación del
   límite del extrusor en las uniones (y `M205`/`M204` o compensación parcial).
3. Pruebas: el ancho depositado simulado como en este estudio, con G-code real de Tisma.
4. Interfaz: la opción, el K y la calibración de pressure advance adaptada.
5. Documentación y **pruebas en impresoras reales** (directa y Bowden, por SD y por USB).

## Fuentes

- Klipper: [Kinematics](https://www.klipper3d.org/Kinematics.html) (fórmula del pressure advance) y
  [Pressure advance](https://www.klipper3d.org/Pressure_Advance.html) (valores típicos).
- Marlin: [Linear Advance](https://marlinfw.org/docs/features/lin_advance.html),
  [M205](https://marlinfw.org/docs/gcode/M205.html).
- Marlin 2.1.x (https://github.com/MarlinFirmware/Marlin, rama `2.1.x`):
  - `Marlin/Configuration_adv.h`: `//#define LIN_ADVANCE`, `DEFAULT_MINSEGMENTTIME 20000`, `SLOWDOWN`,
    `BLOCK_BUFFER_SIZE 16`;
  - `Marlin/Configuration.h`: `DEFAULT_ACCELERATION 3000`, `//#define CLASSIC_JERK`, `JUNCTION_DEVIATION_MM 0.013`,
    `JD_HANDLE_SMALL_SEGMENTS`;
  - `Marlin/src/module/planner.cpp`: junction deviation y `SLOWDOWN`;
  - `Marlin/src/inc/Conditionals_adv.h`: `HAS_CLASSIC_E_JERK` (en una cartesiana el jerk E solo existe con
    `CLASSIC_JERK`).
- Ejemplo de Ender-3 (https://github.com/MarlinFirmware/Configurations, `config/examples/Creality/Ender-3/CrealityV1`,
  `release-2.1.2.5`): `DEFAULT_ACCELERATION 500`, `JUNCTION_DEVIATION_MM 0.08`, `DEFAULT_EJERK 5.0`.
- PrusaSlicer: `src/libslic3r/GCode/PressureEqualizer.hpp`, `src/libslic3r/GCode/GCodeProcessor.cpp`.
- gcode_cflow: https://github.com/gluap/gcode_cflow
- Input shaping en G-code: los enlaces de la sección 6.
