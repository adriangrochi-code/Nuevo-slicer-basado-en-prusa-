# Hoja de ruta de Tisma Slicer

Interfaz híbrida: estructura y estética de PrusaSlicer 3.0 (interfaz clara y tranquila, columna de
navegación a la izquierda, panel derecho con los ajustes más usados y favoritos) con las calibraciones
y funciones de OrcaSlicer. Ambos proyectos son AGPLv3: el código portado conserva su atribución.

## Fase 1 — Interfaz (estilo PrusaSlicer 3.0)

- [x] 1.1 Ajustes rápidos en el panel lateral: favoritos elegibles por el usuario, agrupados por categoría.
- [x] 1.2 Columna de navegación a la izquierda (Preparar / Proceso / Filamento / Impresora / Dispositivo / Ajustes).
- [x] 1.3 Panel derecho: títulos en mayúsculas, secciones planas, botón Laminar violeta y Exportar con contorno.
- [ ] 1.4 Pulido: barra superior con menú y nombre del proyecto, lista de objetos flotante, páginas de ajustes con el mismo estilo.
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

## Fase 4 — Piezas industriales: análisis de carga

Objetivo: indicar cargas y apoyos fijos sobre la pieza y que el slicer refuerce donde hace falta.

- [ ] 4.1 Selección de zonas de la pieza (pintado o modificadores) para cambiar densidad de relleno,
      perímetros y soportes por zona.
- [ ] 4.2 Definición de cargas: caras fijas, fuerzas y presiones, con dirección y valor.
- [ ] 4.3 Análisis simplificado (elementos finitos sobre una malla de vóxeles) teniendo en cuenta la
      anisotropía de la impresión por capas.
- [ ] 4.4 Mapa de tensiones en la vista 3D.
- [ ] 4.5 Optimización automática: más relleno y paredes donde hay tensión, menos donde no la hay,
      orientación de relleno según las tensiones principales; informe de peso y resistencia estimada.
