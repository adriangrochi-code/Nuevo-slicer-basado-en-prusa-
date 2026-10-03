# Arquitectura

## ¿Por qué no un fork directo de PrusaSlicer (todavía)?

PrusaSlicer (`libslic3r`) está construido de arriba abajo sobre capas planas: `Layer`,
`LayerRegion`, `ExPolygon` 2D y `GCodeGenerator`, que emite Z sólo en los cambios de
capa. Hacerlo no planar de forma nativa obliga a tocar el corte, el relleno, la
vista previa y la exportación de G-code a la vez. Además, la rama 3.0 todavía está
en alpha y cambia deprisa.

El enfoque **deformar → cortar en planar → transformar de vuelta** (la misma idea
que S4/S⁴-Slicer y el conical slicing) reutiliza el 100 % de PrusaSlicer y deja
validar la física (extrusión, caudal, colisiones) antes de invertir en C++.

## Módulos

| módulo | responsabilidad |
|---|---|
| `nps/stl.py` | E/S de STL binario y ASCII, soldado de vértices |
| `nps/subdivide.py` | subdivisión adaptativa y estanca (las aristas largas se curvarían mal al deformar) |
| `nps/fields.py` | forma de las capas `g(x,y)` (wave, conical) y rampa `ramp(z)` |
| `nps/transform.py` | real ↔ corte, Jacobiano `J`, validación de espesor y pendiente |
| `nps/prusa.py` | invocación de la CLI de PrusaSlicer con los overrides necesarios |
| `nps/gcode.py` | transformación inversa: troceo, Z real, `E·J`, políticas de caudal (`preserve`, `uniform`, por tipo de extrusión) |
| `nps/config.py` | configuración TOML universal, presets y generación del perfil de PrusaSlicer |
| `nps/homogeneity.py` | métricas de homogeneidad ponderadas por volumen |
| `nps/pipeline.py` | orquestación y compensación automática si PrusaSlicer desplaza la pieza |

## Plan para la fase 2 (C++)

1. Añadir a `PrintObjectConfig` un `nonplanar_field` (tipo + parámetros).
2. Aplicar la deformación a `ModelVolume` antes de `PrintObject::slice()`.
3. En `GCodeGenerator`, aplicar la transformación inversa y la corrección `E·J`
   en `GCodeWriter::extrude_to_xyz` (en lugar de un postproceso de texto).
4. Mostrar las capas curvas en la vista previa (el `GCodeViewer` ya pinta Z por
   vértice).
