# Planchas (varias camas)

Tisma usa las varias camas de PrusaSlicer 2.9 (hasta 9, «Laminar todo», «Exportar todos los G-code») y agrega lo
que les faltaba frente a OrcaSlicer.

## Qué tiene cada plancha

En la vista 3D, cada plancha muestra una etiqueta en su esquina trasera izquierda con el número, el nombre, el botón
**Bloquear** y el botón **Ajustes** (también en Editar > Ajustes de plancha, para la plancha activa). Un `*` indica
que la plancha cambia ajustes de impresión.

| Ajuste | Efecto |
|---|---|
| Nombre | Se muestra en la etiqueta y reemplaza `_bed<n>` en el nombre del G-code al exportar o enviar todas las planchas. |
| Bloqueo | Organizar (A, Mayús+A) no mueve las piezas que había al bloquear ni pone otras en la plancha; «+» sobre una pieza de una plancha bloqueada manda la copia a otra plancha. Organizar o llenar la plancha actual se niega con un aviso. |
| Orden de impresión | Como en los ajustes, por capa o por objeto (`complete_objects`) solo en esta plancha. |
| Jarrón en espiral | Como en los ajustes, activado o desactivado (`spiral_vase`). |
| Temperaturas de cama | Primera capa y resto (°C) para todos los extrusores; 0 = la del filamento. |

Los ajustes se aplican al configurar el laminado de esa plancha (`ModelPlate::apply_to` en
`src/libslic3r/ModelPlate.cpp`); la línea de comandos aplica los de la primera plancha de un 3MF.

## Proyectos

Se guardan en `Metadata/Tisma_plates.xml` dentro del 3MF, solo si alguna plancha no está por defecto. PrusaSlicer
ignora el archivo. Al abrir un proyecto, una plancha bloqueada fija todo lo que tenga.

## Límites conocidos

- El organizado secuencial (con «Imprimir objeto por objeto» activado globalmente) no respeta el bloqueo.
- El orden por objeto de una plancha no cambia la vista de los contornos de colisión del extrusor, que siguen el
  ajuste global; la comprobación al laminar sí usa el de la plancha.
- Al cargar archivos nuevos con una plancha bloqueada activa, las piezas pueden quedar encima: «A» las mueve.
