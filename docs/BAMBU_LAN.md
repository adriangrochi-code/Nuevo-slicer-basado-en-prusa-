# Impresoras Bambu Lab por LAN

Tisma envía trabajos a impresoras Bambu Lab y lee su estado **sin el plugin de red cerrado de Bambu**, con una
implementación propia escrita a partir de la documentación de la comunidad
[OpenBambuAPI](https://github.com/Doridian/OpenBambuAPI).

## Requisitos en la impresora

- Modo **Solo LAN** y **modo Desarrollador** activados (Ajustes > Solo LAN). Desde enero de 2025 el firmware solo
  acepta control de terceros así.
- El **código de acceso** que muestra la pantalla en ese menú.
- La impresión por la nube de Bambu no está disponible (haría falta su plugin o sus credenciales).

## Configuración en Tisma

Dispositivos > Agregar impresora de red (o Configuración > Impresoras físicas):

| Campo | Valor |
|---|---|
| Tipo de host | Bambu Lab (LAN) |
| Nombre de host | Dirección IP de la impresora |
| Clave API | Código de acceso |
| Archivo CA | Vacío: se usa `resources/cert/bambu_printer_ca.pem` (CA de Bambu, tomadas de Bambu Studio, AGPL) |
| Imprimir desde el AMS | Si el filamento sale del AMS en lugar de la bobina externa |

El número de serie no se pide: es el nombre (CN) del certificado de la impresora.

El perfil de impresora debe generar **G-code de texto** (no binario) y con el G-code de inicio de Bambu (los perfiles
convertidos de OrcaSlicer ya lo traen).

### Enviar desde el botón principal

Como con una impresora USB: si la impresora en uso (su perfil) tiene una impresora de red guardada (cualquier tipo de
host salvo Prusa Connect) y esa impresora responde en la red, después de laminar el botón principal pasa a «Enviar a la
impresora», sin tener que elegir la impresora física en la lista. Tisma prueba cada 20 s si se puede abrir una conexión
TCP con su dirección (el puerto de la dirección, o el del tipo de host: 8883 Bambu Lab, 8899 Flashforge, 8080 MKS, 80 los
servidores web); si deja de responder, el botón vuelve a «Exportar». Con varias impresoras de red para el mismo perfil
se usa la última a la que se envió (`Plater::priv::find_network_target()`, `Utils/PrinterProbe`).

## Cómo funciona

- **Estado** (página Dispositivos y botón Probar): MQTT 3.1.1 sobre TLS (puerto 8883, usuario `bblp`, contraseña =
  código de acceso), comando `pushall` en `device/<serie>/request` y lectura de `device/<serie>/report`
  (`gcode_state`, `mc_percent`, `mc_remaining_time`, temperaturas).
- **Enviar e imprimir**: el G-code se empaqueta como `<nombre>.gcode.3mf` (`Metadata/plate_1.gcode` y su MD5), se
  sube por FTPS implícito (puerto 990) a la raíz de la tarjeta y se inicia con el comando `project_file`.
- El TLS lo hace libcurl (OpenSSL en Linux, Schannel en Windows): no hay librerías nuevas. Se verifica que el
  certificado lo haya emitido la CA de Bambu; no se comprueba el nombre porque es el número de serie, no la IP.
- **Windows (Schannel)**: los certificados de las impresoras no indican lista de revocación ni OCSP, y Schannel
  cortaba con «the revocation status is unknown»; las dos conexiones desactivan solo esa comprobación
  (`CURLSSLOPT_NO_REVOKE`), la cadena se sigue verificando contra la CA de Bambu.
- **FTPS y reutilización de sesión TLS**: el servidor FTP de las impresoras es vsftpd y exige que la conexión de
  datos reanude la sesión TLS de la de control («SSL connection failed: session reuse required», informado en el
  [foro de Bambu Lab](https://forum.bambulab.com/t/issues-with-ftp-connection/11518)). Con TLS 1.3 el ticket de
  sesión llega después del saludo y libcurl con GnuTLS no la reanuda (comprobado contra la impresora simulada: 522);
  por eso la subida usa TLS 1.2, donde basta el ID de sesión del saludo. MQTT no cambia. Además, curl 8.21 (el
  de las dependencias, con OpenSSL o Schannel) solo reanuda la sesión si se verificó el nombre del servidor:
  la subida lee primero el número de serie del certificado del FTPS (su CN), se conecta al nombre `<serie>`
  dirigido a la IP de la impresora (`CURLOPT_CONNECT_TO`) y verifica el nombre. Si el certificado no lleva la serie
  como nombre, repite la subida sin verificar el nombre (comprobado con curl 8.21 + OpenSSL 4.0.1: sin verificar
  el nombre, 522; verificándolo, la subida termina).
- La respuesta a `PASV` trae la dirección 0.0.0.0 ([foro](https://forum.bambulab.com/t/we-can-now-connect-to-ftp-on-the-p1-and-a1-series/6464?page=3));
  libcurl usa la dirección de la conexión de control (`CURLOPT_FTP_SKIP_PASV_IP`, activo por defecto).
- **Cámara** (página Dispositivos, A1 y P1): TLS al puerto 6000, un paquete de autenticación de 80 bytes (0x40 y
  0x3000 en little endian, el usuario `bblp` y el código de acceso rellenados con ceros a 32 bytes) y después
  imágenes JPEG, cada una tras una cabecera de 16 bytes cuyo primer entero es el tamaño (OpenBambuAPI, `video.md`).
  Se muestran mientras la impresora está seleccionada; al elegir otra o salir de la página se corta la conexión.
  Las series X1, P2S y H2 sirven la cámara por RTSPS (puerto 322), que Tisma todavía no lee: si nada escucha en el
  puerto 6000 se avisa eso.
- Código: `src/slic3r/Utils/BambuLanClient.*` (protocolo, sin GUI) y `BambuLan.*` (host de impresión); la vista de
  la cámara está en `src/slic3r/GUI/DevicesPanel.cpp`. La app de Android usa el mismo protocolo
  (`android/.../printers/`, rama `tisma/android`).

## Pruebas

- `slic3rutils_tests "[BambuLan]"`: paquetes MQTT, lectura de reportes, archivo `.gcode.3mf` (MD5 contra `md5sum`),
  comando de inicio, paquete de autenticación y cabeceras de la cámara.
- Impresora simulada `tools/bambu/fake_printer.py` (solo biblioteca estándar de Python y `openssl`): TLS con una CA
  de prueba (sin lista de revocación, como las de Bambu), MQTT y FTPS implícito que exige reanudar la sesión TLS y
  responde `PASV` con 0.0.0.0, y la cámara del puerto 6000 (`--camera-images` con JPEG propios, o una imagen de
  relleno). `TISMA_BAMBU_FAKE=<dir> slic3rutils_tests "[BambuLanFake]"` prueba estado, código incorrecto,
  certificado ajeno, subida con progreso, inicio, cancelación, imágenes de la cámara, cámara con código incorrecto y
  sin cámara en el puerto. Con `--ssdp-port 1990` anuncia la impresora en la red como las reales (la app de Android
  las encuentra así).
- **CI**: el test contra la impresora simulada corre en Linux (OpenSSL) y en Windows (Schannel, el TLS que usa el
  programa allí). En Windows el registro muestra además el `curl.exe` del sistema con y sin comprobación de
  revocación.
- **Sin probar todavía con una impresora real.** Puntos a confirmar: que el firmware acepte el `.gcode.3mf` mínimo
  (sin miniaturas ni `slice_info` completo), que su FTPS acepte TLS 1.2 (vsftpd lo acepta salvo que se configure
  solo TLS 1.3; no hay documentación de Bambu al respecto) y qué hace la cámara con un código incorrecto (la
  impresora simulada cierra la conexión; la documentación no lo dice).

## Límites

- Sin la cámara RTSPS de las series X1, P2S y H2, sin mapeo de bandejas del AMS (usa el orden por defecto) y sin calibraciones previas a la impresión
  (se envía nivelación de cama activada y el resto desactivado).
- Si se cancela una subida, puede quedar un archivo parcial en la tarjeta.
