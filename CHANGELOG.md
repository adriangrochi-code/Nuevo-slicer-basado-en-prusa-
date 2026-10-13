# Tisma Slicer: registro de cambios

Numeración propia de Tisma (`TISMA_VERSION` en `version.inc`), con el formato MAYOR.MENOR.PARCHE:

- **MAYOR**: 1 cuando las funciones principales estén validadas en impresoras reales; después, cambios grandes.
- **MENOR**: funciones nuevas.
- **PARCHE**: correcciones.

Las compilaciones de prueba del CI muestran además `+<ejecución>-<commit>` (por ejemplo `TismaSlicer-0.9.0+57-1a2b3c4`).
`SLIC3R_VERSION` sigue siendo la versión de PrusaSlicer en la que se basa (2.9.6): la usan los perfiles, los 3MF y
las instantáneas de configuración para la compatibilidad, y no cambia con las versiones de Tisma.

## Sin publicar (0.10.0)

- **Piel + núcleo en un clic** (Ingeniería > Optimizar > «Skin + core»): la piel con capas no planares automáticas y
  el núcleo con el relleno mínimo por zonas y nervios según las tensiones, calculado con esas capas y aplicado al
  terminar. Sin caras fijas y cargas aplica solo la piel.
- **Relleno nervado según las tensiones**: al aplicar el relleno mínimo de Ingeniería, donde la pieza trabaja en una
  dirección (traccionada o a flexión) el relleno se pone en líneas paralelas a esa dirección en todas las capas, como
  nervios, con modificadores «Infill ribs N°». El análisis los cuenta: el relleno alineado es más rígido y
  resistente a lo largo de sus líneas y mucho más débil a lo ancho, y el relleno mínimo solo usa los nervios si queda
  más liviano (en una probeta de tracción, 27 % en vez de 31 %). También se analiza bien el patrón «Aligned
  Rectilinear» del perfil (`docs/FEA.md`).
- **No planar «Automático» (acabado y menos soportes)**: un solo modo que inclina las capas bajo los voladizos y las
  pone paralelas a las caras superiores, dentro de lo que permite el cabezal. En las pruebas: un hongo con ala a 48°
  pasa de 683 mm de filamento de soporte a ninguno, y la cara superior inclinada de 0,127 a 0,058 mm de dispersión
  (`docs/NONPLANAR_ANALYSIS.md`).
- **Proyectos de Bambu Studio y OrcaSlicer**: se abren como proyecto, con cada objeto y sus partes (antes cada parte
  salía como objeto suelto), nombres, extrusores, modificadores con sus ajustes, pintura de colores, placas en las camas
  de Tisma y los ajustes de impresión y filamento traducidos; la impresora sigue siendo la elegida en Tisma
  (`docs/BAMBU_PROJECTS.md`).
- **Abrir un 3MF**: el diálogo explica cada opción y ya no dice «PrusaSlicer».
- **No planar con dos variables**: la página «No planar» de la configuración de impresión solo elige el tipo de capas.
  Cuánto se curvan sale de las dos medidas del cabezal (ángulo libre y holgura boquilla-cabezal, en la impresora): las
  ondas suben hasta la holgura con la pendiente del ángulo, y ondas y conos se suavizan solos si el cabezal fuera a
  chocar con lo ya impreso. Los perfiles con amplitud o longitud de onda puestas a mano pasan a ajustarse al cabezal.
- **Arc overhangs**: velocidad propia de los arcos (`overhang_arcs_speed`, 5 mm/s por defecto, la que usa el autor de
  Arc Overhang) y distancia máxima (`overhang_arcs_max_distance`, 0 = sin límite; más allá el voladizo se imprime como
  siempre). Dos calibraciones nuevas: «Arc overhang speed» y «Arc overhang maximum distance», con sus resultados en
  «Aplicar un resultado de calibración». La opción sigue apagada por defecto (experimental) y está en los ajustes
  rápidos.
- **Anti-aliasing en Z con capas no planares**: se pueden usar juntos (menos soporte con el modo voladizos y caras
  superiores con menos escalones). Dispersión de la cara superior: 0,17 → 0,10 mm en una cuña con capas en ondas
  (`docs/Z_ANTIALIASING.md`).
- **No planar «Superficie» (acabado)**: las capas de arriba se ponen paralelas a las caras superiores de la pieza, tanto
  como deja el cabezal, para que una cara inclinada o curva salga como una capa continua y no en escalones. En un bloque
  de 24 mm con la cara inclinada 5,7° la dispersión de la cara pasa de 0,127 mm (plano) a 0,058 mm
  (`docs/NONPLANAR_ANALYSIS.md`).
- **Asistente de configuración**: con impresoras convertidas de Cura u OrcaSlicer ya se puede terminar. Antes volvía
  a pedir los filamentos por defecto sin fin y solo dejaba avanzar con una impresora personalizada. Los perfiles nombran
  los filamentos de la biblioteca como se cargan (« @Template»).
