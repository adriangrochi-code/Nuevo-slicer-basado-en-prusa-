# Revisión antes de imprimir

Después de laminar, Tisma revisa el resultado y avisa de los problemas que la impresión probablemente va a tener,
cada uno con las capas donde pasa, qué se encontró (con números) y qué hacer. Avisa y nunca bloquea: la persona
decide.

- **Panel lateral:** la tarjeta «Revisión antes de imprimir», debajo de «Información del laminado». Lo más grave va
  primero. «Ver en la vista previa» lleva la vista previa a la capa del problema.
- **Al exportar o enviar** (exportar, tarjeta SD, red, USB, Prusa Connect), si hay avisos que no son solo
  informativos, se pregunta una vez por cada G-code: «Continuar igual», «Ver en la vista previa» o «Cancelar».
- El cálculo está en `libslic3r` (`src/libslic3r/PrintReview.{hpp,cpp}`), así que también lo puede usar el servidor.

## Qué revisa

| Aviso | Gravedad | De dónde sale |
|---|---|---|
| Parte en el aire, voladizo que colapsa, puente anclado en el aire | Crítico | Los puntos de soporte que PrusaSlicer calcula en cada laminado (`SupportSpotsGenerator`), en los objetos sin soportes, como su aviso «Detectados problemas de estabilidad». Tisma agrega dónde está cada uno. |
| Extrusiones sueltas, baja adherencia a la cama, parte frágil | Advertencia | Lo mismo. |
| Puentes largos | Advertencia | Medido por Tisma, no por PrusaSlicer: el tramo continuo más largo de cada extrusión de puente que no tiene nada debajo (la sección completa de la capa anterior, ensanchada media boquilla donde apoya). Los puentes sobre el relleno o sobre capas sólidas tienen la capa de abajo debajo y no cuentan. Se compara con **Puente más largo** (`max_bridge_length`, Configuración de la Impresora > General > Puentes; 16 mm por defecto, el tramo que supone PrusaSlicer; 0 = sin chequeo). |
| Detalles que no se imprimen | Advertencia | Islas de las capas (más de 0,05 mm²) sin ninguna extrusión (ni perímetro, ni relleno, ni pared fina) en **3 o más capas seguidas** (una pared o un perno fino). La última capa de una punta o una cúpula es una astilla que siempre se pierde y no se avisa. Se informa el ancho estimado (2 × área / perímetro). |
| Capas sin tiempo para enfriarse | Advertencia | Capas del G-code que, ya frenadas por el enfriamiento, tardan menos del 90 % del tiempo mínimo del filamento (`slowdown_below_layer_time`): la velocidad mínima no alcanza. El 10 % de margen cubre la diferencia entre el cálculo del enfriamiento (sin aceleraciones) y el del G-code. |
| Viajes por el aire sin retracción | Advertencia | Viajes de más de 2 mm que salen de las islas de la capa y de los soportes (ensanchadas un diámetro de boquilla) sin retraer: ni sola, ni con wipe, ni durante el viaje. Con retracción no se avisa: así se hace cualquier impresión. No cuentan los viajes a la falda, al borde, a la torre de purga ni al G-code propio. No se calcula en impresoras de cinta ni con capas no planas. |
| Velocidades que la impresora va a limitar | Información | Extrusiones que piden en X o en Y más que `machine_max_feedrate_x/y` (si los límites de la máquina se usan). |

## Sin falsos positivos: verificación independiente

Cada aviso del Benchy con el perfil de la interfaz (Anycubic Kobra 3, PLA, 0,16 mm) se contrastó con el G-code,
reconstruyendo con geometría (shapely) lo que de verdad se extruye en cada capa:

| Aviso | Resultado del contraste | Decisión |
|---|---|---|
| «Puentes largos a 0,52 mm» (de PrusaSlicer) | De 817 mm de puente, el tramo más largo sin nada impreso debajo mide 4,7 mm: el resto apoya sobre el relleno sólido. Su detector mide la distancia al borde de la capa anterior, no si hay material debajo. | Falso positivo: reemplazado por la medición de Tisma. Con ella: 4,7 mm a 0,52 mm y 13,1 mm a 36,84 mm (techo de la cabina; el contraste da 13,5 mm), por debajo de 16 mm. |
| «5 detalles que no se imprimen» | Las 5 islas tienen 0 % extruido adentro, pero son astillas de una sola capa (puntas de 0,6 × 0,6 mm). | Ciertas pero no útiles: ahora solo cuentan 3 o más capas seguidas. |
| «49 capas sin tiempo para enfriarse» | El 100 % de la extrusión de esas capas va a 20 mm/s (el mínimo del perfil) y tardan 2,8–5,0 s contra los 8 s pedidos. | Correcto. |
| «824 viajes por el aire» (con retracción) | El contraste encuentra 1 244 viajes que salen de lo impreso, todos con retracción. | Cierto pero normal: ya no se avisa. |

Con las reglas nuevas, ese Benchy queda solo con el aviso de la chimenea.

## Medido

- Tests `[TismaReview]` (`tests/fff_print/test_print_review.cpp`), cada aviso en una pieza hecha para tenerlo y su
  ausencia donde no corresponde: parte que empieza a 10 mm de la cama; pared de 0,05 mm (1 detalle) y punta de un
  cono (nada); puente de 30 mm entre dos pilares (avisa a 10,2 mm; con 35 mm de límite no), puente de 10 mm (nada) y
  bloque con relleno al 15 % cuyos puentes apoyan en el relleno (nada); pilar de 3 × 3 mm sin tiempo para enfriarse
  (nada con el enfriamiento apagado); dos pilares sin retracción (avisa) y con retracción o wipe (nada); límites de
  20 mm/s (avisa; nada si se ignoran); cubo de 20 mm (nada).
- Benchy con el perfil por defecto: 166 ms para 109 785 movimientos.

## Pendiente

- Mostrar los puntos en la vista 3D (hoy se va a la capa).
- Revisar la retracción contra la calibración guardada del filamento y el caudal contra la boquilla.
