# Tisma Slicer: registro de cambios

Numeración propia de Tisma (`TISMA_VERSION` en `version.inc`), con el formato MAYOR.MENOR.PARCHE:

- **MAYOR**: 1 cuando las funciones principales estén validadas en impresoras reales; después, cambios grandes.
- **MENOR**: funciones nuevas.
- **PARCHE**: correcciones.

Las compilaciones de prueba del CI muestran además `+<ejecución>-<commit>` (por ejemplo `TismaSlicer-0.9.0+57-1a2b3c4`).
`SLIC3R_VERSION` sigue siendo la versión de PrusaSlicer en la que se basa (2.9.6): la usan los perfiles, los 3MF y
las instantáneas de configuración para la compatibilidad, y no cambia con las versiones de Tisma.

## Sin publicar (0.10.0)

- **Tisma Cloud: laminar y calcular en servidores** (Configuración > Cuenta de Tisma Cloud): con usuario y contraseña
  de la página web, «Archivo > Exportar > Laminar en la nube y exportar G-code» lamina la placa activa en los
  servidores, y en Ingeniería «Calcular en la nube» corre el análisis, el relleno mínimo y las paredes de resina allí,
  con el mismo resultado que en la PC. Nueva herramienta de línea de comandos `tisma-fea` (el análisis de un objeto de
  un proyecto 3MF) y formato de resultados `.tfea`. El servidor (`server/`) es aparte y no forma parte de Tisma.
- Tisma Cloud: las descargas de G-code y resultados viajan comprimidas (un G-code baja unas 8 veces más rápido) y la
  ventana de la cuenta enlaza a la página donde se crea la cuenta y se paga el plan.
- El laminado por línea de comandos ya no puede seguir con un script de post-procesado sin que alguien lo confirme
  (sin respuesta es «no»).
- **Análisis estructural en la tarjeta gráfica** (Vulkan): el multigrid del análisis corre en la GPU cuando hay una
  (Preferencias > General); sin driver de Vulkan sigue en el procesador, con los mismos resultados.
- **Render con oclusión ambiental y materiales realistas** (Preferencias > Cámara > Calidad de render): dos modos
  nuevos como los de PrusaSlicer 3.0. «Oclusión ambiental» (el predeterminado) oscurece rincones, uniones y el
  contacto de las piezas con la cama; «Materiales realistas» agrega iluminación física, con filamentos «Silk» más
  brillantes y «Matte» más ásperos.
- **Ingeniería con impresoras de resina**: el análisis estructural de la pieza maciza o ahuecada y la búsqueda del
  espesor de pared y el relleno interior más livianos que aguantan las cargas. Nuevo **relleno interior** de las piezas
  ahuecadas (Print Settings > Hollowing): barras a 45° y verticales que se imprimen sin soportes. Materiales de resina
  de las hojas de datos de Elegoo y Formlabs.
- **Impresoras de cinta**: perfiles de la BLACKBELT 3D (y varioDrive, 15° a 45°, de BlackBelt Cura), la Creality CR-30
  (de Creality Print) y la IdeaFormer IR3 V2 (de VertexSlice): 42 impresoras. Nueva opción «La cinta es el eje Z»
  (activada: así son estas tres); la cinta arranca en 0 en la primera capa.
- **Más impresoras de los laminadores de los fabricantes**: 11 modelos nuevos de Creality Print (K3, Z2, K1C y K1 Max
  2025, serie GS, Sonic, Ender-3 V3 Mega) y uno de Elegoo Slicer.
- **Dividir para que entre en la impresora** (clic derecho en el objeto, o el aviso que aparece al cargar un objeto
  más grande que la impresora): corta el objeto en las piezas justas para el volumen de impresión, donde su sección
  es más chica, y pone encastres en cada corte (pernos aparte o espigas), más finos donde las paredes no dan para el
  diámetro elegido. Las piezas y los pernos se reparten en las camas que hagan falta.
- **Corrección: subida a impresoras Bambu Lab** con las bibliotecas del instalador (curl 8.21): la subida FTPS ahora
  verifica el nombre de la impresora, sin lo cual la impresora la rechazaba («session reuse required»).
