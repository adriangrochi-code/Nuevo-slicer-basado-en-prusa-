# Perfiles de impresoras: máxima compatibilidad

Tisma trae los perfiles de PrusaSlicer (35 fabricantes en `resources/profiles`) y, además, los de **OrcaSlicer** y
**UltiMaker Cura** convertidos al formato de Tisma. No descarga los del repositorio en línea de Prusa (ver abajo).

## Selección de impresoras en el asistente

El asistente de configuración es una sola página, «Impresoras», que se abre directo (también la primera vez):

- **Marcas**: todas las marcas de los perfiles que trae Tisma, en orden alfabético y en una sola lista. Los paquetes
  de una misma marca van juntos: los convertidos de otros laminadores («Creality» junta los de Prusa, Cura y
  OrcaSlicer), las impresoras de resina con las de filamento («Anycubic SLA» dentro de «Anycubic») y todos los
  paquetes de Prusa (Prusa Research, sus SLA, Pro AFS y Pro HT) bajo «Prusa». Entre paréntesis, cuántas impresoras
  de la marca están marcadas.
- **Impresoras**: los modelos de la marca elegida, una fila por modelo, con casillas; al elegir uno se ve su foto.
- **Boquillas**: debajo de la foto, una casilla por boquilla del modelo (las variantes del perfil), como en
  OrcaSlicer. Marcar la impresora marca la boquilla de 0.4 mm (o la primera, si no tiene de 0.4); desmarcarla quita
  todas sus boquillas. Cada boquilla marcada instala su perfil de impresora.
- **Buscador**: filtra por nombre en todas las marcas (todas las palabras deben coincidir, sin distinguir
  mayúsculas). Con una búsqueda, la primera entrada de «Marcas» es «Todos los resultados».
- **Impresora personalizada**: la casilla «Mi impresora no está en la lista» agrega las páginas para definirla
  (firmware, cama, volumen, diámetros, temperaturas).

No hay páginas de bienvenida, inicio de sesión, fuentes de configuración, filamentos ni actualizaciones:

- Los perfiles son los que trae Tisma (`resources/profiles`). El asistente y el inicio del programa no descargan
  los repositorios de perfiles de Prusa: sus paquetes son más nuevos que la base 2.9.6 de Tisma (heredan presets que
  no existen en ella) y no conocen los paquetes propios de Tisma (pedía las imágenes de Elegoo SLA a Prusa y daba
  404). Con `tisma_online_profiles = 1` en `PrusaSlicer.ini` (carpeta de datos) vuelve el comportamiento de PrusaSlicer.
- Los filamentos y resinas son los predeterminados de cada impresora (`default_materials`), instalados sin preguntar.
- El resto (modo, actualizaciones, descargas) queda con sus valores por defecto y se cambia en Preferencias.

Marcar una impresora es lo mismo que marcarla en la página de su fabricante (`ConfigWizard::priv::pick_printer`).

### Cierre al cambiar de impresora (corregido)

Las impresoras convertidas de OrcaSlicer que ya existen en un paquete de Prusa llevan el sufijo « @Orca Prusa» en el
nombre. PrusaSlicer deriva de la «@» un alias para todos los presets, y la lista de impresoras de la ventana
principal mostraba ese alias («Prusa CORE One 0.25 nozzle»). Al elegirla, el nombre no existía y se seleccionaba la
primera impresora visible, a veces de resina, mientras las pestañas ya se preparaban para filamento: el perfil de
impresión quedaba sin selección y el programa se cerraba (y las impresoras «no se abrían»). Ahora las impresoras no
tienen alias (como ya suponía el resto del programa) y, si se selecciona otra impresora que la pedida, las pestañas
siguen a la tecnología de la que quedó. Pruebas: «Printers converted from OrcaSlicer are selected by their name»
(`fff_print_tests`) y «Resin printer profiles have compatible prints and materials» (`sla_print_tests`).

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

## Impresoras de cinta

`tools/profiles/make_belt_profiles.py` arma tres paquetes desde las fuentes de cada fabricante o comunidad:

