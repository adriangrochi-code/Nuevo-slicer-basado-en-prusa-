# Impresoras de resina (MSLA)

Tisma hereda de PrusaSlicer el laminado de resina: soportes en árbol o de pilares, pad, vaciado con agujeros de
drenaje y la exportación a la pantalla de la impresora. Formatos de salida:

| Formato | Impresoras | Origen |
| --- | --- | --- |
| SL1 / SL1S | Prusa SL1, SL1S | PrusaSlicer |
| pwmo, pwmx, pwms | Anycubic Photon Mono, Mono X, Mono X 6K, Mono SE | PrusaSlicer |
| **GOO** | **Elegoo Mars 4, Mars 4 Ultra, Mars 4 Max, Mars 5, Saturn 3, Saturn 3 Ultra, Saturn 4** | **Tisma** |

## Formato Elegoo GOO

`src/libslic3r/Format/GooSLA.cpp`, escrito con la especificación que publica Elegoo
([elegooofficial/GOO](https://github.com/elegooofficial/GOO), «Goo Format Spec V1.2») y contrastado con el lector de
UVtools (AGPLv3):

- cabecera de 195 477 bytes con las vistas previas de 116 × 116 y 290 × 290 px en RGB565;
- cada capa con su altura, exposición, esperas, elevación, retracción y PWM de la luz;
- imagen de la capa en RLE de 8 bits (con antialiasing) y suma de control;
- las primeras «capas atenuadas» del perfil (5 en los perfiles Elegoo) salen con la exposición de fondo.

Los movimientos que no tiene PrusaSlicer van en las **notas del material**, una clave por línea (las mismas que las
de Anycubic: distancias en mm, velocidades en mm/s, esperas en s):

```
LIFT_DISTANCE=5
LIFT_SPEED=1.6667
BOTTOM_LIFT_DISTANCE=7
BOTTOM_LIFT_SPEED=1.3333
RETRACT_SPEED=2.5
DELAY_BEFORE_EXPOSURE=2.5
WAIT_AFTER_CURE=1
WAIT_AFTER_LIFT=0
ANTIALIASING=8
LIGHT_PWM=255
BOTTOM_LIGHT_PWM=255
```

## Perfiles Elegoo (`resources/profiles/ElegooSLA.ini`)

- Pantalla (resolución y tamaño) y altura máxima: especificaciones publicadas por Elegoo, cruzadas con los perfiles
  de PrusaSlicer de UVtools (`PrusaSlicer/printer`).
- Elevación, retracción y esperas por modelo: valores de esos perfiles de UVtools.
- Resina estándar genérica a 0,05 mm: 2,5 s y 30 s de fondo, dentro del rango que Elegoo publica para su resina
  estándar (2,5 a 3 s y 25 a 35 s). **Es un punto de partida**: con cada resina hay que imprimir una prueba de
  exposición.
- La **Mars 5 Ultra** y la **Saturn 4 Ultra** usan el formato CTB cifrado y no están incluidas; la Mars 4 DLP
  tampoco, porque no pude confirmar sus exposiciones.
- Los modelos no tienen foto en el asistente (no se usan imágenes de Elegoo).

## Cómo se probó

- `tests/sla_print/sla_goo_tests.cpp`: codificación y decodificación (incluidos los bloques de diferencia que
  escriben otros programas), suma de control, exportación de un cubo laminado recorriendo todo el archivo, y carga
  del paquete Elegoo con laminado y exportación con la Mars 5.
- Interfaz: asistente (familias Mars y Saturn), laminado con soportes y pad, exportación a `.goo` y lectura del
  archivo resultante (520 capas, 4098 × 2560, fondo de 30 s con elevación de 7 mm a 80 mm/min).
- **Falta probarlo en una impresora real.** Si la impresora rechaza el archivo o la pieza sale espejada, avisar
  con el modelo y la versión de firmware.

En modo resina, Ingeniería y Estructuras no están disponibles: el análisis estructural es solo para FDM.
