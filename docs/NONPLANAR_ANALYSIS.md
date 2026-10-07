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
