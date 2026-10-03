# Slicer no planar basado en PrusaSlicer 2.9

Base: PrusaSlicer 2.9.6, con estos añadidos.

## Capas no planares (`Print Settings > Non-planar`)

El objeto se deforma antes de laminarse. Después pasa por el proceso planar normal de PrusaSlicer y,
al final, el G-code se transforma punto a punto para que las capas queden curvas:

- **Capas reales:** `z = s + D(x, y, z)`, con `D = g(x, y, z) · rampa(z)`.
- **Formas `g`:**
  - ondas: huevera (`egg`), crestas (`ridges`), crestas giradas (`twisted`);
  - cónica (`conical`).
- **Rampa:** las primeras capas son planas (`nonplanar_flat_below`) y la deformación crece de forma
  suave (`nonplanar_ramp_height`). Con `nonplanar_flat_top` la superficie superior también queda plana.
- **Extrusión:** se escala con el jacobiano `J` (el grosor real de la capa).
- **Flujo y velocidad:** el avance se corrige para conservar el caudal volumétrico. Con la política
  `uniform` se fija un caudal constante para que el material se deposite de forma homogénea; los
  perímetros externos, voladizos, puentes y gap fill quedan fuera de esa regla.
- **Límites del eje Z:** la velocidad se limita para que Z no supere los límites de máquina
  (`machine_max_feedrate_z`, `machine_max_acceleration_z`; por ejemplo, M203 Z5 y M201 Z100 en una CR-5 Pro H).
- **Validación:** se rechazan los casos en los que
  - `J` sale de [0.6, 1.4];
  - la pendiente de capa supera `nonplanar_max_slope`;
  - hay más de una instancia;
  - está activo el modo vaso, los arcos G2/G3 o la E absoluta.

## Impresión por USB (`Configuration > Print via USB...`)

Envío por puerto serie con el protocolo de host de Marlin:

- líneas numeradas con checksum, control de flujo con `ok` y reenvíos (`Resend:` / `rs`);
- soporte de `busy:`;
- lectura de temperaturas;
- pausa, reanudación y cancelación (M108 y apagado de calentadores);
- consola de comandos.

Funciona con Marlin, firmware de Prusa y RepRapFirmware.

## Menos soportes y menos material

- **Arc overhangs** (`Layers and perimeters > overhang_arcs`): los voladizos que no se pueden puentear
  se rellenan con arcos concéntricos que crecen desde el borde apoyado. Cada arco se apoya en el anterior,
  así que los voladizos pronunciados se imprimen sin soportes. Se basa en el algoritmo de Steven McCulloch.
- **Relleno denso bajo superficies superiores** (`Infill > infill_dense`, idea de SuperSlicer): solo la
  capa de relleno que está justo debajo de las superficies sólidas superiores se imprime más densa
  (`infill_dense_density`), y solo donde esas superficies lo necesitan. Así se puede bajar mucho la
  densidad del relleno general (5–10 %) sin que el techo se hunda.

## Compilar en Linux (Ubuntu 24.04) con las librerías del sistema

```
build-utils/build_linux_system_libs.sh --deps   # instala las dependencias con apt y compila
build/src/prusa-slicer
```

Tests: `ctest --test-dir build`. Etiquetas propias: `[NonPlanar]`, `[USBPrinter]`, `[ArcOverhangs]`, `[DenseInfill]`.
