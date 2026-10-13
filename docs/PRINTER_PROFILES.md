# Perfiles de impresoras: máxima compatibilidad

Tisma trae los perfiles de PrusaSlicer (35 fabricantes en `resources/profiles` más los del repositorio en línea de
Prusa) y, además, los de **OrcaSlicer** convertidos al formato de Tisma.

## Selección de impresoras en el asistente

La página «Impresoras» del asistente de configuración (antes de las páginas de cada fabricante) reemplaza la lista
de casillas de fabricantes:

- **Marcas**: todas las marcas de los perfiles que tiene Tisma, en orden alfabético. Los paquetes convertidos de
  otros laminadores se muestran bajo la marca (por ejemplo, «Creality» junta los perfiles de Prusa y los de
  OrcaSlicer). Entre paréntesis, cuántas impresoras de la marca están marcadas.
- **Impresoras**: los modelos de la marca elegida, uno por boquilla, con casillas; al elegir uno se ve su foto.
- **Buscador**: filtra por nombre en todas las marcas (todas las palabras deben coincidir, sin distinguir
  mayúsculas). Con una búsqueda, la primera entrada de «Marcas» es «Todos los resultados».

Marcar una impresora es lo mismo que marcarla en la página de su fabricante (`ConfigWizard::priv::pick_printer`): la
página del fabricante aparece en el índice para elegir más boquillas o ver las fotos, y desaparece al desmarcar todas
sus impresoras. El resto del asistente (filamentos, perfiles instalados) no cambia.

## Impresoras de UltiMaker Cura

- Origen: `resources/definitions` y `resources/extruders` de [Cura](https://github.com/Ultimaker/Cura) (commit en la
  cabecera de cada archivo), licencia **LGPL-3.0** (texto en `resources/licenses/Cura-LGPL-3.0.txt`).
- Solo se toma la máquina: tamaño y forma de la cama (rectangular o elíptica, origen en el centro), altura, boquilla,
  firmware, G-code de inicio y fin, retracción. Los perfiles de calidad y materiales de Cura no se convierten: cada
  paquete trae tres procesos genéricos por boquilla (fino, estándar, borrador: 30 / 50 / 70 % de la boquilla) y las
  impresoras usan los filamentos genéricos de la biblioteca de Orca (1,75 mm) o los genéricos de 2,85 mm del paquete.
- Archivos: `resources/profiles/Cura_<fabricante>.ini`, mostrados como «Fabricante» o «Fabricante (Cura)» si Tisma ya
  tiene ese fabricante (en el asistente se listan bajo la misma marca).
- Conversión: `tools/profiles/convert_cura_profiles.py <Cura>/resources resources/profiles --commit <sha>`.
  - G-code: variables de Cura traducidas (temperaturas, altura de capa, extrusor…), las demás reemplazadas por el valor
    de la definición; los corchetes literales pasan a paréntesis; `M82` pasa a `M83` (Tisma usa E relativa).
  - Se omiten: modelos que Tisma ya tiene (mismo nombre en cualquier paquete), definiciones ocultas o abstractas,
    impresoras de varios extrusores, firmwares que Tisma no genera (Griffin, UltiGCode, MakerBot, BFB, Mach3, E
    volumétrica), G-code con condicionales o variables desconocidas, y las que no son de filamento.

| Resultado de la conversión (Cura 5bea2f8) | |
|---|---|
| Impresoras nuevas | 296 (110 paquetes) |
| Ya estaban en Tisma | 193 |
| Omitidas: varios extrusores / firmware o no FFF / G-code | 93 / 10 / 21 |
| Laminan un cubo (`validate_bundles.py`) | 296 de 296 |

## Perfiles de OrcaSlicer

- Origen: `resources/profiles` de [OrcaSlicer](https://github.com/SoftFever/OrcaSlicer) (commit indicado en la cabecera
  de cada archivo), licencia **AGPLv3**, la misma de Tisma. Las imágenes y camas de cada impresora vienen del mismo
  lugar.
- Archivos: `resources/profiles/Orca_<fabricante>.ini` (+ `.idx` y la carpeta de imágenes). La biblioteca de
  filamentos genéricos de Orca es `Orca_OrcaFilamentLibrary.ini` (paquete de plantillas: compatible con todas las
  impresoras, como «Templates»). PrusaSlicer carga los filamentos de un paquete de plantillas con el sufijo
  « @Template» (`PresetBundle::load_configbundle`), así que los perfiles de impresora los nombran así en
  `default_materials` y `default_filament_profile` («Generic PLA @Orca @Template»). Sin el sufijo el asistente no
  encuentra los filamentos por defecto y no deja terminar (versión 1.0.1 de los paquetes; test
  `Default filaments of the converted printers exist as loaded`).
