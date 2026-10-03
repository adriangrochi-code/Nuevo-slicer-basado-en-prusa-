# Conversor de presets 2.x → 3.0

PrusaSlicer 3.0 (alpha) sólo trae presets de Prusa, en su nuevo formato YAML
(`resources/presets/<repo>/<Fabricante>/`). Este conversor genera ese formato a
partir de los paquetes `.ini` por fabricante de la 2.9 (Creality, Voron,
Anycubic, Elegoo…), que es donde está el trabajo de la comunidad. El resultado
vive en `resources/presets/nps-community-fff/`.

```bash
tools/presets/regenerate.sh                     # descarga la 2.9.6 y convierte todo
tools/presets/regenerate.sh Creality Voron      # sólo algunos fabricantes
python -m pytest tools                          # tests (con PS29_PROFILES=… convierte todo)
python -m tools.presets.validate30 resources/presets/nps-community-fff/Creality
```

## Cómo funciona

| fichero | papel |
|---|---|
| `extract_schema.py` → `schema30.json` | lee el C++ de la 3.0 (`ConfigCommon.cpp`, `ConfigDefsFDM.cpp`) y saca, para cada clave, su ubicación (Printer/Print/Filament…), dónde se puede sobrescribir (Tool, Filament…) y su tipo |
| `ini29.py` | lee los `.ini` de la 2.x y resuelve la herencia (`inherits = A; B`: gana lo de la derecha y luego los valores propios) |
| `cond29.py` | evalúa las condiciones de compatibilidad de la 2.x (`printer_notes=~/…/ and nozzle_diameter[0]==0.4`) |
| `convert.py` | genera `vendor.yaml` (hardware) y los presets `printer`, `tool_print`, `print` y `filament` |
| `validate30.py` | comprueba lo que exige el cargador C++ de la 3.0 y la coherencia de los datos |

Decisiones principales:

- **Hardware:** cada `[printer_model:X]` pasa a ser una impresora en
  `vendor.yaml`. Cada variante de boquilla es una herramienta con id `0.4` o,
  si es de alto caudal, `0.4HF`, con una feature `supports_04_nozzle` por modelo.
  Se añade una bandeja `default` y un `printer_config` por modelo. La familia
  de la 2.x se convierte en `printer_families`.
- **Compatibilidad:** en vez de traducir el lenguaje de condiciones de la 2.x
  al de la 3.0, se evalúa cada condición contra todas las impresoras del paquete
  y se escribe una condición explícita con los pares modelo/boquilla
  compatibles, del tipo `(tool.nozzle_diameter == 0.4 and ! tool.nozzle_high_flow and printer.model =~ /^(CR5PROH|ENDER3)$/)`.
- **Reparto de claves:** la sección `[printer:]` de la 2.x se divide en el
  preset `printer` (claves Printer) y el preset `tool_print` (claves Print que
  se sobrescriben por herramienta, como la retracción). Las claves
  `filament_retract_*` pasan a ser overrides en el filamento, igual que hace
  `ConfigLegacy.cpp`.
- **Conversiones de la 3.0 replicadas:** los anchos de extrusión en % pasan a
  absolutos sobre la altura de capa (porque en la 3.0 el % es sobre la boquilla)
  y se aplica el reparto de los parámetros de primera capa del raft.
- **Ids:** son UUID5 en base64, estables entre ejecuciones, para que regenerar
  no cambie los ids.

## Limitaciones conocidas

- **No se ha probado aún dentro de la 3.0 compilada**, porque este entorno no
  puede descargar sus dependencias. La validación se hace contra las reglas del
  cargador C++ y contra el propio esquema: los presets oficiales de Prusa pasan
  el validador con 0 errores.
- **Impresoras multiherramienta (IDEX):** si hay varios presets para el mismo
  modelo y boquilla (p. ej. Sovol SV04 en modos espejo o copia), sólo se
  convierte el primero y el resto se registra como omitido.
- `compatible_prints_condition` (filamento según el perfil de impresión) se
  ignora y se anota. La usan muy pocos presets.
- Las claves sin equivalente en la 3.0 se descartan y se listan en
  `conversion-report.json` de cada fabricante. Muchas son de otros slicers
  (Orca/Bambu) y la 2.9 tampoco las usaba.
- Un hotend Volcano se trata como boquilla de alto caudal (`nozzle_high_flow`),
  que es lo más parecido que existe en la 3.0.
- `Templates.ini` (filamentos genéricos sin impresora) y los paquetes SLA no se
  convierten.

## Resultado (PrusaSlicer 2.9.6 → 3.0.0-alpha12)

31 fabricantes, 222 modelos, 792 presets de impresora, 1221 de impresión y
1000 de filamento; 76 presets omitidos, cada uno con su motivo en el informe.

| fabricante | modelos | impresoras | impresión | filamento | omitidos |
|---|---|---|---|---|---|
| Anker | 2 | 2 | 6 | 4 | 0 |
| Anycubic | 7 | 9 | 52 | 43 | 5 |
| Artillery | 8 | 8 | 26 | 23 | 2 |
| BIBO | 1 | 1 | 13 | 16 | 9 |
| BIQU | 1 | 1 | 7 | 3 | 0 |
| CocoaPress | 1 | 2 | 2 | 1 | 0 |
| Creality | 43 | 172 | 82 | 38 | 0 |
| E2D | 2 | 4 | 14 | 7 | 0 |
| Elegoo | 9 | 9 | 7 | 9 | 0 |
| FLSun | 2 | 4 | 14 | 6 | 0 |
| Geeetech | 25 | 104 | 86 | 9 | 0 |
| HartSmartProducts | 2 | 8 | 48 | 33 | 4 |
| INAT | 3 | 3 | 10 | 32 | 0 |
| Infinity3D | 2 | 2 | 10 | 24 | 0 |
| Jubilee | 1 | 2 | 4 | 7 | 0 |
| LNL3D | 5 | 20 | 86 | 3 | 0 |
| LulzBot | 2 | 2 | 3 | 4 | 0 |
| MakerGear | 9 | 33 | 14 | 38 | 4 |
| PapapiuLab | 1 | 1 | 4 | 4 | 0 |
| Print4Taste | 1 | 1 | 3 | 1 | 0 |
| QIDITechnology | 6 | 24 | 78 | 410 | 0 |
| RatRig | 13 | 38 | 37 | 3 | 3 |
| Rigid3D | 4 | 4 | 6 | 6 | 0 |
| Snapmaker | 18 | 72 | 68 | 88 | 0 |
| Sovol | 8 | 32 | 118 | 12 | 37 |
| TriLAB | 11 | 19 | 22 | 103 | 0 |
| Trimaker | 3 | 3 | 4 | 2 | 0 |
| Ultimaker | 2 | 2 | 4 | 3 | 2 |
| Voron | 20 | 188 | 378 | 48 | 4 |
| Zonestar | 5 | 5 | 2 | 4 | 6 |
| gCreate | 5 | 17 | 13 | 16 | 0 |
