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

## Cómo funciona

- **Estado** (página Dispositivos y botón Probar): MQTT 3.1.1 sobre TLS (puerto 8883, usuario `bblp`, contraseña =
  código de acceso), comando `pushall` en `device/<serie>/request` y lectura de `device/<serie>/report`
  (`gcode_state`, `mc_percent`, `mc_remaining_time`, temperaturas).
- **Enviar e imprimir**: el G-code se empaqueta como `<nombre>.gcode.3mf` (`Metadata/plate_1.gcode` y su MD5), se
  sube por FTPS implícito (puerto 990) a la raíz de la tarjeta y se inicia con el comando `project_file`.
- El TLS lo hace libcurl (OpenSSL en Linux, Schannel en Windows): no hay librerías nuevas. Se verifica que el
  certificado lo haya emitido la CA de Bambu; no se comprueba el nombre porque es el número de serie, no la IP.
- Código: `src/slic3r/Utils/BambuLanClient.*` (protocolo, sin GUI) y `BambuLan.*` (host de impresión).

## Pruebas

- `slic3rutils_tests "[BambuLan]"`: paquetes MQTT, lectura de reportes, archivo `.gcode.3mf` (MD5 contra `md5sum`),
  comando de inicio.
- Impresora simulada `tools/bambu/fake_printer.py` (solo biblioteca estándar de Python y `openssl`): TLS con una CA
  de prueba, MQTT y FTPS implícito. `TISMA_BAMBU_FAKE=<dir> slic3rutils_tests "[BambuLanFake]"` prueba estado,
  código incorrecto, certificado ajeno, subida con progreso, inicio y cancelación.
- **Sin probar todavía con una impresora real.** Puntos a confirmar: que el firmware acepte el `.gcode.3mf` mínimo
  (sin miniaturas ni `slice_info` completo) y la reutilización de sesión TLS del FTPS con Schannel en Windows.

## Límites

- Sin cámara, sin mapeo de bandejas del AMS (usa el orden por defecto) y sin calibraciones previas a la impresión
  (se envía nivelación de cama activada y el resto desactivado).
- Si se cancela una subida, puede quedar un archivo parcial en la tarjeta.