- **Capas curvas por tensiones**: Ingeniería calcula capas no planares que se inclinan hacia donde tiran de la pieza,
  tanto como deja el cabezal, y las compara con capas planas (factor de seguridad). Con «Usar las capas curvas», el
  modo no planar «Tensiones» las lamina (`docs/NONPLANAR_ANALYSIS.md`).
- **Relleno entrelazado (no planar)**: el relleno disperso ondula en Z con la misma huevera en cada capa, para que las
  capas encastren en lugar de apoyarse en planos; paredes y superficies quedan planas (`docs/NONPLANAR_ANALYSIS.md`).
- **Orientación automática para FDM y resina**: en el gizmo de rotación, «Menos material de soporte» (FDM: apoya la
  pieza donde los voladizos piden menos soporte y con buen contacto con la cama) y «Menor fuerza de despegue» (resina:
  sin ventosas y con la capa más grande lo más chica posible) (`docs/ORIENTATION.md`).
- **Resina: detector de ventosas**: al laminar avisa de los huecos cerrados abajo y abiertos hacia el film que el vacío
  puede arrancar, con la altura, el área y cómo arreglarlo (`docs/RESINA.md`).
- **Avisos de laminado de cada pieza**: ya no desaparecen al terminar de laminar (afectaba sobre todo a resina).
- **296 impresoras nuevas tomadas de UltiMaker Cura** (las que Tisma no tenía), con procesos genéricos y filamentos de
  1,75 / 2,85 mm; todas laminan (`docs/PRINTER_PROFILES.md`).
- **Anti-aliasing en Z**: las caras superiores inclinadas siguen al modelo en lugar de salir en escalones; el relleno
  superior y los perímetros bajan dentro de la última capa con menos flujo (`docs/Z_ANTIALIASING.md`).
- **No planar solo en voladizos** (modo «Voladizos»): las capas se inclinan únicamente bajo los voladizos que
  necesitarían soporte, tanto como permite el cabezal; menos soporte y material (`docs/NONPLANAR_ANALYSIS.md`).
- **No planar ajustado al cabezal**: ángulo libre en X y en Y en Configuración de impresora > Cabezal (el asistente de
  calibración no planar los mide con una galga plana de ranuras en V de distintos ángulos y profundidades, en X y en
  Y: se ingresa la altura libre y el ángulo de cada fila); con «Ajustar al cabezal» la amplitud de las ondas y el ángulo
  del cono salen de esos ángulos y de la altura libre, no de valores fijos (`docs/NONPLANAR_ANALYSIS.md`).
- **Impresión por USB como en Cura**: la impresora se detecta sola (puerto y velocidad) al enchufarla; al laminar, el
  botón principal es «Imprimir por USB» y la flecha de al lado despliega las demás salidas (exportar G-code, tarjeta SD,
  enviar a la impresora). La página Device muestra el monitor: temperaturas con precalentado y gráfico, movimiento de
  los ejes, extrusor y ventilador, G-code, impresión activa (progreso, tiempo, pausar, cancelar) y terminal.
- **Flecha para ocultar los ajustes** de la barra derecha: la barra desaparece y la vista 3D ocupa todo el ancho, con
  los botones de laminar y de salida flotando abajo a la derecha (como en Cura); «<<» vuelve a mostrar los ajustes.
- **Thingiverse en lugar de Printables**: la página Thingiverse busca modelos y «Descargar e importar este modelo» (o
  el botón de descarga del sitio) los baja y los carga en la plataforma; un zip abre el selector de archivos.
- **Ingeniería con Estructuras en un solo botón**, visible en todos los modos con impresoras FFF.
- **Lista de objetos abajo en la barra derecha**, como en PrusaSlicer (la columna de objetos a la izquierda se quitó).
- **Modo principiante**: la configuración avisa que la mayoría de los ajustes están ocultos y ofrece mostrarlos todos.
- **Bambu Lab por LAN en Windows**: corregido «TLS: schannel: the revocation status is unknown».
- **Resina: impresoras Elegoo con formato GOO** (`docs/RESINA.md`): exportación directa a `.goo` (el formato abierto
  de Elegoo, sin pasar por UVtools) y perfiles de Mars 4, Mars 4 Ultra, Mars 4 Max, Mars 5, Saturn 3, Saturn 3 Ultra y
  Saturn 4 con resina estándar genérica. La barra de estado y el resumen del laminado muestran tiempo y mililitros de
  resina.
- **Calibraciones de resina**: torre de exposición (un tiempo por banda de capas, en impresoras GOO), bloque de
  medidas y corrección dimensional del material con X, Y y Z medidos; «Aplicar un resultado» escribe la exposición.
- **Modo resina sin funciones de FDM**: con una impresora de resina se ocultan Ingeniería y Estructuras, y Calibración
  muestra las pruebas de resina en lugar de las de FDM.
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
