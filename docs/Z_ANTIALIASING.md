# Anti-aliasing en Z (Tisma)

Opción `zaa_enabled` (Configuración de impresión > Capas y perímetros > Calidad). Con capas planas una cara superior
inclinada sale en escalones: cada capa está entera o no está. Donde la superficie del modelo cruza una capa, Tisma baja
hasta la superficie las extrusiones visibles (relleno superior, planchado, perímetros, perímetros en voladizo y relleno
de huecos) y reduce su flujo al espesor que queda. La cara superior sigue al modelo en lugar de a los escalones.

## Cómo funciona

- Filtro del G-code de cada capa (`src/libslic3r/ZAntiAliasing.cpp`), después del enfriamiento, como el de capas no
  planares. Lee las etiquetas `;TYPE:` y `;HEIGHT:` que PrusaSlicer ya escribe para el visor.
- Cada movimiento con extrusión de esos tipos se parte cada 0,3 mm. En cada punto se lanza un rayo hacia arriba desde el
  piso de la capa contra el modelo (árbol AABB): si la primera cara encontrada mira hacia arriba y está dentro de la
  capa, la boquilla baja hasta ella.
- Nunca baja más allá del espesor mínimo (`zaa_min_thickness`, 25 % de la capa por defecto) y nunca sube por encima de
  la capa. El flujo de cada tramo se escala por el espesor que queda.
- Antes de un viaje o de otra extrusión la boquilla vuelve a la altura de la capa.
- Funciona con E relativo o absoluto, con varios objetos e instancias. No se combina con modo jarrón, impresoras de cinta
  ni ajuste de arcos (validación). Desactiva los perímetros escalonados.
- **Con capas no planares** (cualquier modo): el filtro trabaja antes del no planar, sobre el G-code plano del espacio
  de laminado y contra la malla deformada (la que cortan esas capas planas). Después el filtro no planar lleva todo a
  las capas curvas, incluidas las bajadas. Así las caras superiores siguen al modelo también dentro de las capas
  curvas.
- **Medido** en una cuña de 40 × 40 mm con la cara superior de 5 a 9 mm y capas de 0,2 mm. Se mide la dispersión de la
  cara superior impresa respecto del modelo: percentiles 5–95 de la extrusión más alta de cada celda de 1 mm, con las
  extrusiones muestreadas cada 0,25 mm.
  - En plano pasa de 0,125 a 0,088 mm.
  - Con capas no planares en ondas pasa de 0,17 a 0,10 mm.
  - Una primera medición que solo miraba los extremos de cada movimiento daba 0,50 → 0,06 mm, y no es válida.
  - Test: `Z anti-aliasing works within non-planar layers`.

## Fuente

La idea es la de Hai-Chuan Song, Nicolas Ray, Dmitry Sokolov y Sylvain Lefebvre, *Anti-aliasing for fused filament
deposition*, Computer-Aided Design 89 (2017) 25–34 ([arXiv:1609.03032](https://arxiv.org/abs/1609.03032)). Ellos
reparten la desviación en ±media capa entre varias capas; esta es una versión propia más simple que solo baja la última
capa de cada escalón. No se copió código de otras implementaciones.

## Pruebas

`tests/fff_print/test_zaa.cpp`: una cuña con techo a 2,9°: el relleno superior baja dentro de su capa (nunca por
encima, nunca debajo del 25 %), usa menos material, E absoluto y relativo coinciden, y la validación rechaza las
combinaciones incompatibles.

## Límites

- Solo baja, no sube: el escalón se suaviza desde arriba (la última capa), no se reparte entre capas como en el paper.
- Bajar la boquilla junto a una línea ya impresa a la altura de la capa puede rozarla en pendientes fuertes; el efecto
  útil es en pendientes suaves, donde los escalones más se notan.
- Sin probar todavía en una impresora real.
