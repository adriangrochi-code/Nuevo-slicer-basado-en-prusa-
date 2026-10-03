# Fork no planar de PrusaSlicer 3.0

Fork de [PrusaSlicer](https://github.com/prusa3d/PrusaSlicer) **3.0.0-alpha12**
(upstream `30ef591`) para crear un slicer universal con capas no planas,
orientado a **resistencia homogénea** y a **velocidad**. Nombre definitivo
pendiente; de momento lo llamamos NPS (Non-Planar Slicer).

## Estructura

| ruta | contenido |
|---|---|
| raíz (`src/`, `resources/`, `deps/`…) | PrusaSlicer 3.0 alpha12 sin modificar (de momento) |
| `nps-prototype/` | prototipo en Python: deformación de capas, transformación inversa del G-code, políticas de caudal, límites de Z, métricas de homogeneidad, configuración TOML. Es la **referencia** con la que se valida la versión C++. |
| `tools/update-upstream.sh` | trae nuevas versiones de PrusaSlicer |
| `FORK.md` | este documento |

## Objetivos

1. **No planar en el núcleo C++** (`src/libslic3r`). Se deforma la malla antes
   del corte y se aplica la transformación inversa al generar el G-code, con
   corrección de extrusión `E·J`, caudal volumétrico controlado y límites
   cinemáticos de Z. Las capas curvas se verán en la vista previa de la 3.0.
2. **Interfaz:** sección "No planar / Homogeneidad" en los ajustes de impresión
   (patrón de onda, amplitud, longitud de onda, giro, rampa, política de
   caudal) e informe de homogeneidad tras el corte.
3. **Impresoras de otros fabricantes.** La 3.0 alpha sólo trae presets de Prusa,
   en el nuevo formato YAML (`resources/presets/`). Plan: un conversor de los
   paquetes `.ini` por fabricante de la 2.9 (Creality, Voron, Anycubic,
   Elegoo…) al formato 3.0, empezando por Creality (CR-5 Pro H incluida),
   Voron/Klipper y Marlin genérica.

## Actualizar a una nueva versión de PrusaSlicer

El fork se importó sin el historial de Prusa, así que las versiones nuevas se
traen aplicando el diff entre etiquetas:

```bash
tools/update-upstream.sh version_3.0.0-alpha12 version_3.0.0-alpha13 --check   # sólo comprobar
tools/update-upstream.sh version_3.0.0-alpha12 version_3.0.0-alpha13           # aplicar (3-way)
```

Después hay que actualizar la versión base indicada en este documento.

## Compilar

Igual que PrusaSlicer: ver [doc/Build.md](doc/Build.md). Las dependencias
(`deps/`) tardan horas la primera vez.

## Licencia y marca

PrusaSlicer es AGPL-3.0, así que este fork también lo es, incluido
`nps-prototype/`. Como hacen otros forks (SuperSlicer, OrcaSlicer), antes de
distribuir binarios hay que cambiar el nombre y los iconos, para no presentarlo
como un producto de Prusa Research.
