# Impresoras de cinta (belt)

Estado: **versión 1, probada con pruebas automáticas; falta probarla en una impresora real.**

## Uso

Configuración de impresora → General → **Impresora de cinta**:

| Ajuste | Clave | Valor |
|---|---|---|
| Impresora de cinta | `belt_printer` | sí / no |
| Ángulo del cabezal | `belt_angle` | 10–80°, por defecto 45° |

La pieza se coloca sobre la cinta como sobre una cama (X a lo ancho, Y a lo largo de la cinta, Z hacia arriba). Conviene
definir la cama larga en Y (la cinta es infinita).

## Método

El de Cura BlackBelt (LGPLv3), reimplementado en C++ (`src/libslic3r/BeltPrinter.hpp/.cpp`), sin copiar código:

1. **Laminado** (`Print::apply`): se gira una copia del modelo alrededor de X por el ángulo del cabezal, para que los
   planos de impresión (paralelos al cabezal) queden horizontales, y se desplaza para que el punto más bajo quede en la
   primera capa. Se lamina con el motor de siempre.
2. **G-code** (`GCodeGenerator::_do_export`): entre el G-code inicial y el final, cada movimiento G0/G1 se convierte a
   los ejes de la máquina:
   - X: a lo ancho de la cinta.
   - Y: la cinta. Queda fija durante cada capa y avanza `altura de capa / sen(ángulo)` entre capas.
   - Z: a lo largo del cabezal inclinado (Z = 0 es la superficie de la cinta).
   El G-code inicial y final se escriben tal cual, en los ejes de la máquina. El G-code personalizado de capa y de
   cambio de herramienta se interpreta en el sistema de laminado y se convierte.
3. **Vista previa** (`GCodeProcessor::finalize`): los tiempos se calculan con los ejes de la máquina; las trayectorias se
   muestran en el mundo, sobre la cinta.
4. **FEA**: el eje débil del material (adhesión entre capas) se orienta según la normal de las capas inclinadas,
   `(0, sen θ, cos θ)`. La recomendación de orientación se desactiva (está pensada para camas planas).

## No disponible en la cinta (versión 1)

Soportes, balsa, borde, falda, torre de purga, impresión secuencial, modo vaso y arcos (G2/G3): se desactivan al
laminar. La comprobación de altura máxima no se aplica (la cinta no tiene máximo a lo largo).

Pendiente:
- Ajustes propios de las zonas que tocan la cinta (velocidad y flujo como primera capa en cada capa): hoy solo la
  primera capa del laminado usa los ajustes de primera capa.
- Soportes que apoyen sobre la cinta.
- Sentido de avance de la cinta configurable (hoy la pieza avanza hacia +Y).

## Pruebas

`tests/fff_print/test_belt_printer.cpp`:
- Ida y vuelta mundo → laminado → máquina → mundo (30°, 45°, 60°).
- Conversión de líneas de G-code.
- Cubo de 20 mm a 45° y 30°: Y fija dentro de cada capa, la cinta avanza entre capas, las extrusiones vuelven a formar
  el cubo de 20 × 20 × 20 mm sobre la cinta y nada queda por debajo de la cinta.

`src/libtisma_fea/test/test_fea.cpp`: «Belt printer inclines the layers of the analysis».
