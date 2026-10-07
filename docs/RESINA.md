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

## Interfaz en modo resina

Con una impresora de resina se ocultan las funciones que solo sirven para FDM:

- las pestañas **Ingeniería** y **Estructuras** (el análisis estructural y el relleno son de FDM); si estaban
  abiertas al cambiar de impresora, se vuelve a Preparar;
- las calibraciones de FDM: el menú y la pestaña **Calibración** pasan a mostrar las de resina.

Ya estaban limitadas a FDM: las capas no planares, los voladizos en arco y el resto de los ajustes de impresión
de FDM (están en la pestaña de impresión FDM, que en resina no aparece), las herramientas de pintar costuras,
piel difusa y multimaterial, y los ajustes por plancha. **Dispositivos** queda, porque también maneja impresoras
por red (PrusaLink, OctoPrint, ...).

## Calibraciones de resina

Calibración (pestaña o menú) con una impresora de resina:

- **Torre de exposición** (solo impresoras GOO, que guardan la exposición de cada capa): una sola impresión con una
  base y una banda por tiempo de exposición (por defecto de 1,5 a 3,5 s cada 0,25 s, bandas de 2 mm). Cada banda
  tiene agujeros horizontales de 0,4, 0,6, 0,8, 1,0 y 1,4 mm (se cierran con exposición de más) y aletas de 0,15,
  0,25, 0,35 y 0,5 mm atrás (faltan o se doblan con exposición de menos); una ranura al frente separa las bandas. Se
  imprime sobre la placa, sin soportes ni pad. La banda elegida se aplica con «Aplicar un resultado de calibración»
  (tiempo de exposición de la resina). Las capas de fondo mantienen su exposición.
- **Bloque de medidas** de 30 × 30 × 10 mm y **Corrección dimensional de la resina**: con X, Y y Z medidos calcula la
  corrección de escala del material (`material_correction_x/y/z` = actual × medida deseada / medida impresa).
