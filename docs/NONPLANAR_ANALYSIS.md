# Laminado no planar y de 5 ejes: análisis para resistencia mecánica

Fecha: 2026-10-06. Objetivo del usuario: usar el no planar para **mejorar la resistencia mecánica**. Impresora de
referencia: CR-5 Pro H (3 ejes).

## Opciones existentes

| Opción | Tipo | Máquina | Qué mejora | Licencia / derechos | Uso para Tisma |
|---|---|---|---|---|---|
| S³-Slicer (Zhang et al., SIGGRAPH Asia 2022) | Capas curvas por deformación, dirección de impresión local optimizada (sin soportes, **refuerzo por tensiones**, superficie) | Multi-eje (robot) | Las capas siguen las tensiones principales; el trabajo relacionado informa hasta 6,35× de carga | BSD-3 (código C++/Qt/MKL) | Referencia de la formulación; el principio (deformar, laminar plano, volver) es el mismo que ya usa Tisma |
| Neural Slicer (2024) | Igual que S³, con campo neuronal implícito | Multi-eje | Refuerzo alineando la dirección de impresión con la tensión principal | Artículo | Ideas; demasiado pesado para un laminador de escritorio |
| CurviSlicer (Inria, 2019) | Capas suavemente curvas optimizadas con límites de pendiente y espesor | **3 ejes** | Superficie, menos porosidad interna | **AGPL-3.0** (compatible con Tisma); OSQP + TetWild | **La formulación correcta para 3 ejes**: optimizar un campo de altura con límites de pendiente y de espesor de capa |
| Active-Z (Penn State, 2017) | Capas sinusoidales (amplitud, frecuencia, orientación) alineadas con la tensión | 3 ejes | Resistencia | Artículo | Lo que ya hace el modo «ondas» de Tisma; falta elegir los parámetros según el FEA |
| Relleno y paredes no planares entrelazados (Tenger, 2024) | Senoides en Z, con fase alterna entre líneas vecinas | 3 ejes | Unión entre capas (sin planos de rotura limpios) | Scripts de posprocesado (sin verificar licencia) | Reimplementar en el motor (idea, no código) |
| Brick layers / perímetros escalonados | Perímetros a alturas alternas | 3 ejes | Unión entre capas | **Patente US11331848B2 (ADDMAN, hasta 2040)** en EE. UU. | Evitar o dejar desactivado hasta una consulta legal |
| Z-pinning (ORNL) | Pines de material que atraviesan varias capas | 3 ejes | Resistencia en Z > 3,5× (PLA) | **Patente en trámite** | Evitar |
| Fractal Cortex (2025) | 3+2 ejes: trozos laminados planos en distintas direcciones | 5 ejes con cama giratoria | Sobre todo menos soportes | GPL-3.0 (Python) | Futuro, si hay máquina de 5 ejes |
| Open5x | Conformal slicing en Grasshopper | 5 ejes | Superficies curvas | Requiere Rhino (pago) | No |
| Slicer4RTN | Cónico para boquilla inclinada giratoria | 4 ejes | Voladizos | Código abierto | El modo cónico de Tisma ya cubre el caso de 3 ejes |
| Z anti-aliasing | Micro no planar en superficies superiores | 3 ejes | Solo acabado | Fork de Bambu Studio / OrcaSlicer 2.4 | Útil, pero no para resistencia |

## Conclusión

En 3 ejes la boquilla solo admite capas con poca pendiente (unos 20–30°), así que no se pueden girar las capas
hasta seguir del todo la tensión como en S³-Slicer. Lo que más resistencia da dentro de esos límites:

1. **Capas curvas guiadas por el FEA** (formulación de CurviSlicer con el objetivo de S³-Slicer): el análisis de
   Ingeniería da las tensiones principales; se busca un campo de altura suave cuyas capas eviten quedar
   perpendiculares a la tracción principal, con los límites de pendiente, espesor y colisión del cabezal. Encaja en
   el flujo actual de Tisma (deformar, laminar plano, volver) sin cambiar el motor.
2. **Entrelazado no planar del relleno y las paredes** (senoides con fase alterna): mejora la unión entre capas en
   toda la pieza, con poco riesgo y sin patentes conocidas.