- En el asistente de configuración aparecen en «Other FFF» con el nombre del fabricante, o «Fabricante (OrcaSlicer)»
  cuando Tisma ya tiene un paquete de ese fabricante.

| Resultado de la conversión (OrcaSlicer 1d577ea) | |
|---|---|
| Fabricantes (paquetes) | 63 + la biblioteca de filamentos |
| Modelos de impresora nuevos | 340 |
| Perfiles de impresora (modelo × boquilla) | 902 |
| Procesos | 2 293 |
| Filamentos | 4 297 |
| Texto / imágenes | 6,8 MB / 24 MB |

### Sin duplicados

- Los modelos que Tisma ya tiene (mismo fabricante y nombre de modelo) no se convierten: 88 modelos.
- Los procesos y filamentos idénticos que Orca repite para cada impresora se unen en uno compatible con todas ellas:
  489 procesos y 2 650 filamentos.
- Los filamentos idénticos a uno genérico de la biblioteca se reemplazan por el genérico: 18.
- Los perfiles intermedios sin descendientes no se copian; los fabricantes sin modelos nuevos no generan paquete.

### Cómo se convierte

`tools/profiles/convert_orca_profiles.py` (detalles en su cabecera):

- Tabla de equivalencias de nombres de opciones y de valores (relleno, costura, bordes, soportes, planchado…),
  escrita comparando las definiciones de ambos programas. Las opciones de Orca sin equivalente se descartan.
- Anchos de línea en % de la boquilla (Orca) → mm; aceleraciones en % → mm/s²; temperatura de cama según la placa
  de Orca; encogimiento (Orca: tamaño final, Tisma: encogimiento); valores por defecto de Orca que difieren de los de
  Tisma (E relativa).
- G-code personalizado: variables traducidas; las variables de Orca sin equivalente se reemplazan por el valor del
  perfil o el valor por defecto de Orca; el tipo de cama por el de la impresora.
- Reglas de Tisma que Orca no exige: Klipper sin emitir límites de máquina, `G92 E0` por capa con E relativa, sin
  limpieza con retracción de firmware, diámetros mínimos de soportes orgánicos.

### Validación

- `tests/fff_print/test_vendor_bundles.cpp`: cada paquete carga en Tisma como perfil del sistema, sin sustituciones.
- `tools/profiles/validate_bundles.py`: lamina un cubo con cada impresora (su proceso y filamento por defecto).
  Resultado: **902 de 902 impresoras laminan**. Correcciones del convertidor que lo hicieron posible (antes 886):
  - Variables de Orca con mayúsculas (`required_nozzle_HRC`) y con índices anidados
    (`nozzle_volume_type[filament_map[0]-1]`) en el G-code de Prusa CORE One INDX, MK4 MMU3 y Snapmaker U1.
  - Extrusor de soportes: Orca usa 0 («el actual») por defecto y Tisma 1; con torre de purga y soportes no solubles
    Tisma exige 0 (M3D Enabler, iQ TiQ8).
  - Impresoras de varios extrusores con E absoluta: pasan a E relativa, que exige la torre de purga de Tisma
    (Flashforge Adventurer 3); las de un extrusor que heredan de ellas no heredan el `G92 E0` por capa (UltiMaker S5).
  - Proceso por defecto: si el de Orca no sirve (primera capa más gruesa que la boquilla o capa de más del 80 % de
    la boquilla), el compatible de capa más gruesa que sí sirva, también entre los procesos unificados (boquillas de
    0,2 mm de Creality K2 Pro y LONGER LK10).
- Bambu Lab: se pueden laminar y exportar; el envío por red de Bambu usa un componente cerrado y no está disponible.

### Regenerar

```
python3 tools/profiles/convert_orca_profiles.py <OrcaSlicer>/resources/profiles resources/profiles --orca-commit <hash>
python3 tools/profiles/validate_bundles.py build/src/tisma-slicer resources/shapes/box.stl /tmp/val \
    resources/profiles/Orca_*.ini --library resources/profiles/Orca_OrcaFilamentLibrary.ini
```

El convertidor lee los tipos de opciones de `src/libslic3r/PrintConfig.cpp` y `Preset.cpp` de Tisma, y los valores
por defecto de Orca de su `src/libslic3r/PrintConfig.cpp` (junto a `resources/profiles`).

## Próximos laminadores (pendiente)

| Laminador | Formato | Licencia | Plan |
|---|---|---|---|
| Bambu Studio, Creality Print, Elegoo Slicer | JSON de Orca/Bambu | AGPLv3 | El mismo convertidor; solo modelos que no estén ya |
| SuperSlicer | INI de PrusaSlicer | AGPLv3 | Copia casi directa |
| Cura | `definitions/*.def.json` | LGPLv3 | Convertidor propio (otro modelo de ajustes) |
