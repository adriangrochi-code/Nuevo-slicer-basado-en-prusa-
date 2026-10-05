# Riesgos técnicos — Tisma Slicer

Fecha: 2026-10-05. Probabilidad (P) e impacto (I): Alto / Medio / Bajo.

| # | Riesgo | P | I | Mitigación |
|---|---|---|---|---|
| 1 | **Fusiones con upstream** costosas: el fork toca `GCode.cpp`, `PrintConfig.*`, `Plater.cpp`, `GLCanvas3D.cpp`, y `GLCanvas3D.cpp` cambió de CRLF a LF (15 942 líneas de diff falso). | A | A | Restaurar los finales de línea originales; mantener el código nuevo en archivos propios con enganches pequeños; etiquetar los cambios del fork con comentarios `Tisma`; fusionar upstream en cada versión menor. |
| 2 | **Migración a Vulkan**: no hay abstracción gráfica; habría que reescribir `GLCanvas3D`, `GLModel`, shaders, `libvgcode` y el backend de ImGui. | A | A | Posponer a la Fase 8; empezar por una abstracción mínima aplicada a lo nuevo; medir primero si OpenGL es realmente un cuello de botella. |
| 3 | **Fiabilidad del FEA**: material FDM anisótropo y poroso, adhesión entre capas variable, datos de material escasos; posibles malas interpretaciones como "garantía de resistencia". | A | A | Validar con casos analíticos y probetas reales; mostrar hipótesis y límites en cada resultado; factores de seguridad configurables; nunca etiquetar resultados como certificados. |
| 4 | **Rendimiento del FEA**: piezas grandes → millones de vóxeles; memoria y tiempo. | M | A | Resolución adaptativa, precondicionadores, ejecución en `Job` cancelable, límites de memoria; GPU opcional después. |
| 5 | **Referencias estables a geometría CAD** (nombrado topológico): los ids de cara cambian si el CAD se modifica. | A | M | Garantizar solo el retesselado del mismo STEP; detectar selecciones inválidas por hash y avisar; reproyección asistida. |
| 6 | **Selecciones pintadas** ligadas a triángulos: se pierden al simplificar, cortar o reemplazar la malla. | M | M | Hash de malla + aviso (como en §5); usar ids de cara CAD cuando existan. |
| 7 | **No planar**: colisiones del cabezal con la pieza, límites del eje Z, firmware. Ya hay validación de pendiente y jacobiano, pero no de colisiones. | M | A | Modelo de cabezal configurable por impresora; detección de colisiones antes de exportar; pruebas físicas progresivas. |
| 8 | **Impresora de cinta**: afecta a primera capa, soportes, brim, vista previa, límites de cama y estimación de tiempos. | M | M | Transformación aislada en `Manufacturing/`; perfiles por máquina; no fijar ángulos; empezar por una máquina de referencia del usuario. |
| 9 | **Licencias**: OpenSSL 1.1.0l (licencia incompatible con AGPL), OpenCSG (verificar "o posterior"), paquetes GPL de CGAL (impiden relicenciar). | M | A | Actualizar OpenSSL a 3.x o usar TLS del sistema; verificar `COPYING` de OpenCSG; documentar todo en `DEPENDENCIES_AND_LICENSES.md`. |
| 10 | **Dependencias antiguas** con vulnerabilidades: curl 7.75, OpenSSL 1.1.0l, expat 2.4.3, libpng 1.6.35. | A | M | Plan de actualización en la Fase 2, con pruebas de regresión. |
| 11 | **Sin pruebas de GUI** y compilaciones lentas (≈1 h en 4 núcleos; CI de Windows similar). | A | M | Pruebas automáticas del núcleo para todo lo nuevo; guion de humo de la GUI con Xvfb; caché de dependencias en CI. |
| 12 | **Compatibilidad de proyectos**: archivos 3MF nuevos deben abrir en PrusaSlicer y en versiones anteriores de Tisma. | B | M | Datos nuevos en archivos aparte versionados; pruebas de ida y vuelta. |
| 13 | **Calibración persistente**: las opciones `calib_*` siguen activas si el usuario añade su propio modelo a una prueba. | M | M | Aviso visible mientras haya una prueba activa y botón para desactivarla. |
| 14 | **Alcance**: la especificación cubre CAD, FEA, optimización, lattice, no planar, cinta, Vulkan; riesgo de muchas funciones a medias. | A | A | Fases con criterios de aceptación; no empezar una fase sin cerrar la anterior; marcar lo experimental como tal en la interfaz. |
| 15 | **Validación física**: varias funciones (calibraciones, no planar, arc overhangs, FEA) solo se han verificado en software. | A | M | Registro de pruebas impresas por función; el usuario aporta resultados de su máquina (CR-5 Pro H). |
| 16 | **Trackhead** sin definición y **Arc Overhang** con comportamiento definitivo pendiente. | — | M | No implementar hasta tener referencias del usuario. |
| 17 | **Diseño de la interfaz**: la columna de navegación se solapa en ventanas bajas; el tema oscuro solo se ha visto en Linux. | A | B | Corregir el cálculo de posiciones; probar en Windows con la compilación de CI. |

## Riesgos que bloquean decisiones

- Solver FEM definitivo (propio sobre vóxeles vs biblioteca externa) — ver `ARCHITECTURE.md` §6.
- Alcance de Vulkan: ¿solo cómputo, solo renderizado, o ambos?
- Máquinas de referencia para cinta y no planar.
