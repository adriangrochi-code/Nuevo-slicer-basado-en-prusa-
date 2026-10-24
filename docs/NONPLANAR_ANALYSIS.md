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
- cono: la pendiente que piden los voladizos de la pieza en cada radio, como mucho el ángulo libre menor (ver «Cono
  según la pieza»);
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

## Cono según la pieza (Tisma)

Modo «Cónico» con «Ajustar al cabezal» (lo predeterminado): las capas tienen forma de cono alrededor del centro de la
pieza, pero la pendiente de cada radio sale de la geometría, no de un ángulo fijo. La idea de un cono cuyo ángulo cambia
con el radio es la del laminador radial de Joshua Bird (https://github.com/jyjblrd/radial_non_planar_slicer), donde el
ángulo es una fórmula puesta a mano para cada modelo (por ejemplo 15° + 30° · r / R). Su licencia (GPL v3 con una
cláusula que prohíbe vender) no es compatible con la AGPL, así que no se usa nada de su código; Tisma lo calcula de la
pieza (`build_radial_profile`):

1. Cada triángulo que mira hacia abajo más allá del umbral de voladizo (`nonplanar_overhang_threshold`) y menos que un
   techo (80°), por encima de las capas planas del principio, pide una pendiente radial. La pieza se lamina cizallada
   (z' = z − g(r)), así que una cara a β de la vertical queda en cot β' = cot β + pendiente: se pide la que la lleva
   5° por debajo del umbral, o 2° si el cabezal no permite más (lo mismo que el modo Voladizos); si ni así entra en el
   ángulo libre, se deja a los soportes. Solo cuenta la parte radial del voladizo.
2. Hacia abajo en los voladizos que miran hacia afuera (un hongo, una hélice, un árbol), hacia arriba en los que miran
   hacia adentro (el borde de un cuenco dado vuelta). Cada triángulo pide en todos los radios que cubre.
3. El perfil cambia de a poco: la pendiente pasa de plana a la máxima del cabezal en un cuarto del radio de la pieza
   (al menos 5 mm), es plana en el centro (sin punta) y nunca supera el ángulo libre menor.
4. Las capas llegan a esa pendiente gradualmente desde las primeras capas planas hasta el voladizo más bajo que la pide;
   luego se reduce lo necesario para el espesor de capa (60–140 %) y para que el cabezal no choque.
5. Una pieza sin voladizos que el cono pueda ayudar se lamina plana.
6. El cono se usa solo si deja menos soporte que las capas planas (ver «Elección por menos soporte»): un cono que
   conviene a un voladizo puede hacer más altos los soportes de otro. En el cuenco dado vuelta (borde a 52° hacia
   adentro) el cono subía el soporte de 718 a 1052 mm de filamento; ahora queda plano (718).

Medido en los tests (hongo con ala a 52°, cabezal de 25°): soportes de 705 mm de filamento con capas planas a 0 con el
cono según la pieza; el ala pide cot 40° − cot 52° = 0,41 de pendiente, el centro queda plano. Sin «Ajustar al cabezal»
el cono sigue siendo el de antes, con el ángulo puesto a mano.

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
- **Pendiente según la distancia**: además de esa pendiente única, se arma otra forma en la que la pendiente cambia
  dentro de la capa según la geometría. Cerca de lo ya impreso, las capas bajan tan empinadas como deja el ángulo libre
  (la boquilla, hasta el bloque calefactor); más lejos, más suave. La regla: entre dos puntos cualesquiera, lo ya impreso
  no sube más de lo que el cabezal permite a esa distancia (el mínimo entre ángulo libre × distancia y la altura libre,
  dentro de su radio, o el perfil medido). Se calcula la bajada más profunda que la cumple a lo largo de la distancia a
  lo ya impreso (programación dinámica, mínimo de sumas). Así una cuña o un voladizo corto se inclinan con el ángulo
  máximo y una parte recta queda plana. Con el cabezal por defecto, un ala a 55° pasa de 1237 a 863 mm de filamento de
  soporte (antes no se tocaba).
- **Transición**: cada zona entra de a poco debajo del voladizo y sale arriba, con el espesor de capa dentro de
  60–140 % donde se imprime la pieza; las primeras capas quedan planas (`nonplanar_flat_below`). Debajo de un voladizo
  solo se imprime la parte que lo sostiene (un tallo), donde las capas bajan poco, y el aire alrededor, donde solo van
  viajes y soportes (espesor de capa de al menos 30 %): la transición es tan corta como esas dos cosas permiten. Antes
  se calculaba con la bajada más profunda en todo el espacio, y un ala ancha sobre un tallo corto quedaba plana. Si no
  hay lugar, la zona se inclina menos.
- **Resultado medido** (test `Non-planar layers tilted under the overhangs need fewer supports`): un hongo con ala a 55°
  y cabezal de 25° pasa de 1341 mm de filamento de soporte a 0. Con el cabezal por defecto (holgura 3 mm a 15 mm, ~10°)
  un ala a 48° queda sin soporte y una a 55° baja de 1237 a 863 mm (pendiente según la distancia).
- **Medido por línea de comandos** (capas de 0,2 mm, soporte automático, cabezal de 25° con holgura de 7 mm a 15 mm;
  filamento de soporte plano → voladizos):
  - ala a 55° de un tallo de radio 8 a radio 20, a 10 mm de la cama: 2339 → 0 mm (antes de la transición corta: 2339);
  - ala a 52°: 1714 → 0; hongo de cúpula a 52°: 3065 → 0; hongo alto a 55°: 3690 → 0;
  - cuenco dado vuelta (borde a 52° hacia adentro): 718 → 0, con el techo como puente;
  - ala a 60°: sin cambio (2153): necesita 31,6° de inclinación y el cabezal deja 25°.
- **Elección por menos soporte**: de las dos formas (pendiente única y según la distancia) queda la que deja menos
  soporte, y ninguna si no deja al menos 10 % menos que las capas planas (ver abajo).
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
- **Cono**: también se compara con el cono según la pieza. Queda el que deje menos soporte; si ninguno deja claramente
  menos que las capas planas, queda la suma (por el acabado de las caras de arriba) solo si no deja más soporte que
  plano.

## Elección por menos soporte (Tisma)

Los modos «Voladizos», «Cónico» (con «Ajustar al cabezal») y «Automático» comparan sus formas de capa con las capas
planas antes de laminar (`support_volume`):

1. La pieza se deforma al espacio de laminado con cada forma.
2. Cada cara que mira hacia abajo más que el umbral de soporte del objeto (`support_material_threshold`, o con 0 el
   automático: media línea de 1,125 × boquilla por capa, unos 48° desde la vertical con capas de 0,2 mm) aporta una
   columna desde la cara hasta la pieza de abajo o la cama (rayo hacia abajo), medida en el espacio real.
3. Queda la forma con menos volumen, si deja al menos 10 % menos que las capas planas; si no, capas planas. El margen
   cubre lo que la estimación no ve (cómo el generador expande y une los soportes): en el cuenco dado vuelta con umbral
   de 45° la estimación daba 22 % menos con la pendiente según la distancia y el soporte real era 11 % más, hasta usar
   el umbral real del generador.
4. La decisión se guarda por pieza y ajustes: la deformación se arma varias veces por laminado (validación, corte,
   G-code, análisis).

La validación revisa el espesor de capa dentro de la pieza (60–140 %, con 1 % de margen por el muestreo) y en el aire
alrededor (al menos 30 %).

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

## Calibraciones no planares (Tisma)

**Quitadas del programa** (se van a rehacer de otra manera): la tabla describe las que hubo, cuyo código queda en la
historia de git.

| Prueba | Qué mide |
|---|---|
| Asistente de calibración no planar | La pendiente máxima en dos pasos: la galga estática con la impresora fría y luego la prueba impresa |
| Galga estática del ángulo libre | Ranuras en V por alturas: ángulo libre del cabezal en X y en Y (`nonplanar_head_free_angle_x/_y`, `nonplanar_head_profile`) |
| Pendiente máxima de capa | Crestas de pendiente creciente por franjas: la primera donde la boquilla raspa o arrastra marca el límite |
| Velocidad del eje Z | Cilindro con capas en onda y velocidad Z creciente por franjas: la primera con capas aplastadas o corridas, pared áspera o ruido del motor marca el límite |
| Galga de altura libre del cabezal | Escalera impresa plana: el escalón más alto que entra bajo el punto más bajo del cabezal, con la impresora fría |
| Probetas del modo automático | Hongo (soportes) y bloque inclinado (acabado) |
| Probetas de resistencia entre capas | Tres barras macizas de pie (5 mm de espesor) con capas en onda, para romper y comparar con las mismas impresas planas |

Las probetas de resistencia son una prueba comparativa propia, no una norma: el pedestal llega 2 mm por encima de donde
las capas curvas están completas (`nonplanar_flat_below` + `nonplanar_ramp_height`), así la barra se rompe por
capas en onda; una cuña a 45° arriba sujeta el hilo sin soportes. Con la barra horizontal en una morsa y un balde
colgado, la tensión de rotura es la de una viga en voladizo, σ = 6·F·L / (W·T²) (flexión elemental). El cociente entre
el promedio con capas en onda y el promedio plano es la ganancia de resistencia entre capas que da el modo Onda.

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
- Laminador radial (solo la idea del ángulo según el radio): https://github.com/jyjblrd/radial_non_planar_slicer
- S4 Slicer (4 ejes, deformación por rotaciones de tetraedros; solo para comparar): https://joshuabird.com/blog/post/the-s4-slicer
- Automated topography slicing for support-free 3-axis FDM (Int. J. Adv. Manuf. Technol., 2026; campo de alturas bajo
  los voladizos en 3 ejes, la idea más cercana al modo «Voladizos»): https://link.springer.com/article/10.1007/s00170-026-18768-1
- Interfaces onduladas: https://researchers.westernsydney.edu.au/en/publications/interlayer-shear-strength-and-bonding-strength-of-sinuous-3d-prin/
