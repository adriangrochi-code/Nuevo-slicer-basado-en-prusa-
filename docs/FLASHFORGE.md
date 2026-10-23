# Impresoras Flashforge por la red

Tisma envía trabajos a impresoras Flashforge por la red local. El código está basado en el host Flashforge de
[OrcaSlicer](https://github.com/SoftFever/OrcaSlicer) (`src/slic3r/Utils/Flashforge.cpp`, AGPL-3.0), adaptado a Tisma.

Hay dos formas de hablar con la impresora y Tisma elige sola según lo que se complete:

| Forma | Impresoras | Qué hace falta |
|---|---|---|
| **API local** (HTTP, puerto 8898) | Adventurer 5M, 5M Pro, AD5X | IP, número de serie y código de acceso del modo LAN |
| **Consola de G-code** (TCP, puerto 8899) | Las anteriores (Adventurer 3/4, Guider, Creator…) | Solo la IP |

## Requisitos en la impresora

- Adventurer 5M / 5M Pro / AD5X: activar el **modo LAN** (Ajustes > Red). La pantalla muestra el **código de acceso**.
  El **número de serie** está en Ajustes > Acerca de.
- La impresora y la PC en la misma red local.

## Configuración en Tisma

Dispositivos > Agregar impresora de red (o Configuración > Impresoras físicas):

| Campo | Valor |
|---|---|
| Tipo de host | Flashforge |
| Nombre de host | Dirección IP de la impresora. **Explorar** busca las impresoras de la red y completa IP y número de serie |
| Clave API | Código de acceso del modo LAN (vacío en las impresoras viejas) |
| Número de serie | El de la impresora (vacío en las impresoras viejas) |

**Probar** confirma la conexión: con la API local pide el estado de la impresora (`/detail`) con el número de serie y
el código; con la consola toma el control de la impresora (`~M601 S1`).

## Cómo funciona

- **Búsqueda:** Tisma manda un mensaje UDP de difusión al puerto 48899; cada impresora contesta al puerto 18007 con su
  nombre y número de serie.
- **API local:** `POST /uploadGcode` con el archivo (campo `gcodeFile`) y, en encabezados, el número de serie, el
  código, el tamaño y si empieza a imprimir (`printNow`). La impresora contesta `{"code": 0}` si salió bien.
- **Consola:** `~M601 S1`, `~M115`, `~M650`, `~M119`; después `~M28 <tamaño> 0:/user/<nombre>`, el archivo en partes
  de 4 KiB, `~M29` para guardarlo y `~M23 0:/user/<nombre>` para imprimir.
- El nombre del archivo en la impresora solo lleva letras, números, `.`, `_` y `-` (el resto se cambia por `_`).

## Limitaciones

- No se manda el mapeo de la estación de materiales del AD5X: la impresión usa el filamento cargado.
- La G-code debe ser **de texto** (no binaria) y con el G-code de inicio de Flashforge (los perfiles de Flashforge
  convertidos de OrcaSlicer ya lo traen).

## Pruebas sin impresora

`tools/flashforge/fake_printer.py` simula una impresora (búsqueda, API local y consola):

```
python3 tools/flashforge/fake_printer.py --dir /tmp/fakeff &
TISMA_FLASHFORGE_FAKE=/tmp/fakeff build/tests/slic3rutils/slic3rutils_tests "[FlashforgeFake]"
```

Los tests `[Flashforge]` (sin red) prueban la lectura de las respuestas, la búsqueda y los nombres de archivo.