- **Análisis estructural más rápido (multigrid)**: el solver necesita 15–30 iteraciones en vez de cientos o miles;
  una pieza de 230 000 vóxeles pasa de 25 s a 3 s, con el mismo resultado.
- **Asistente de configuración en una sola página**: se abre directo en la lista de impresoras, con todas las marcas
  juntas (resina y filamento de una misma marca, todos los paquetes de Prusa bajo «Prusa») y el buscador; los
  filamentos y resinas se instalan solos y no se descargan perfiles de Prusa (los de Tisma ya vienen incluidos).
  «Mi impresora no está en la lista» abre las páginas de la impresora personalizada.
- **Corrección: cierre al cambiar de impresora**. Elegir en la ventana principal una impresora convertida de
  OrcaSlicer con «@» en el nombre (por ejemplo la Prusa CORE One de Orca) seleccionaba otra y, si era de resina, el
  programa se cerraba; también hacía que las impresoras agregadas «no se abrieran».
- **Ingeniería: tabla de materiales editable** (Material > «Editar la tabla de materiales…»): valores propios para
  los materiales de Tisma (los de tu filamento o tus probetas) y materiales nuevos, guardados en la carpeta de datos
  y marcados con `*`. Las flechas de las cargas que tiran de una cara ya no quedan escondidas dentro de la pieza.
- **Bambu Lab por LAN**: la subida por FTPS reanuda la sesión TLS como exige el servidor de las impresoras (con
  TLS 1.3 podía fallar con «session reuse required»); la impresora simulada ahora exige lo mismo y el test corre en el
  CI de Linux y de Windows, donde prueba el TLS de Windows (Schannel) y la comprobación de revocación.
- **Laminado más rápido**: la comprobación de perímetros extra ya no calcula dos offsets por isla cuando una cota
  muestra que la capa de arriba no puede necesitarlos; la etapa de perímetros del Benchy baja de 0,58 a 0,39 s con el
  mismo G-code byte a byte (`docs/PERFORMANCE.md`).
- **Marca**: los textos de la interfaz que todavía decían «PrusaSlicer» (preferencias, errores, «no volver a
  preguntar», actualizaciones, ayudas de los ajustes) dicen «Tisma Slicer» en inglés y en los 22 idiomas; también las
  traducciones que ponían «PrusaSlicer» donde el inglés decía Slic3r. Se regeneraron los catálogos `.mo`, así que las
  traducciones al español agregadas en esta versión ya se ven en el programa.
- **Análisis estructural: relleno y paredes como se laminan**: las paredes ahora tienen el espesor del material que
  ponen los perímetros (eran un 17 % más gruesas) y el relleno el material que deposita cada patrón (el panal pone un
  tercio más de lo pedido, el panal 3D un 11 % menos). La masa del análisis coincide con la laminada dentro de ±3 % en
  los patrones regulares (`docs/FEA.md`).
- **Análisis estructural comparado con CalculiX**: barra a tracción, voladizos a lo largo de X y de Z y una escuadra
  en L coinciden con CalculiX dentro del 1,3 % en desplazamientos y tensiones; queda como test automático
  (`docs/FEA.md`).
- **Probetas de resistencia entre capas** (Calibración > «Non-planar: layer strength test pieces»): tres barras
  macizas con capas en onda para romper colgando peso y comparar con las mismas impresas planas; el panel explica cómo
  calcular la tensión de rotura.
- **Cono según la pieza**: el modo no planar Cónico ya no usa un ángulo fijo; en cada radio las capas se inclinan lo
  que piden los voladizos de la pieza (hacia abajo bajo los que miran hacia afuera, hacia arriba bajo los que miran
  hacia adentro), planas en el centro y donde nada lo pide, dentro del ángulo libre del cabezal. En los tests, un hongo
  con ala a 52° pasa de 705 mm de filamento de soporte a ninguno. Idea del laminador radial de Joshua Bird,
  reimplementada sin su código (`docs/NONPLANAR_ANALYSIS.md`).
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

## [0.10.0](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/compare/v0.9.0...v0.10.0) (2026-10-23)


