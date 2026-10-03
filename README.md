# NPS — Slicer no planar basado en PrusaSlicer

Slicer de capas **no planas** que usa PrusaSlicer como motor de corte, pensado para
dos objetivos:

- **Resistencia homogénea**: capas onduladas que encajan entre sí, de modo que la
  carga en Z ya no depende sólo de la adhesión entre capas planas.
- **Velocidad**: caudal volumétrico constante en todo el recorrido curvo y relleno
  impreso al caudal máximo del hotend.

## Cómo funciona

```
STL ──► deformación ──► PrusaSlicer (corte planar) ──► transformación inversa ──► G-code no planar
        z' = z - D          (perímetros, relleno,         z = z' + D, E·J,
                             retracciones…)                 F con caudal constante
```

1. **Deformar la malla.** Cada capa real es la superficie `z = s + D(x,y,z)` con
   `D = g(x,y) · ramp(z)`. La malla se subdivide (sin T-junctions) y se lleva al
   "espacio de corte" `z' = z - D`, donde esas superficies son planos.
2. **Cortar con PrusaSlicer.** Se corta la malla deformada con tu perfil de
   PrusaSlicer. Así se aprovecha todo su trabajo: perímetros, relleno, costuras,
   retracciones, refrigeración, etc.
3. **Transformar el G-code de vuelta.** Cada movimiento se trocea (`--seg-len`) y se
   devuelve al espacio real. La deformación es exactamente invertible, así que la
   geometría final coincide con el STL original.

### Física de la corrección de extrusión

El volumen entre dos capas sobre un área XY `A` es `A · h · J`, con
`J = dz/dz' = 1 / (1 − g · ramp'(z))`. Por eso:

- **Extrusión:** `E_real = E · J`. Las retracciones y los wipes no se escalan.
- **Velocidad:** `F_real = F · (L_3D / L_XY) / J`. Así el caudal volumétrico es el
  mismo que había decidido PrusaSlicer, y los cordones salen uniformes aunque la
  capa sea más gruesa o más fina.
- `printer.max_flow`, `printer.max_speed` y los límites de Z acotan el resultado.
  `homogeneity.flow_policy` decide la velocidad de cada tramo (ver Homogeneidad).

`D` sólo depende de `z` a través de la rampa, así que `J = 1` fuera de las zonas de
transición. Las capas onduladas mantienen un espesor constante y sólo cambian de
forma.

### Modos (`--mode`)

| modo | forma de capa | uso | impresora |
|---|---|---|---|
| `wave` (por defecto) | `A·sin(kx)·sin(ky)` (huevera) | resistencia homogénea entre capas, piezas anchas | 3 ejes, con pendiente ≤ `--max-slope` |
| `wave --wave-pattern ridges` | `A·sin(k·u)` (crestas) | paredes y probetas estrechas | 3 ejes |
| `conical` | cono `r·tan(θ)` | voladizos sin soportes | mejor en 4/5 ejes; en 3 ejes sólo con ángulos pequeños |
| `planar` | plano | referencia / depuración | cualquiera |

Antes de cortar, el programa comprueba dos cosas y se detiene si alguna falla
(`--force` para ignorarlo):

- que el espesor real de capa quede entre ×0.6 y ×1.4 del nominal (si no, alarga
  `--ramp`);
- que la pendiente de las capas no supere la que tolera tu boquilla
  (`--max-slope`, 20° por defecto), para evitar colisiones en impresoras de 3 ejes.

### Límites del eje Z (importante en impresoras de 3 ejes)

En una capa curva el eje Z se mueve todo el tiempo. Su velocidad es
`v_z = v_xy · pendiente` y su aceleración `a_z ≈ v_xy² · |z''|`. En máquinas con Z
por husillo (Creality y similares) son límites duros: si se superan, el firmware
frena el movimiento entero o el motor pierde pasos. Con `--z-max-speed` y
`--z-max-accel`, NPS reduce F tramo a tramo para respetarlos, y el tiempo
estimado ya lo incluye. **Ondas más largas y más bajas cuestan menos tiempo.**

## Uso

```bash
pip install -e .[dev]            # requiere PrusaSlicer (CLI) en el PATH o --prusa RUTA

nps printers                                     # presets incluidos
nps config --printer generic_marlin > mi.toml    # vuelca la configuración completa para editarla
nps slice pieza.stl -o pieza.gcode -c mi.toml
nps slice pieza.stl -o pieza.gcode --printer cr5proh --pattern twisted --set print.infill=40
```

### Configuración universal (TOML)

Prioridad: valores por defecto < `--printer` < `-c fichero.toml` (en orden) <
atajos de la CLI < `--set seccion.clave=valor`. Las claves desconocidas dan error.

| sección | contenido |
|---|---|
| `[printer]` | cama, altura, boquilla, firmware (`marlin2`, `klipper`…), velocidad y caudal máximos, límites de Z, retracción, G-code inicial/final |
| `[material]` | temperaturas y ventilador |
| `[print]` | altura de capa, perímetros, relleno, velocidad de referencia |
| `[nonplanar]` | `mode` (wave/conical/planar), `pattern` (egg/ridges/twisted), amplitud, longitud de onda, giro, rampa |
| `[homogeneity]` | `flow_policy`, `uniform_flow`, `uniform_exclude`, `feature_flow`, `isotropic_print` |
| `[output]` | centro, resolución de segmentación |
| `[prusa]` | cualquier clave de PrusaSlicer, tal cual (se aplica la última) |
| `prusa_profiles` | perfiles `.ini` propios de PrusaSlicer que cargar encima del generado |

A partir del TOML se genera el perfil de PrusaSlicer, así que cualquier impresora
se añade con un `.toml` (ver `nps/printers/`). El perfil nunca envía
`M201`/`M203`: se respetan los límites de tu firmware.

