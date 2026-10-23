# Interfaz nueva de Tisma Slicer (PC)

Decisión del dueño del proyecto: la interfaz de PC se escribe de cero en C++, con el diseño de la app de Android, y
conserva todas las funciones que existen. Se reutilizan el motor (libslic3r), el visor 3D (`GLCanvas3D`: render con
oclusión ambiental, PBR y sombras, herramientas 3D, vista previa del G-code), los trabajos en segundo plano (laminado,
ordenar, subidas) y las funciones propias de Tisma (calibraciones, Ingeniería, nube, dispositivos). Lo de PrusaSlicer
que la interfaz nueva reemplaza se borra cuando deja de usarse.

## Principios

- La misma estructura, los mismos nombres y los mismos íconos que el teléfono (`resources/icons/tisma_*.svg` son los
  dibujos de `android/app/src/main/res/drawable`), los colores de `TismaTheme`, tema claro y oscuro.
- Ninguna función se pierde: el inventario de abajo dice a dónde va cada una.
- Por etapas: cada una compila, se prueba en pantalla (Xvfb) y se guarda en su commit; mientras tanto conviven la
  parte nueva y la que todavía no se reemplazó.
- Textos en inglés con su traducción al español.

## Estructura

- **Barra superior**: logo, secciones (Placa, Impresión, Filamento o Material, Impresora, Dispositivos, Más),
  búsqueda, impresora en uso y su conexión, cuenta.
- **Placa**: a la izquierda los grupos de herramientas (Archivo, Vista, Organizar, Pintar, Ingeniería, Laminar) y las
  herramientas del grupo elegido; en el centro la vista 3D con los ajustes en uso arriba (chips que abren su página),
  el zoom a la derecha y el estado abajo; a la derecha el panel de la placa: objetos, propiedades de la selección,
  ajustes por objeto y el botón principal (Laminar, Exportar o Enviar).
- **Impresión, Filamento, Impresora**: páginas propias, con el perfil arriba (elegir, guardar, renombrar, borrar,
  comparar) y los ajustes en grupos con títulos grandes, como en el teléfono; búsqueda y modo simple o experto.
- **Dispositivos**: impresoras por USB y por red (estado, cámara, envío). Cada impresora puede usar una cámara USB
  de la computadora (ver `docs/USB_CAMERA.md`).
- **Más**: calibración, Thingiverse, cuenta de Tisma Cloud, preferencias, asistente de configuración, ayuda.

## Inventario: de dónde viene cada función y a dónde va

| Hoy (PrusaSlicer) | En la interfaz nueva |
|---|---|
| Barras de la vista 3D (agregar, borrar, ordenar, copias, dividir, altura variable, deshacer, herramientas 3D, 3D/vista previa) | Herramientas de la placa (hecho) |
| Menú Archivo: proyecto nuevo, abrir, guardar, importar (modelos, archivos de resina, ZIP, configuración), exportar (G-code, SD, nube, placa STL/OBJ, trayectorias, configuración), convertir G-code binario | Grupo Archivo (lo frecuente) y Más > Archivo (el resto); atajos de teclado iguales |
| Menú Editar: seleccionar, borrar, deshacer, copiar y pegar, recargar, buscar, galería de formas, cola de subidas, comparar perfiles | Grupo Archivo, panel de objetos, búsqueda de la barra, Dispositivos (cola), página de cada perfil (comparar); atajos |
| Menú Ventana y Vista | Secciones de la barra y grupo Vista |
| Menú Configuración y Calibración, Ayuda | Más |
| Panel lateral: perfiles, boquilla, tipo de cama, ajustes rápidos | Chips sobre la vista y páginas de ajustes; ajustes rápidos en el panel de la placa |
| Panel lateral: lista de objetos, posición/rotación/escala, ajustes por objeto, capas por rango | Panel de la placa |
| Panel lateral: datos del laminado y botones de acción | Barra de estado, grupo Laminar y botón principal |
| Pestañas de ajustes (`Tab`) | Páginas de ajustes nuevas |
| Notificaciones de la vista 3D | Se mantienen (estilo nuevo más adelante) |

## Qué se reutiliza y qué se escribe

- **Se reutiliza**: `Plater` como dueño del modelo, de las vistas y del laminado (sin su distribución de paneles),
  `GLCanvas3D` y las herramientas 3D, `PresetBundle` y las reglas de los ajustes (`ConfigManipulation`), las
  definiciones de las opciones (`PrintConfigDef`: nombres, ayudas, unidades, límites), los trabajos y los diálogos
  propios de Tisma.
- **Se escribe**: la ventana y su barra, las herramientas de la placa (hecho), el panel de la placa, las páginas de
  ajustes, la página Más y un juego de componentes propios (tarjeta, chip, botón de herramienta, opciones en chips,
  interruptor, campo numérico, título de grupo).

## Etapas

1. Herramientas de la placa a la izquierda de la vista 3D (hecho, e1bde8f).
2. Secciones de la barra superior (hecho, 5090783).
3. Sin las barras de PrusaSlicer en la vista 3D (hecho, 1b9b3f5).
4. Componentes propios en C++ con tema claro y oscuro (en curso: chip, título de grupo, opciones en chips; campos
   redondeados e interruptores en todo el programa; títulos planos en lugar de los marcos de PrusaSlicer; las etiquetas
   de los ajustes sin dos puntos; los botones de Tisma en los diálogos de mensajes, de impresora en red, de guardar
   perfil, de cambios sin guardar, de la cuenta de Tisma Cloud y en el asistente de configuración, con Enter y Escape
   como los botones nativos; títulos de sección en las listas de perfiles; las notificaciones de la vista 3D en
   morado).
5. Panel de la placa nuevo (en curso): tarjetas con títulos grandes; la impresora por su modelo, con la boquilla y la
   cama al lado como en OrcaSlicer y Bambu Studio (la boquilla elige el perfil del modelo para ella o pone su diámetro
   en la impresora; la cama se elige para toda impresora de filamento); soportes, relleno y borde en chips; la lista de
   objetos en todos los modos; la rueda del mouse sobre un campo desplaza el panel en lugar de cambiar el valor.
   Falta: chips de ajustes sobre la vista.
6. Páginas de ajustes nuevas (Impresión, Filamento, Impresora; también resina).
7. Barra superior propia y menú reorganizado (en macOS el menú del sistema sigue).
8. Página Más (hecho: tarjetas armadas desde los menús, con sus atajos y su estado, más Thingiverse, la cuenta de
   Tisma Cloud y las preferencias; falta: preferencias propias).
9. Borrar lo de PrusaSlicer que quedó sin uso; pruebas de pantalla de todas las secciones; documentación.

Cada etapa se prueba con el programa en Xvfb (capturas de las pantallas cambiadas) y con `ctest`.
