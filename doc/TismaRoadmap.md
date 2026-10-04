# Hoja de ruta de Tisma Slicer

Interfaz híbrida: estructura y estética de PrusaSlicer 3.0 (interfaz clara y tranquila, columna de
navegación a la izquierda, panel derecho con los ajustes más usados y favoritos) con las calibraciones
y funciones de OrcaSlicer. Ambos proyectos son AGPLv3: el código portado conserva su atribución.

## Fase 1 — Interfaz (estilo PrusaSlicer 3.0)

- [x] 1.1 Ajustes rápidos en el panel lateral: favoritos elegibles por el usuario, agrupados por categoría.
- [ ] 1.2 Columna de navegación a la izquierda (Preparar / Vista previa / Dispositivo / Calibración / Ajustes).
- [ ] 1.3 Panel derecho en tarjetas: impresora, filamento y proceso con resumen; botones Laminar / Exportar / Imprimir.
- [ ] 1.4 Pulido visual: controles planos y redondeados, espaciado, tipografía, modo oscuro coherente.
- [ ] 1.5 Proyectos en pestañas (opcional).

## Fase 2 — Calibraciones y funciones de OrcaSlicer

- [ ] 2.1 Menú Calibración e infraestructura (modelos, parámetros por capa).
- [ ] 2.2 Torre de temperatura.
- [ ] 2.3 Flujo (pasada 1 y 2).
- [ ] 2.4 Pressure / linear advance (línea, patrón, torre; M900 en Marlin, SET_PRESSURE_ADVANCE en Klipper).
- [ ] 2.5 Torre de retracción.
- [ ] 2.6 Velocidad volumétrica máxima.
- [ ] 2.7 VFA, tolerancias e input shaping.
- [ ] 2.8 Funciones de Orca a evaluar: paredes precisas, compensación de flujo en áreas pequeñas,
      orden de paredes interior/exterior/interior, una sola pared en superficies superiores, "hacer imprimibles los voladizos".

## Fase 3 — Funciones avanzadas

- [ ] 3.1 Brick layers (capas desplazadas en perímetros alternos).
- [ ] 3.2 Arc overhangs: revisión de la versión actual, ajustes en la interfaz y vista previa.
- [ ] 3.3 Laminado no planar: integración en la interfaz, vista previa y validación paso a paso.
