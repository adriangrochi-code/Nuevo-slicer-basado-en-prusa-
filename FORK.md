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
| `tools/presets/` | conversor de presets 2.9 → 3.0 (tras actualizar upstream, ejecuta `tools/presets/regenerate.sh`) |
| `resources/presets/nps-community-fff/` | presets de otros fabricantes generados por el conversor |
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
   en el nuevo formato YAML (`resources/presets/`). **Hecho (pendiente de
   probar en la 3.0 compilada):** `tools/presets/` convierte los paquetes
   `.ini` de la 2.9 al formato 3.0. `resources/presets/nps-community-fff/`
   contiene 31 fabricantes (Creality, Voron, Anycubic, Elegoo, Sovol, QIDI,
   Snapmaker…) con 222 modelos. Ver [tools/presets/README.md](tools/presets/README.md).

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

## Bugs corregidos sobre la 3.0.0-alpha12

**Ninguno de estos cambios se ha compilado ni probado todavía.** Este entorno
no puede descargar las dependencias de PrusaSlicer, porque la política de red
bloquea `github.com`. Cada arreglo incluye, cuando ha sido posible, un test de
regresión que habrá que ejecutar en cuanto se pueda compilar (`ctest --test-dir build`).

### Portados de pull requests de la comunidad (autoría conservada con `Co-Authored-By`)

| upstream | qué arregla |
|---|---|
| PR #15774 | cuelgue de Arachne con aristas de Voronoi que se cruzan (GH #14421). **Medir rendimiento:** añade una comprobación con CGAL exacto en cada diagrama |
| PR #15764 | cuelgue en el primer arranque si los presets están instalados en solo lectura (Nix/Guix) |
| PR #15883 | la torre de purga dejaba la presión de avance desactivada si el slicer no podía restaurarla (GH #11187) |
| PR #15561 | en modo secuencial con raft, el raft usaba la herramienta del objeto anterior |
| PR #15565 | cierre en el desplegable de ajustes con teclas sin nombre (KDE/XWayland) |
| PR #15763, #15462 | compilación: Tracy sólo con perfilado, Catch2 v3 |
| PR #15829 | sólo el test: la corrección ya estaba en la alpha12 |
| PR #15709 (parcial) | `CFBundleIdentifier` con barra final en macOS (GH #15818) |

### Propios

| issue | causa encontrada | arreglo |
|---|---|---|
| GH #15899 | con `layer_height = 0`, `generate_object_layers()` no avanzaba y llenaba la memoria dentro de `Print::validate()` | error de validación `InvalidLayerHeight` (la 2.x lo tenía) y bucle protegido |
| GH #15868 | con el modo vaso y el proyecto vacío, `validate()` hacía `m_objects.front()` sobre un vector vacío | se revisan todos los objetos sin asumir uno solo |
| GH #15881 | los presets de usuario perdían valores heredados si el preset base tenía variantes sin `id:` | `unconditional_inherits` aplica también las variantes anónimas que coinciden |
| GH #15693 | `populate_local_bundle()` abortaba con rutas largas en Windows y dejaba el datadir roto para siempre | copia a `.partial` + renombrado, errores capturados y uso de los presets de la instalación como alternativa |
| (cppcheck) | `set_tool_value`: `index > size()` y después `.at(index)` → excepción sin capturar | `>=` (posible relación con GH #15875, sin confirmar) |
| (cppcheck) | pintado: se indexaba con `volume_idx = -1` al girar la rueda fuera de la pieza | acceso dentro de la comprobación |
| (cppcheck) | `Circle::operator=` sin `return` (comportamiento indefinido) | devuelve `Circle&` |
| (cppcheck) | `ConfigItemContext::is_dirty()` desreferenciaba antes de comprobar nulo | comprobación primero |

### Analizados y pendientes

- **GH #15673** (el modo vaso no hace la espiral): en la 3.0, el código que al
  activar el modo vaso ajustaba perímetros, relleno, capas superiores y
  soportes (`AbstractConfigManipulation`) ya no se usa en ningún sitio. Hay
  que decidir dónde vive esa lógica en la interfaz nueva.
- **GH #15668** (cierre al añadir un volumen) y **GH #15875**: hace falta
  reproducirlos con el programa compilado.
- **PR #15947** (conservar los filamentos al cambiar de impresora, GH #15945)
  y **PR #15909** (miniaturas en la CLI, GH #15937): son grandes y cambian
  comportamiento; conviene revisarlos con el programa compilado.
- **GH #15634** (colores de la vista "velocidad real"): exige cambiar los
  shaders de la vista previa.
