# Proyectos de Bambu Studio y OrcaSlicer

Tisma abre los 3MF de proyecto de Bambu Studio y OrcaSlicer (por ejemplo los de MakerWorld o Printables hechos con
ellos) como proyectos: al abrirlos aparece el diálogo de proyecto y «Abrir como proyecto» carga piezas, placas y ajustes.

## Qué se carga

- **Objetos con sus piezas**: cada objeto de Bambu es un objeto de Tisma con sus partes (antes cada parte salía como
  un objeto suelto). Tipos: pieza, modificador, volumen negativo, forzador y bloqueador de soportes.
- **Nombres** de objetos y partes, **extrusor** de cada objeto y parte, y los **ajustes por objeto o parte** que tienen
  equivalente (por ejemplo `sparse_infill_density` de un modificador → densidad de relleno).
- **Pintura**: colores (`paint_color`), soportes (`paint_supports`) y costura (`paint_seam`).
- **Placas**: cada placa va a la cama de Tisma con el mismo número, con su nombre y si está bloqueada. Las piezas
  conservan su lugar respecto del centro de la cama (sirve aunque la cama de Tisma sea de otro tamaño). Tisma tiene hasta
  9 camas; las placas de más quedan en la última.
- **Ajustes de impresión y de filamento** del proyecto, traducidos a Tisma con la misma tabla del conversor de
  perfiles (`tools/profiles/convert_orca_profiles.py`): capas, perímetros, relleno, soportes, velocidades, anchos de
  línea y aceleraciones en porcentaje, temperaturas de boquilla y de cama (las de la placa del proyecto), colores y
  tipos de filamento. Se aplican sobre los ajustes actuales y quedan como «(modificado)».

## Qué no se carga

- **La impresora**: el proyecto se imprime en la impresora elegida en Tisma. Los G-code de inicio y fin de Bambu usan
  sus propias variables y no se copian.
- Ajustes sin equivalente en Tisma (se anotan en el registro), los ajustes por placa (tipo de cama, secuencia) y las
  ayudas de costura precisa de OrcaSlicer.
- Si la impresora tiene menos extrusores que filamentos el proyecto, los valores por filamento se recortan a los
  extrusores de la impresora; la pintura de colores se conserva.

## Formato y fuentes

El formato se tomó del exportador de OrcaSlicer (`src/libslic3r/Format/bbs_3mf.cpp`, AGPLv3) y de su distribución de
placas (`PartPlateList::compute_origin()` en `src/slic3r/GUI/PartPlate.cpp`): mallas de las partes en
`3D/Objects/*.model`, un objeto con un componente por parte en `3D/3dmodel.model`, un elemento de `build` por instancia,
`Metadata/model_settings.config` (nombres, extrusores, tipos, ajustes y placas) y `Metadata/project_settings.config`
(ajustes en JSON). Las placas están en una grilla de ⌈√n⌉ columnas, separadas 1,2 veces el tamaño de la placa, con las
filas hacia −Y. El lector es propio de Tisma (`src/libslic3r/Format/BambuProject.cpp`); no copia código de OrcaSlicer.

## Pruebas

`tests/libslic3r/test_bambu_project.cpp` con `tests/data/test_3mf/bambu_project.3mf`, escrito por
`make_bambu_project.py` con esa misma disposición: dos objetos (uno con modificador pintado), tres instancias en dos
placas, dos filamentos. Comprobado también en la interfaz (Linux): placas «1 - Main» y «2 - Spare», ajustes modificados y
la impresora sin cambios. No probado con proyectos reales descargados (no hay ejemplos públicos con placas en los
repositorios oficiales): conviene probar varios de MakerWorld.