3. **FEA con la orientación local de las capas**, necesario para medir 1 y 2 y elegir los parámetros.
4. 5 ejes (3+2 al estilo Fractal Cortex) más adelante, con una máquina de referencia.

Las mejoras citadas (6,35×, 3,5×, +31–67 % de corte entre capas) son de otras máquinas, materiales y ensayos; en
Tisma hay que medirlas con probetas propias.

## Ajuste al cabezal (Tisma)

En la interfaz, las capas no planares tienen **solo dos variables**, las dos medidas del cabezal (Configuración de
impresora > General > Cabezal): el **ángulo libre** (en X y en Y) y la **holgura** entre la punta de la boquilla y el
cabezal. En la configuración de impresión solo se elige el tipo de capas (`nonplanar_mode`). Todo lo demás sale de esas
dos medidas:

- pendiente de las capas ≤ ángulo libre;
- ondas: suben como mucho la holgura de valle a cresta, y su longitud de onda es π · holgura / tan(ángulo libre menor)
  (entre 4 y 100 mm), para que lleguen a la vez a los dos límites;
- cono: el ángulo libre menor;
- después se reduce lo necesario para el espesor de capa y para que el cabezal no choque con lo ya impreso.

Las demás opciones (`nonplanar_fit_head`, amplitud, longitud de onda, transición, flujo...) siguen en los perfiles y en la
línea de comandos con sus valores por defecto, para los tests y las calibraciones; `nonplanar_fit_head = 0` vuelve a
los valores puestos a mano.

Detalle de las dos medidas:

- **Ángulo libre en X y en Y** (`nonplanar_head_free_angle_x` / `_y`), medidos con la galga estática de ranuras en
  V: se imprime plana, con ranuras de ángulo de apertura φ creciente y filas de profundidad H/3, 2H/3 y H (todas con la
  misma cara superior), un bloque con las paredes inclinadas a lo largo de X y otro a lo largo de Y. Con la impresora
  fría se baja la boquilla en cada ranura: la más angosta que entra sin que toque nada más da φ, y el ángulo libre a esa
  altura (la profundidad de la fila) es (180° − φ) / 2. El asistente guarda cada par altura:ángulo en el perfil del
  cabezal (`nonplanar_head_profile`, una vez por fila) y el de la fila leída en los ángulos libres. El cabezal no es simétrico (ventilador de capa, sensor de nivelación). Se modela como un
  cono elíptico alrededor de la boquilla: una capa con gradiente (gx, gy) cabe si (gx / tan αx)² + (gy / tan αy)² ≤ 1.
  Las crestas que suben a lo largo de X usan αx, a lo largo de Y usan αy, en otra dirección la combinación; la
  huevera y las crestas torcidas, el menor de los dos; el cono, el menor con el sentido elegido. Sin medir (0) se usa
  la pendiente máxima de la configuración de impresión.
- **Altura libre** (`nonplanar_head_clearance_height`): la onda sube como mucho esa altura de valle a cresta
  (amplitud ≤ altura / 2).
- El espesor de capa sigue dentro de 60–140 % del nominal: si la transición es corta, la amplitud se reduce lo justo.

La comprobación de colisiones con el perfil medido sigue igual. El modelo de cono elíptico es una aproximación propia
de Tisma a partir de las dos medidas; no sale de una fuente externa.

## Voladizos: no planar solo donde hace falta soporte (Tisma)

Modo «Voladizos (menos soportes)» (`nonplanar_mode = overhangs`): la pieza se imprime con capas planas y las capas se
inclinan solo en las zonas con voladizos más inclinados que «Voladizo imprimible» (`nonplanar_overhang_threshold`,
desde la vertical, 45° por defecto). Objetivo: menos soporte y menos material.

- **Dirección**: las capas *bajan* hacia el voladizo. Así la normal de la capa se inclina hacia donde crece el voladizo
  y el voladizo sobresale menos respecto de la capa. En el espacio de laminado (donde PrusaSlicer genera los soportes)
  esto es un cizallamiento: la cotangente del ángulo del voladizo desde la vertical crece en tan(b), con b la inclinación
  de la capa. Se apunta 5° por debajo del ángulo imprimible.
