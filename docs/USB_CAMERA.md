# Cámara USB apuntada a la impresora

Una webcam USB (UVC) conectada a la computadora donde corre Tisma sirve de cámara de una impresora: para las que no
tienen (la mayoría de las Marlin: Ender, Prusa MK3/MK4, Anycubic...) o tienen una lenta (series A1 y P1 de Bambu Lab,
que por LAN mandan del orden de 1 a 5 imágenes por segundo según usuarios y SimplyPrint; Bambu Lab no publica el dato).

## Uso

Dispositivos > elegir la impresora > **Cámara**: «La cámara de la impresora» (Bambu Lab A1 y P1), «Sin cámara» o una de
las cámaras USB conectadas. La elección se guarda por impresora en `PrusaSlicer.ini` (sección `tisma_usb_cameras`) y
vuelve sola al abrir Tisma. Al lado se ve lo que da la cámara: tamaño, formato e imágenes por segundo reales.

- Impresora USB: la imagen va en la columna derecha de su panel, entre la impresión activa y la temperatura.
- Impresoras de red: debajo de los datos de la impresora (en las Bambu Lab reemplaza a su cámara).

## Cómo funciona

- Biblioteca [openpnp-capture](https://github.com/openpnp/openpnp-capture) 0.0.28 (MIT, en `bundled_deps/`):
  DirectShow en Windows, AVFoundation en macOS, V4L2 en Linux; los cuadros llegan siempre en RGB de 24 bits.
- `src/slic3r/Utils/UsbCamera.cpp`: lista las cámaras, elige el formato y entrega los cuadros en un hilo.
  - Formato: el más grande hasta 1280 x 720 que dé al menos 10 imágenes por segundo, MJPEG primero (menos ancho de
    banda USB: entra una segunda cámara en el mismo puerto). Obico mira imágenes de 416 x 416, así que 720p alcanza.
  - En pantalla, hasta 15 imágenes por segundo; las que la interfaz no alcanza a dibujar se saltean (no se acumulan).
  - Si la cámara se desconecta o deja de mandar imágenes 10 s, se avisa en la vista.
- Las imágenes quedan solo en memoria: no se guardan en disco ni salen de la computadora.
- macOS: `NSCameraUsageDescription` en `Info.plist` (sin ella macOS cierra el programa al abrir la cámara) y el permiso
  `com.apple.security.device.camera` del hardened runtime.

## Pruebas

- `tests/slic3rutils/usb_camera_tests.cpp`: elección del formato (también el `jpeg` de macOS), imágenes por segundo,
  lista sin cámaras y cámara inexistente.
- Linux, sin cámara física: una cámara V4L2 simulada con `LD_PRELOAD` (MJPEG 1920 x 1080, 1280 x 720, 640 x 480 a 30
  imágenes por segundo, sin tablas Huffman como las webcams) pasa por openpnp-capture, `UsbCamera` y la página
  Dispositivos: 1280 x 720 MJPG, 15 imágenes por segundo en pantalla; la cámara elegida vuelve al reiniciar.
- El decodificador MJPEG de Linux da los mismos píxeles con y sin tablas Huffman y tarda 2,1 ms por cuadro de 1280 x 720.
- **Sin probar**: cámaras reales en Windows, macOS y Linux (solo compilan en CI), varias cámaras a la vez, desconectar
  la cámara durante la transmisión.

## Obico

El detector de fallas de Obico está ajustado para **una imagen cada 10 s**: en `obico-server`
(`backend/config/settings.py`) `MIN_DETECTION_INTERVAL = 10`, «Recommended not to change as the hyper parameters are
tuned based on interval = 10s», y sus ventanas se cuentan en imágenes (`EWM_SPAN` 12, `ROLLING_WIN_SHORT` 310,
`INIT_SAFE_FRAME_NUM` 30). Pasarle más imágenes por segundo sin más cambiaría esos tiempos y daría más falsas alarmas.
Lo que sí aprovecha una cámara más rápida y mejor ubicada: varias imágenes por cada 10 s (por ejemplo la mediana de sus
puntajes, menos ruido por el cabezal tapando la pieza o por movimiento) entregadas al mismo algoritmo, y mejor imagen y
ángulo. Eso va con la detección distribuida y el puente de la PC, todavía pendientes.
