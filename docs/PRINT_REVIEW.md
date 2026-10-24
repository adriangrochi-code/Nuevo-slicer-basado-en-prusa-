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
| Extrusiones sueltas, puentes largos, baja adherencia a la cama, parte frágil | Advertencia | Lo mismo. |
| Detalles que no se imprimen | Advertencia | Islas de las capas (más de 0,05 mm²) sin ninguna extrusión (ni perímetro, ni relleno, ni pared fina): la boquilla no entra. Se informa el ancho estimado (2 × área / perímetro). |
| Capas sin tiempo para enfriarse | Advertencia | Capas del G-code que, ya frenadas por el enfriamiento, tardan menos del 90 % del tiempo mínimo del filamento (`slowdown_below_layer_time`): la velocidad mínima no alcanza. El 10 % de margen cubre la diferencia entre el cálculo del enfriamiento (sin aceleraciones) y el del G-code. |
| Viajes por el aire sin retracción | Advertencia | Viajes de más de 2 mm que salen de las islas de la capa y de los soportes (ensanchadas un diámetro de boquilla) sin retraer: ni sola, ni con wipe, ni durante el viaje. Muestreados cada 1 mm, sin el primer y el último milímetro. |
| Viajes por el aire | Información | Lo mismo, con retracción: dónde aparecerían hilos si la retracción o la temperatura no son las del filamento. No cuentan los viajes a la falda, al borde, a la torre de purga ni al G-code propio. No se calcula en impresoras de cinta ni con capas no planas (sus coordenadas no son las de las capas). |
| Velocidades que la impresora va a limitar | Información | Extrusiones que piden en X o en Y más que `machine_max_feedrate_x/y` (si los límites de la máquina se usan). |

## Medido

- Tests `[TismaReview]` (`tests/fff_print/test_print_review.cpp`): cada aviso aparece en una pieza hecha para tenerlo
  (una parte que empieza a 10 mm de la cama, una pared de 0,05 mm, un pilar de 3 × 3 mm, dos pilares separados, con y
  sin retracción y con wipe, límites de 20 mm/s), no aparece cuando no corresponde (enfriamiento apagado, límites
  ignorados, un solo bloque) y un cubo de 20 mm no tiene avisos.
- Benchy con el perfil por defecto: 166 ms para 109 785 movimientos. Encuentra las capas de la chimenea (40,5–42,4 mm)
  que tardan 4,3 s cuando el filamento pide 5 s, y 520 viajes por el aire entre partes.
- En la interfaz (Anycubic Kobra 3, PLA, 0,16 mm): puentes largos a 0,52 mm, 5 detalles de hasta 0,2 mm entre 13 y
  27,4 mm, 49 capas de la chimenea de 3 s cuando el filamento pide 8 s, y 824 viajes por el aire. «Ver en la vista
  previa» lleva a la capa 3 (0,52 mm) y a la 252 (40,36 mm).

## Pendiente

- Mostrar los puntos en la vista 3D (hoy se va a la capa).
- Revisar la retracción contra la calibración guardada del filamento y el caudal contra la boquilla.
