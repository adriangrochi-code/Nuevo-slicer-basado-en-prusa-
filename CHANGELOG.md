# Tisma Slicer: registro de cambios

Numeración propia de Tisma (`TISMA_VERSION` en `version.inc`), con el formato MAYOR.MENOR.PARCHE:

- **MAYOR**: 1 cuando las funciones principales estén validadas en impresoras reales; después, cambios grandes.
- **MENOR**: funciones nuevas.
- **PARCHE**: correcciones.

Las compilaciones de prueba del CI muestran además `+<ejecución>-<commit>` (por ejemplo `TismaSlicer-0.9.0+57-1a2b3c4`).
`SLIC3R_VERSION` sigue siendo la versión de PrusaSlicer en la que se basa (2.9.6): la usan los perfiles, los 3MF y
las instantáneas de configuración para la compatibilidad, y no cambia con las versiones de Tisma.

## Sin publicar (0.10.0)

- **Perímetros escalonados** (alternativa a Brick Layers fuera de la patente US11331848B2, ver
  `docs/BRICK_LAYERS.md`): opción «Perímetros escalonados» en Capas y perímetros; uno de cada dos perímetros
  internos media capa más arriba, el externo sin cambios, flujo ajustado en la primera y la última capa.
- **Ingeniería**: zonas con desplazamiento máximo permitido; rojo al llegar al límite y negro donde rompe.
- **Rediseño Órbita Pro**: tema grafito, pestañas de trabajo arriba con chip de impresora, panel de Ingeniería
  por secciones, objetos a la izquierda, barra de estado y resumen del laminado con los ajustes usados.
- **Planchas al estilo de OrcaSlicer** (sobre las varias camas de PrusaSlicer 2.9): nombre por plancha, bloqueo
  (organizar no mueve lo que había al bloquear ni pone nada más; las copias con «+» van a otra plancha) y ajustes
  propios por plancha (orden de impresión, jarrón en espiral y temperaturas de cama) que solo cambian su G-code.
  Etiqueta con botones en cada plancha y menú Editar > Ajustes de plancha; se guardan en el 3MF
  (`Metadata/Tisma_plates.xml`) y el G-code exportado lleva el nombre de la plancha.
- **Impresoras Bambu Lab por LAN** (modo Solo LAN + Desarrollador) sin el plugin cerrado de Bambu: estado en
  Dispositivos, prueba de conexión, envío e inicio de la impresión (MQTT y FTPS sobre TLS, según OpenBambuAPI).
  curl de Windows compilado con FTP.
- **Análisis aerodinámico** en Ingeniería (pestaña propia, separada de la estructural): arrastre, fuerza transversal y
  mapa de presión con una simulación lattice Boltzmann propia, más rozamiento con la rugosidad de las capas por cara.
  Validado con placa, cubo y prismas (`docs/AERO.md`).
- **Coasting** (como Simplify3D): ajuste de filamento «Distancia de coasting»; los últimos milímetros de cada
  recorrido se hacen sin extruir (`G1 X Y` sin E) para que la presión de la boquilla cierre la línea. No afecta a
  recorridos de menos de 3 veces la distancia ni al modo jarrón. Calibración «Distancia de coasting»: fila de torres
  huecas con la costura atrás, una distancia por torre, y su resultado se aplica al filamento.
- La comprobación de versión ya no anuncia versiones de PrusaSlicer como actualizaciones de Tisma.

## 0.9.0 (beta, sin validar todavía en impresoras reales)

Base: PrusaSlicer 2.9.6.

- **Ingeniería**: análisis estructural (FEA) con relleno y paredes, relleno mínimo, refuerzos locales, lattice 3D,
  calidad de vóxeles, adherencia según temperatura, orientación recomendada; el material sigue las capas curvas.
- **Capas no planares**: ondas y cónico, caudal uniforme, límites del eje Z, comprobación de colisiones del cabezal
  con el perfil medido.
- **Calibraciones**: temperatura, pressure advance, retracción, velocidad volumétrica, VFA, aceleración, esquinas,
  input shaping, primera capa por zonas, flujo en porcentaje, y no planar (galga estática por alturas, pendiente
  máxima, velocidad Z, galga del cabezal y asistente en dos pasos); aplicar resultados a los perfiles.
- **Impresoras**: 902 perfiles convertidos desde OrcaSlicer (63 fabricantes) que laminan; selector por marca con
  buscador; impresoras de cinta con ángulo configurable.
- **Interfaz**: render con luz por píxel y sombras, panel derecho con ajustes rápidos, página de Dispositivos (USB y
  red con la interfaz web de la impresora), impresión por USB.
- **Rendimiento y correcciones**: laminado 30–45 % más rápido; correcciones de PrusaSlicer 3.0 portadas (SPE-2783,
  SPE-3329, SPE-3488, SPE-3866, SPE-3377, SPE-3414, #15768 y otras); sin envío de datos del sistema a Prusa.
- **Windows**: instalador (Inno Setup) y firma preparada con SignPath.