### Homogeneidad

Cada corte imprime y guarda (`pieza.report.json`) métricas comparando la versión
planar de PrusaSlicer con la de NPS: variación del caudal, variación del espesor
de capa, trabazón entre capas y anisotropía en XY. Ver `nps/homogeneity.py`.

- `flow_policy = "uniform"` (por defecto): todo sale al mismo caudal volumétrico
  (salvo perímetro exterior, voladizos, puentes y gap fill), así la soldadura
  entre cordones es igual en toda la pieza.
- `isotropic_print = true`: relleno gyroid, ancho de cordón único, costura
  aleatoria y solape de relleno del 25 %.
- `pattern = "twisted"`: crestas que giran con la altura, para que el encaje
  entre capas actúe en todas las direcciones.

## Creality CR-5 Pro H

`--printer cr5proh` carga `nps/printers/cr5proh.toml`:

- cama de 300×225×380, boquilla de 0.4, Bowden con retracción de 5 mm;
- BL-Touch con `M420 S1`, para usar la malla guardada (cámbialo por `G29` si no la tienes);
- PLA a 205/60 °C, 100 mm/s como máximo, caudal de 10 mm³/s;
- límites de Z del firmware de serie (`M203 Z5`, `M201 Z100`), que NPS usa en su
  limitador; el perfil no envía `M201`/`M203`, así que se respetan los de tu máquina;
- valores por defecto de la onda: 0.5 mm de amplitud y 20 mm de longitud.

```bash
nps slice pieza.stl -o pieza.gcode --printer cr5proh
nps slice pieza.stl -o pieza.gcode --printer cr5proh -c mi_filamento.toml   # tus ajustes encima
```

Si tu firmware tiene otros límites (consúltalos con `M503`), cámbialos con
`--set printer.z_max_speed=…` y `--set printer.z_max_accel=…`. Subirlos acorta mucho las impresiones no
planas, pero compruébalo antes: el Z de husillo puede perder pasos.

### Prueba de resistencia en Z (G-code listo en `examples/cr5proh/`)

Probeta "hueso de perro" impresa en vertical: mordazas de 30 mm y zona de ensayo
de 20×5×30 mm, así que se rompe entre capas. Se genera con
`examples/make_test_models.py` → `probeta_traccion_z.stl`.

| fichero | capas | tiempo de movimiento estimado |
|---|---|---|
| `probeta_planar.gcode` | planas (referencia) | 40.7 min |
| `probeta_crestas_l20.gcode` | crestas λ=20 mm, A=0.5 mm | 50.7 min (+25 %) |
| `probeta_crestas_l15.gcode` | crestas λ=15 mm, A=0.5 mm | 63.3 min (+54 %) |

Con Z a 5 mm/s y 100 mm/s², la onda de λ=10 mm costaría +115 %, así que no se incluye.

![capas de la probeta](docs/img/cr5_probeta_capas.png)

Protocolo sugerido:

1. Imprime primero un cubo (`--printer cr5proh`) y vigila la primera capa
   ondulada a ~5 mm de altura: no debe haber roces ni saltos de Z.
2. Imprime al menos 3 probetas de cada tipo con el mismo filamento y el mismo día.
3. Tracciona en Z (máquina de ensayos o un montaje con cubo y báscula colgante)
   y anota la carga de rotura y dónde rompe.
4. Las crestas ganan si rompen con más carga **y** la grieta sigue la onda en vez
   de un plano limpio.

### Resultados de referencia

Cubo de 20 mm, preset `generic_marlin` (Z a 5 mm/s y 100 mm/s²), PrusaSlicer 2.7.2:

| estrategia | CV de caudal | trabazón (>5°) | tiempo de movimiento |
|---|---|---|---|
| planar, caudal de PrusaSlicer (`preserve`) | 0.234 | 0 % | 15.1 min |
| planar + `uniform` | **0.161** | 0 % | 14.7 min |
| wave egg + `uniform` | 0.282 | **74 %** | 21.5 min |
| wave ridges + `uniform` | 0.356 | 68 % | 22.1 min |
| wave twisted + `uniform` | 0.329 | 61 % | 21.5 min |

Lectura honesta: con un Z lento, el limitador de Z frena muchos tramos curvos y
rompe la uniformidad del caudal. La trabazón entre capas sube mucho, pero el
caudal se vuelve menos uniforme. Con un Z más rápido (Klipper, CoreXY) ese
conflicto desaparece en buena parte.

Los tiempos son estimaciones cinemáticas que no tienen en cuenta las
aceleraciones. Los modelos de prueba se generan con
`PYTHONPATH=. python examples/make_test_models.py`.

## Estado y hoja de ruta

Esto es la **fase 1**: un pipeline en Python sobre la CLI de PrusaSlicer. Funciona con
2.7–2.9 y debería funcionar con 3.0 (ahora en alpha) sin cambios, porque sólo
depende de la CLI y del G-code de texto. Ver [docs/arquitectura.md](docs/arquitectura.md).

- [ ] Validación física: ensayos de tracción en Z, planar frente a `wave`
      (probeta vertical incluida).
- [ ] Campos guiados por tensiones (FEA) en lugar de una onda fija.
- [ ] Tapa superior que siga la superficie del modelo (acabado no planar real).
- [ ] Detección de colisiones con la geometría real del cabezal (no sólo la pendiente).
- [ ] Desfase de fase entre capas alternas (entrelazado tipo "brick layers").
- [ ] Fase 2: integrar la deformación en el núcleo C++ de PrusaSlicer (`libslic3r`)
      como etapa de *slicing* y *G-code export*, con vista previa en la GUI.

## Tests

```bash
pytest          # el test de extremo a extremo se omite si no hay PrusaSlicer
```