- **Forma**: por cada grupo de voladizos (zona, por altura) se toma la sección de la pieza donde empieza el voladizo (lo
  ya impreso debajo); dentro de ella las capas siguen planas y fuera bajan con la distancia a esa sección, medida en la
  métrica del cabezal (pendiente ≤ ángulo libre en X y en Y, cono elíptico), hasta el punto más lejano del voladizo. Se
  calcula en una grilla (transformada de distancia exacta de Felzenszwalb y Huttenlocher, *Distance Transforms of
  Sampled Functions*, Theory of Computing 8, 2012) y se suaviza.
- **Cabezal**: además de los ángulos libres, la pendiente queda limitada por la holgura del cabezal (lo ya impreso no
  puede subir más de lo que permite el bloque calefactor a esa distancia: altura libre / radio, o el perfil medido con
  la galga de ranuras en V). Con el cabezal por defecto (3 mm a 15 mm) eso es ~10°.
- **Transición**: cada zona entra de a poco debajo del voladizo y sale arriba, con el espesor de capa dentro de
  60–140 %; las primeras capas quedan planas (`nonplanar_flat_below`). Si no hay lugar, la zona se inclina menos.
- **Resultado medido** (test `Non-planar layers tilted under the overhangs need fewer supports`): un hongo con ala a 55°
  y cabezal de 25° pasa de 1341 mm de filamento de soporte a 0. Con el cabezal por defecto (holgura 3 mm a 15 mm, ~10°)
  un ala a 48° queda sin soporte y una a 55° no se toca (queda igual que en planar).
- **Solo si alcanza**: un voladizo se inclina solo si la inclinación posible lo deja imprimible; inclinarlo a medias
  dejaría el soporte y además lo haría más alto (más material).
- **Límites**: techos casi horizontales (> 80°) quedan para puentes y soportes; las superficies superiores dentro de
  la transición salen curvas.

## Automático: acabado y menos soportes a la vez (Tisma)

Modo «Automático (acabado y menos soportes)» (`nonplanar_mode = automatic`), el recomendado: junta los dos modos que
ya funcionan por separado, sin ajustes propios (solo las dos medidas del cabezal).

- **Cómo**: la deformación es la suma de la de «Voladizos» (las capas se hunden hacia los voladizos que necesitarían
  soporte, tanto como deja el cabezal, en zonas que entran y salen solas) y la de «Caras superiores» (ajuste por
  mínimos cuadrados a las pendientes de las caras de arriba). Si la suma deja capas demasiado gruesas o finas, demasiado
  empinadas o el cabezal chocaría, se reduce solo la parte de las caras superiores; la de los voladizos ya cumple sola.
- **Medido** (test `Automatic non-planar layers: finish of the top and fewer supports`):
  - hongo con ala a 48° (cabezal 25°): filamento de soporte 683 mm en plano → 0 mm;
  - bloque con la cara superior de 20 a 24 mm: dispersión de la cara 0,127 mm → 0,058 mm.
- **Probado y descartado**: pedir las pendientes de los voladizos dentro del mismo ajuste por mínimos cuadrados. El
  ajuste daba el 74 % de la pendiente pedida y aun pidiendo la del cabezal los soportes bajaban solo 18–25 %: la
  inclinación fuerte bajo el voladizo choca con el límite de espesor de capa y la reducción uniforme achicaba todo.
- **Límites**: los de cada modo; donde un voladizo y una cara superior están muy cerca, manda el voladizo.

## Piel: capas que siguen las caras superiores (Tisma)

Modo «Caras superiores (acabado)» (`nonplanar_mode = surface`). Busca que las caras superiores se vean como una pieza
inyectada y no como escalones: las capas de arriba se ponen paralelas a la cara superior.

- **Sin opciones propias**: usa solo los datos del cabezal, el ángulo libre en X e Y y su holgura (comprobación de
  colisiones), y las capas planas de abajo.
- **Cómo**:
  - cada cara que mira hacia arriba pide, en una piel de 3 mm bajo ella, que la capa tenga su misma pendiente (limitada
    al cabezal); se muestrea cada ~1 mm, pesada por el área y por cuán horizontal es (en las caras casi planas es donde
    más se ven los escalones);
  - el desplazamiento de las capas se ajusta por mínimos cuadrados en una grilla 3D, con el mismo método de las capas
    por tensiones (`src/libslic3r/NonPlanarFit.cpp`, compartido con Ingeniería);
  - la transición desde las primeras capas planas ocupa toda la altura de la pieza;
  - el desplazamiento se reduce lo necesario para respetar el espesor de capa (60–140 %) y el ángulo del cabezal, y
    después hasta que el cabezal no choca con lo ya impreso (holgura medida). Lo mismo vale para las capas por tensiones.