### Features

* **3mf:** open Bambu Studio and OrcaSlicer projects ([f74d824](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/f74d824e8527772af3e96a904d749a1cf9f83f2f))
* **belt:** profiles of BLACKBELT, Creality CR-30 and IdeaFormer IR3 V2, belt on Z ([66a1a39](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/66a1a395a0cc955501cf897e6c0181e8115d9984))
* **calibration:** non-planar layer strength test pieces ([7eaeec4](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/7eaeec4ddcfcd8137fbca7a3960985e385ac5432))
* **calibration:** test pieces of the automatic non-planar mode ([95ed147](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/95ed1476ebde0ad8d6cf169dc6185c00997f3473))
* **calibration:** V groove gauge with several angles and depths ([3ad7958](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/3ad7958dc45635013391cfef38c5fecff68bd0b3))
* **engineering:** resin printers, walls and interior infill of hollowed parts ([b3dcef1](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/b3dcef1dcec254f87ab3f5160cc920e44a8c0977))
* **fea:** editable material table and load arrows outside of the part ([22e775e](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/22e775eb52c3ea7ec80eba913573b69fb9ee07f0))
* **fea:** infill ribs along the stresses ([3f5e860](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/3f5e860aa24251b96f18cbfeb950b153b313d88d))
* **fea:** orthotropic model of aligned infill and ribs in the lightest infill ([22d3e1e](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/22d3e1e70fb5c66cabaacb409be9eaefa3322c36))
* **fea:** skin + core in one click ([59e93bb](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/59e93bbb663f4f0d8fe55a427f19723b05e643d6))
* **fea:** walls and infill of the analysis as they are sliced ([6acbccf](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/6acbccf174375454e670a947214ddfd6d0bf9c4a))
* **fragment:** split an object larger than the printer into pieces with connectors ([8717b01](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/8717b010ee2ae064526db14c6af494330e37042b))
* **gcode:** Z anti-aliasing of sloped top surfaces ([b6f5bc2](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/b6f5bc28d52d7b547bab7d473baa75f8cd8a0426))
* **gcode:** Z anti-aliasing within non-planar layers ([352a6fc](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/352a6fcc35580d7b04550bb69bbe4b60be30e14b))
* **gui:** floating slice and output buttons in compact mode ([01d5592](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/01d55920f9c8271277ee65656a4e632cac23eb50))
* **gui:** new Tisma UI ([#7](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/issues/7)) ([dee3ea9](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/dee3ea985a9fdef07d4126f48642e28790174935))
* **gui:** tell that the Beginner mode hides most settings ([430ab73](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/430ab731af91c069ef1fbd11200308ce69a710bf))
* **gui:** Thingiverse page, one Engineering button and the objects in the sidebar ([4e96778](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/4e967780636ce6d585b330750b715686cfa47fd5))
* **nonplanar:** automatic mode for the finish and fewer supports ([a6ef8bd](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/a6ef8bd1b1db97fef696381f6d9f8910357649f7))
* **nonplanar:** conical layers whose slope at each radius comes from the part ([80f295c](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/80f295c1822d87ab33a1aa242207137c1e927276))
* **nonplanar:** curved layers that follow the stresses of the analysis ([e435bef](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/e435befd2324affb86968036a9bb8668c95b0760))
* **nonplanar:** fit the layers to the free angles of the print head ([61ca8b0](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/61ca8b048c64875c3dd80d85d9a3111511f9cf5f))
* **nonplanar:** interlocking non-planar infill ([d061717](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/d0617170d6bf94cf3d3643a8cd7d0b4b7111c0d2))
* **nonplanar:** only the two print head measurements in the GUI ([a98ed13](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/a98ed13dfd9aaaa5c31fe53801c4adfff0dbfdf1))
* **nonplanar:** tilt the layers only under overhangs that need supports ([22cddff](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/22cddff6a439d7c85f8b0ddefbd8fcc3387f3cc7))
* **nonplanar:** upper layers that follow the top surfaces ([cd02f0c](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/cd02f0ce419df09031f7e8af27cfb507073bb5dc))
* orientation, curved layers, ZAA with non-planar, arc overhangs, Bambu/Orca projects ([c27cc31](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/c27cc31b370984aca7b53d7bd7a0f858ac7dfd86))
* **orientation:** least support (FFF) and lowest peel force (SLA) ([89ce8dd](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/89ce8ddc5207d1f32beea04cd51ab6f521c3474f))
* overhang-only non-planar layers, Z anti-aliasing, Cura printers and V groove gauge ([5de2268](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/5de2268c9a6850c9ca487db2dbffcd7156506f33))
* **perimeters:** arc overhang speed, maximum distance and calibrations ([e1c9395](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/e1c9395c834a758469480dd9ab56a43957acc8f5))
* **profiles:** printers of Creality Print and Elegoo Slicer, belt brands in the wizard ([76d3d01](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/76d3d01864ba0eec08a96b6968c92a7d7741033e))
* **profiles:** printers of UltiMaker Cura that Tisma did not have ([da8bfe0](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/da8bfe0ca9b1e7678b91308b92471d92e1c8e3ad))
* realistic render, GPU structural analysis, tisma-fea and Tisma Cloud client ([#4](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/issues/4)) ([de2da9b](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/de2da9b996e9458f27db105f9f4c3dafb0e51586))
* **sla:** warn about suction cups ([585f0e6](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/585f0e6646b7896700e56bc4c299f32178853cee))
* Tisma 0.10 work, rebrand to Tisma and CI/CD for Windows, Linux and macOS ([f396764](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/f3967649ac9235aa03cdf1a67ab85dba0d693203))
* **usb:** printer monitor in the Device page and print via USB after slicing ([4c8f9be](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/4c8f9bea75ff3ccaf959a417b43f57900e248a18))


### Bug Fixes

* **bambu:** resume the TLS session on the FTPS data connection ([37e7f07](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/37e7f07f82d8bba54ce1350f7b198c2f588fcad2))
* **bambu:** skip the certificate revocation check on LAN connections ([528dcf5](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/528dcf551fc7efc5cb851821799b117395e187f6))
* **bambu:** verify the printer name so FTPS resumes the TLS session ([8f61869](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/8f61869ae5c1865fe4f99b0881d475acfc14c997))
* **build:** finish the Tisma rename in the FHS install ([32aa87d](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/32aa87d969f21e8839346eb685dc233d9e6b4ce7))
* **deps:** download GMP and MPFR from GNU mirrors first ([8145e68](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/8145e68e76cf24e030ff8c9387fe08d7d4b281fd))
* **gui:** keep the settings fields in their static boxes on Windows ([d641de4](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/d641de4dd998f2f045601167a70eff548200c45a))
* **gui:** keep the slicing warnings of the objects ([7620a7b](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/7620a7b9ec4d5596f829c399609bea5d4fc6fa6a))
* **gui:** open project dialog without PrusaSlicer and with descriptions ([572c172](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/572c17276df90f41f9fdddd1809ac1529590f3d7))
* **i18n:** say Tisma Slicer in the visible texts that still said PrusaSlicer ([985f9e0](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/985f9e0f84c82390b8f49689fd16a02f59eafb18))
* **profiles:** name the library filaments as loaded in the converted printers ([8d1d703](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/8d1d7034bdc02e2909148ce149f518e982384c35))
* start TismaSlicer on macOS and finish the user-visible rename ([f9afdd7](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/f9afdd758b120d127fabd9804a3fd532547874ac))
* **wizard:** one printer list, bundled profiles only, no crash on printer change ([bdd380b](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/bdd380b0cd3b8b1dc437e24540f739d845882992))


### Performance

* **fea:** geometric multigrid preconditioner for the voxel solver ([b07824d](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/b07824da34c8b70bb629340caa1c3dd3024ac5c7))
* **perimeters:** skip the extra perimeters offsets when a bound decides ([7bb299e](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/7bb299e4054b011cd4ab407915c0bf39d2c31f06))


### Refactoring

* rebrand PrusaSlicer and NonPlanarSlicer names to Tisma ([472c606](https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/commit/472c606924267a23957acd1e0e812605c036503f))

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
