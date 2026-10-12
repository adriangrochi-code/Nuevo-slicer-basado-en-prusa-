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

Con «Ajustar al cabezal» (`nonplanar_fit_head`, activado por defecto) la amplitud de las ondas y el ángulo del cono no
salen de los valores de la configuración de impresión sino de lo que mide el usuario en su cabezal (Configuración de
impresora > General > Cabezal):

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