- **Medido** (test `Non-planar layers following the top surfaces`): bloque de 40 × 40 mm con la cara superior de 20 a
  24 mm (5,7°). La dispersión de la cara impresa respecto del modelo baja de 0,127 mm en plano a 0,058 mm.
  - Con el anti-aliasing en Z solo (plano) queda en 0,088 mm.
  - En una pieza baja (cara de 5 a 9 mm) mejora poco (0,17 mm): no hay altura para inclinar las capas sin pasarse del
    espesor permitido.
- **Límites**:
  - las caras más empinadas que el cabezal se siguen solo hasta su ángulo;
  - las paredes verticales muestran las líneas de capa curvas;
  - hay una sola transición para toda la pieza: con caras superiores a alturas muy distintas y cercanas, la de menor
    peso se sigue menos.
- **Fuente de la idea**: CurviSlicer (Etienne et al., 2019), capas ligeramente curvas en 3 ejes para quitar los
  escalones de las caras superiores. La formulación de Tisma, mínimos cuadrados sobre las pendientes de las caras, es
  propia; no usa código de CurviSlicer.

## Capas curvas por tensiones (Tisma)

Es el punto 1 de la conclusión. En Ingeniería, sección «Capas curvas para resistencia»: **Calcular capas curvas** y,
si ayudan, **Usar las capas curvas**. Eso guarda las capas en el objeto (se guardan en el 3MF y se pueden deshacer) y
pone Configuración de impresión > No planar en «Tensiones».

- **Objetivo**: las piezas impresas son más débiles entre capas. Donde la pieza está traccionada, la capa debería
  contener la dirección del tirón para que vaya a lo largo de las extrusiones.
- **Cálculo** (`src/libtisma_fea/src/StressLayers.cpp`):
  1. análisis con capas planas; el FEA ahora guarda el tensor de tensiones de cada vóxel;
  2. por vóxel, la tensión principal mayor (si es tracción) y su dirección; la capa debe subir a lo largo de la
     dirección horizontal del tirón con pendiente tirón_z / tirón_horizontal, limitada por el cabezal (cono elíptico
     de los ángulos libres en X e Y). Los tirones casi verticales se dejan: inclinar 20–30° casi no ayuda;
  3. el desplazamiento de las capas se ajusta por mínimos cuadrados en una grilla 3D: pendientes pedidas pesadas por la
     tracción y por cuán horizontal es el tirón, capas suaves, poco cambio a lo largo de Z (espesor de capa) y planas
     en las primeras capas. Se resuelve con gradiente conjugado (Eigen);
  4. análisis de nuevo con las capas tal como se laminarán (transición desde las primeras capas planas, espesor dentro
     de 60–140 %, pendiente dentro del cabezal): se muestran los dos factores de seguridad.
- **Laminado**: modo `nonplanar_mode = stress`. Usa las capas guardadas en el objeto, relativas a la esquina mínima de
  la caja de la pieza, así que se puede mover en la cama. Si la pieza cambió de tamaño o de orientación, el laminado
  pide calcularlas de nuevo. Las capas se reducen hasta respetar el espesor y el cabezal, igual que el modo voladizos.
- **Test** (`Curved layers follow the pull of an inclined bar`): barra de 8 × 8 × 40 mm inclinada 45°, sujeta abajo y
  traccionada a lo largo de su eje, con un cabezal de 25°: el factor de seguridad pasa de 5,97 a 7,83 (+31 %). Una
  barra horizontal traccionada a lo largo queda con capas planas.
- **Límites**: en 3 ejes las capas solo se inclinan lo que deja el cabezal, y la transición desde las primeras capas
  planas reduce la inclinación cerca de la cama. Es un modelo lineal con material homogeneizado. La ganancia real hay
  que medirla con probetas.