| Paquete | Impresoras | Origen | Licencia |
|---|---|---|---|
| `Belt_BLACKBELT_3D.ini` («BLACKBELT 3D») | BLACKBELT 3D Printer y varioDrive, a 15°, 25°, 35° y 45°, boquillas de 0,2 a 1,2 mm: 40 | [BlackBelt Cura](https://github.com/BlackBelt3D/Cura) (`blackbelt*.def.json` y sus variantes), commit 4e18b19 | LGPL-3.0 |
| `Belt_IdeaFormer.ini` («IdeaFormer») | IR3 V2, 0,4 mm, con sus filamentos PLA (cama a 75 °C) | [VertexSlice](https://github.com/kdho77/VertexSlice), fork de OrcaSlicer para cintas, commit 1d6ad28 | AGPL-3.0 |
| `Belt_Creality.ini` («Creality (Belt)», bajo Creality) | CR-30 PrintMill, 0,4 mm, con su proceso y su filamento Hyper PLA | [Creality Print](https://github.com/CrealityOfficial/CrealityPrint), el laminador de Creality (`resources/profiles/Creality`, «Creality CR-30»), commit 59ae8cb | AGPL-3.0 |

- **Ejes**: en las tres la cinta es el eje **Z** del firmware y el pórtico inclinado el **Y** (BlackBelt: `G1 Z30 ; Move Belt`;
  CR-30: `G92 Z0 ; Set Belt to zero` en Creality Print y `Z_MAX_POS 20000000` en su
  [firmware](https://github.com/CrealityOfficial/cr-30); VertexSlice: «X=lateral, Y=gantry height, Z=belt»). Por eso Tisma tiene la opción
  `belt_axis_z` («La cinta es el eje Z», activada por defecto); desactivada, la cinta es Y y el pórtico Z.
- **BlackBelt**: el G-code de inicio y fin es el de BlackBelt Cura; su variable `{blackbelt_z_offset}` se reemplaza por
  el valor que calcula su definición (`boquilla/2 − 0,25/sen(ángulo)`). Velocidades, paredes, relleno y capas de
  BlackBelt Cura (capa de cada variante, primera capa a 5 mm/s). Altura útil: 455 mm (325 en varioDrive) por el seno
  del ángulo, como en su definición. Filamentos genéricos de Orca.
- **IR3 V2**: G-code de VertexSlice con las variables de Orca traducidas; proceso de 0,20 mm y dos filamentos PLA de
  VertexSlice.
- **CR-30**: impresora, proceso y filamento de Creality Print, con su G-code (sondeo del pórtico contra la cinta y
  purga). Creality Print no dice el ángulo: 45°, el de la CR-30 (VertexSlice: «45° inclined-belt machines (CR-30 /
  IdeaFormer IR3 V2 family)»). Ancho 170 mm y altura 200 mm como en Creality Print (el firmware tiene
  `X_BED_SIZE 210`, con los márgenes).
- Las tres usan E absoluta, porque su G-code de inicio ceba con E absoluta (`G92 E-4.5` en BlackBelt).
- **Cinta desde la primera capa**: la cinta vale 0 en la primera capa (los G-code de inicio la ponen a 0 justo antes), sea
  donde sea que esté la pieza sobre la «cama»; antes de este cambio arrancaba en la posición de la pieza (488 mm en la
  validación). Además, antes del primer movimiento en el plano de la capa el pórtico no se mueve (iba a 345 mm, una
  posición calculada sin conocer Y). Tests «Belt printer: the part is printed on the belt».
- La cama de Tisma es la cinta: ancho del pórtico × 1 000 mm.
- Validación: los paquetes cargan como perfiles del sistema (test «Vendor bundles of belt printers load») y las 42
  impresoras laminan un cubo (`validate_bundles.py`). **No probadas en impresoras reales.**
- Regenerar: `make_belt_profiles.py <BlackBelt Cura> <VertexSlice> <Creality Print> resources/profiles` con los commits.
- Pendientes: Sainsmart INFI-20 / iFactory3D One (sin fuente de perfil abierta encontrada), los materiales propios de
  BlackBelt.

## Laminadores de los fabricantes basados en OrcaSlicer

Creality Print, Elegoo Slicer, QIDI Studio, Snapmaker Orca y Bambu Studio son forks de OrcaSlicer o de Bambu Studio,
AGPL-3.0, con el mismo formato de perfiles. `convert_orca_profiles.py` los convierte con `--prefix`, `--source` y
`--url` (y `--maker` cuando el laminador nombra a sus fabricantes por serie, como QIDI Studio): solo los modelos que no
están en ningún paquete de Tisma (también los convertidos de Orca), sin su biblioteca de filamentos (se usa la de Orca)
y sin las impresoras de cinta (van en `Belt_*`). En el asistente aparecen bajo la marca («Creality (Creality Print)»).

| Laminador (commit) | Modelos nuevos | Paquete |
|---|---|---|
| Creality Print (59ae8cb) | 11: Creality Z2, K3, K1C 2025 y K1 Max 2025 (CFS-C), GS-01 a GS-04, Sonic Ender-3 S1 y Ender-5 S1, Ender-3 V3 Mega | `CrealityPrint_Creality.ini` |
| Elegoo Slicer (2d507e3) | 1: M3D Enabler D8500 MM | `ElegooSlicer_M3D.ini` |
| QIDI Studio (83be9f6), Snapmaker Orca (4996d50), Bambu Studio (79df7ae) | 0: todos sus modelos ya estaban | — |

- Variables propias de Creality Print traducidas: `position[2]` → `layer_z` (en el G-code de inicio de filamento, donde
  Tisma no tiene `position`), `initial_layer_height` → `first_layer_height`.
- Validación: cargan como perfiles del sistema (test «Vendor bundles converted from Creality Print and Elegoo Slicer
  load») y las 17 impresoras laminan un cubo.

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

## Impresora, boquilla y cama en el panel derecho

Como en OrcaSlicer, el panel derecho empieza por la impresora, sigue con los filamentos y termina con los ajustes de
impresión (el proceso):

- **Impresora**: cada modelo aparece una sola vez en la lista (se muestra el perfil de la boquilla elegida, si no el
  de 0.4 mm). Los perfiles de usuario y las impresoras físicas se listan como antes.
- **Boquilla**: las boquillas del modelo (los perfiles de sistema con el mismo `printer_model`); elegir una cambia al
  perfil de esa boquilla. Una impresora física que tiene ese perfil sigue seleccionada. Con un perfil de usuario se ve
  su boquilla, sin cambiarla.
- **Tipo de cama**: solo en las impresoras con camas intercambiables (`support_multi_bed_types`, en «General» de la
  impresora). Los tipos y sus nombres son los de OrcaSlicer (`src/libslic3r/BedType.cpp`, tomados de
  `src/libslic3r/PrintConfig.cpp` de OrcaSlicer): Smooth Cool Plate, Engineering Plate, Smooth High Temp Plate,
  Textured PEI Plate, Textured Cool Plate y Cool Plate (SuperTack). La cama elegida se guarda en el proyecto
  (`curr_bed_type`, como en OrcaSlicer); al cambiar de impresora vuelve a su cama por defecto (`default_bed_type`).

Cada filamento tiene una temperatura de cama (primera capa y resto) por tipo de cama, con las claves de OrcaSlicer
(`hot_plate_temp`, `hot_plate_temp_initial_layer`, `textured_plate_temp`...), en «Temperatura de cama por tipo de
cama» de la página «Filamento». Al laminar, la cama elegida reemplaza la temperatura de cama del filamento
(`apply_bed_type()` en `Print::apply`); un 0 deja la del filamento (en OrcaSlicer, 0 quiere decir que el filamento no
sirve para esa cama). Sin cama elegida se usa `default_bed_type` de la impresora, de modo que el G-code de inicio de
OrcaSlicer que pregunta por `curr_bed_type` (por ejemplo el ajuste de Z de las Bambu Lab en la cama texturada) recibe
la cama real.

El conversor de OrcaSlicer copia las temperaturas por cama de los filamentos, `default_bed_type` de las impresoras (el
nombre o el número del enum de OrcaSlicer) y activa `support_multi_bed_types` en las impresoras que lo tienen y en
todas las Bambu Lab (OrcaSlicer siempre deja elegir la cama en ellas). La temperatura de cama del filamento sigue
siendo la de la primera cama que da el filamento (`PLATE_ORDER`), para las impresoras sin tipos de cama.

## Revisión de los filamentos

`tools/profiles/check_filaments.py <bundles> --library resources/profiles/Templates.ini` revisa cada filamento (con
la herencia resuelta) y avisa de valores con los que no se puede imprimir: sin tipo, temperaturas fuera del rango de
su familia de material, cama, diámetro o densidad. `tools/profiles/validate_bundles.py` lamina un cubo con cada
impresora.

Valores rotos que vienen así de OrcaSlicer se corrigen en la conversión (`DATA_FIXES` de
`convert_orca_profiles.py`), tomando el valor del mismo filamento o del mismo fabricante:

| Filamento | En OrcaSlicer | En Tisma | De dónde |
|---|---|---|---|
| Anycubic ABS, ASA @Kobra S1 Max 0.8 | 220 °C | 250 °C | El mismo filamento con boquilla de 0.4 |
| Anycubic PA, PA6-CF, PAHT-CF, PC, PC-CF, PC-GF @Kobra S1 Max 0.6 / 0.8 | 220 °C | 260 °C (primera capa 250-270) | Ídem |
| Anycubic PET-CF @Kobra S1 Max 0.6 / 0.8 | 220 °C | 280 °C | Ídem |
| re3D PC @0.4 | primera capa 20 °C | 260 °C | Su temperatura de boquilla |
| re3D rPETG @0.8 | 0 °C | 250 °C | re3D PETG @0.4 |

## Próximos laminadores (pendiente)

| Laminador | Formato | Licencia | Plan |
|---|---|---|---|
| Bambu Studio, Creality Print, Elegoo Slicer | JSON de Orca/Bambu | AGPLv3 | El mismo convertidor; solo modelos que no estén ya |
| SuperSlicer | INI de PrusaSlicer | AGPLv3 | Copia casi directa |
| Cura | `definitions/*.def.json` | LGPLv3 | Convertidor propio (otro modelo de ajustes) |
