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
- **Colisiones del cabezal (Tisma):** en `Printer Settings > General > Print head` se indican la altura libre
  bajo el cabezal (de la punta de la boquilla a lo más bajo del bloque calefactor, el ducto o el sensor) y el radio
  de esa zona. Antes de laminar se recorren las capas curvas: en cada punto, la pieza ya impresa (casco convexo de
  la malla hasta esa altura, con la superficie de la capa actual como techo) no puede subir más que la altura libre
  dentro de ese radio. Si sube, se rechaza con la altura, la subida y la distancia. Valores por defecto: 3 mm y
  15 mm (conservadores; conviene medir la impresora). Con altura 0 no se comprueba. La boquilla misma la cubre la
  pendiente máxima de capa.

### Calibración (menú Calibración, Tisma)

Lo que el laminado no planar necesita saber de la impresora se mide con estas pruebas (las impresas con capas
curvas usan su propia deformación, no los ajustes no planares del perfil). El **asistente de calibración no planar**
las ordena y aplica los resultados a los perfiles:

- **Pendiente máxima en dos pasos.** Paso 1: **galga estática de ángulo libre**, impresa plana: rampas de 30 mm cuyo
  ángulo crece por pasos; con la impresora fría se baja la boquilla sobre cada rampa (la rampa está libre si solo la
  toca la punta) y se repite con la galga girada 90°, 180° y 270°. Paso 2: la prueba impresa de pendiente, preparada
  por el asistente de 10° por debajo a 5° por encima del ángulo libre, cada 2,5°. Se aplica el menor de los dos
  valores menos la tolerancia (3° por defecto); con solo el paso 1, el asistente avisa que no está confirmado.

- **Pendiente máxima de capa** (`calib_mode = nonplanar_slope`): bloque con crestas de 8 mm de longitud de onda cuya
  pendiente crece por pasos a lo largo de X. El último paso sin raspado ni líneas despegadas da
  `nonplanar_max_slope`. La comprobación de pendiente no se aplica a esta prueba; las de espesor de capa y colisión
  del cabezal sí.
- **Velocidad del eje Z** (`calib_mode = nonplanar_z_speed`): cilindro con ondas; la velocidad Z permitida a los
  movimientos curvos sube por pasos desde donde las ondas están completas (capas planas + 8 mm de transición). El
  último paso sin capas aplastadas ni pasos perdidos da la velocidad máxima de Z de los límites de la máquina.
- **Galga de altura libre del cabezal**: escalera (sin capas curvas) que se desliza bajo el cabezal frío con la
  boquilla apoyada en la cama: da `nonplanar_head_clearance_height`; el radio se mide con una regla.

Siguiente (pendiente): probetas plana y no planar para medir la ganancia real de resistencia entre capas y
calibrar con ella el análisis de Ingeniería.

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