- **Fuentes**: la idea de capas que siguen las tensiones principales es la de S³-Slicer (Zhang et al., SIGGRAPH Asia
  2022). Las capas ligeramente curvas con límites de pendiente y espesor para 3 ejes son las de CurviSlicer (Etienne
  et al., ACM Transactions on Graphics 38(4), SIGGRAPH 2019). La formulación de Tisma (mínimos cuadrados sobre las
  pendientes pedidas por el tirón) es propia y mucho más simple; no usa código de esos proyectos.

## Relleno entrelazado (Tisma)

Opción «Relleno entrelazado (no planar)» (`interlock_infill`, Configuración de impresión > Relleno > Avanzado). Es el
punto 2 de la conclusión: en lugar de curvar las capas enteras, solo el relleno disperso ondula en Z.

- **Forma**: el relleno de cada capa sigue la misma superficie de huevera, z = capa + a · sen(2πx/λ) · sen(2πy/λ), con
  `interlock_infill_amplitude` (porcentaje de la altura de capa, 50 % por defecto) y `interlock_infill_wavelength`
  (λ, 10 mm). Las capas del relleno encastran unas en otras en lugar de apoyarse en planos, que es por donde suelen
  romper las piezas impresas.
- **Lo demás queda plano**: perímetros, relleno sólido, superficies superiores e inferiores. La amplitud baja a cero
  junto a ellos: en la capa, a lo largo de λ/4 desde el borde del relleno disperso; entre capas, a lo sumo un 20 % de
  la altura de capa por capa (espesor entre 80 y 120 %), y la capa de relleno pegada a una capa sólida queda plana.
- **Flujo**: cada tramo extruye según el espesor que queda entre su capa y la de abajo en ese punto (mismo material
  total que en plano).
- **Viajes**: después del relleno ondulado, los viajes van por encima de las crestas impresas.
- **Cómo funciona**: un filtro del G-code de cada capa (como el anti-aliasing en Z), después de los otros filtros que
  cambian la Z. La amplitud de cada objeto sale de las superficies de relleno disperso de cada capa, en una grilla
  (`src/libslic3r/InterlockInfill.cpp`).
- **Compatibilidad**: no con capas no planares, modo vaso, impresoras de cinta ni ajuste de arcos (`Print::validate`).
- **Tests** (`tests/fff_print/test_interlock_infill.cpp`): bloque de 30 × 30 × 6 mm al 20 %: el relleno queda dentro de
  ±0,1 mm de su capa y llega a la amplitud completa; la primera y la última capa de relleno disperso quedan planas;
  todo lo demás está a la Z de su capa; el filamento total cambia menos del 1 %; E absoluta y relativa coinciden.
- **Sin medir todavía**: la ganancia de resistencia. La idea de capas onduladas para resistencia es la de Active-Z
  (Penn State, 2017) y la de las interfaces sinuosas entre capas (Western Sydney University), con otras máquinas y
  ensayos; en Tisma hay que medirla con probetas propias.

## Fuentes

- S³-Slicer: https://github.com/zhangty019/S3_DeformFDM, https://history.siggraph.org/?p=215754
- Neural Slicer: https://arxiv.org/html/2404.15061v2
- CurviSlicer: https://github.com/mfx-inria/curvislicer, https://mfx.loria.fr/software/2020/03/23/curvislicer.html
- Active-Z: https://pure.psu.edu/en/publications/active-z-printing-a-new-approach-to-increasing3d-printed-part-str/
- Relleno no planar: https://hackaday.com/tag/script/
- Brick layers y patente: https://www.fabbaloo.com/?p=229784
- Z-pinning: https://impact.ornl.gov/en/publications/z-pinning-approach-for-improving-interlayer-strength-of-3d-printe/
- Fractal Cortex: https://github.com/fractalrobotics/Fractal-Cortex, https://hackaday.com/2025/08/04/open-source-5-axis-printer-has-its-own-slicer/
- Open5x: https://github.com/FreddieHong19/Open5x
- Slicer4RTN: https://xyzdims.com/3d-printing/slicer4rtn/
- Z anti-aliasing: https://cnckitchen.com/blog/anti-aliasing-for-fdm-3d-printing-is-finally-here-micro-non-planar-printing
- Interfaces onduladas: https://researchers.westernsydney.edu.au/en/publications/interlayer-shear-strength-and-bonding-strength-of-sinuous-3d-prin/
