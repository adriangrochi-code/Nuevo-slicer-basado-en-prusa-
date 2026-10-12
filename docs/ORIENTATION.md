# Orientación automática

Gizmo de rotación (tecla `R`) > ventana **Optimizar orientación** > método > **Aplicar**. PrusaSlicer 2.9.6 la mostraba
solo con impresoras de resina; Tisma la muestra también con impresoras FDM, con sus propios métodos. El método elegido
y la precisión se guardan por tecnología (`sla_auto_rotate` y `fff_auto_rotate` en `TismaSlicer.ini`).

Código: `src/libslic3r/SLA/Rotfinder.cpp` (`find_least_fff_support_rotation`, `find_least_peel_rotation`), métodos en
`src/slic3r/GUI/Jobs/RotoptimizeJob.hpp`, ventana en `src/slic3r/GUI/Gizmos/GLGizmoRotate.cpp`. Tests:
`tests/sla_print/sla_rotfinder_tests.cpp`.

## FDM: «Menos material de soporte» (Tisma)

- Candidatas: apoyar cada cara de la envolvente convexa sobre la cama (como el método «Reducción de las pendientes de
  los voladizos» de resina), más la orientación actual: el resultado nunca es peor que la actual.
- Costo de cada candidata:
  - volumen de soporte: para cada cara que mira hacia abajo más empinada que el umbral de soporte
    (`support_material_threshold`; 45° desde la horizontal si es automático), su área proyectada por la altura hasta
    la pieza que tiene debajo o hasta la cama (rayo hacia abajo sobre un árbol AABB de la malla, construido una vez:
    se gira la vertical, no la malla);
  - menos el área apoyada en la cama por 1 mm (adherencia);
  - las orientaciones que tocan la cama con menos de 10 mm² se evitan (se despegan o vuelcan);
  - desempate por la altura.
- Después, como en todos los métodos, se gira en Z para que la caja mínima quede apaisada y se apoya en la cama.

Es una estimación geométrica: no simula el generador de soportes (puentes, soportes en árbol, voladizos que se
imprimen por perímetros solapados).

## Resina: «Menor fuerza de despegue» (Tisma)

- Candidatas: con la pieza en la plataforma (sin elevación o pad alrededor), las caras de la envolvente convexa; con
  soportes, direcciones repartidas sobre la esfera (red de Fibonacci). Más la orientación actual.
- Se ordenan por el puntaje de soportes de PrusaSlicer (el de «Reducción de las pendientes de los voladizos») y se
  quedan las mejores (4 a 16 según la precisión).
- Cada una se lamina (como mucho 400 capas) y se elige:
  1. la que tiene menos ventosas (detector de ventosas, `docs/RESINA.md`);
  2. entre esas, la de capa más grande más chica: la fuerza que separa cada capa del film crece con el área pegada
     (referencia sobre la fuerza de separación: F. Liravi, S. Das, C. Zhou, «Separation force analysis based on
     cohesive delamination model for bottom-up stereolithography using finite element analysis», Solid Freeform
     Fabrication Symposium 2014, https://utw10945.utweb.utexas.edu/sites/default/files/2014-113-Liravi.pdf).

## Cómo se probó

- Tests: un hongo (tallo 8 × 8 × 20 mm, sombrero 30 × 30 × 3 mm) queda con el sombrero en la cama; una barra de
  5 × 5 × 60 mm queda acostada; una taza de 20 mm sobre soportes no queda abierta hacia el film y sobre la plataforma
  queda de costado (sin ventosa).
- GUI (Linux, Xvfb): con una impresora FDM la ventana muestra «Menos material de soporte» y al aplicar el hongo queda
  con el sombrero en la cama. El método de resina se probó con tests, no en la GUI.
