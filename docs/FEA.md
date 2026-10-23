# Análisis estructural (Fase 5) — método, validación y límites

Código: `src/libtisma_fea/` (biblioteca aparte de `libslic3r`, para aislar el código experimental).
Pruebas: `src/libtisma_fea/test/test_fea.cpp` (suite `libtisma_fea_tests`).

## Qué calcula

Una pieza a una **temperatura de trabajo uniforme** (como dentro de una cámara caliente), sujeta por unas caras
fijas y con cargas en puntos o caras (fuerzas X, Y, Z en N). Devuelve desplazamientos, tensión de von Mises,
factor de seguridad y un **veredicto**:

| Veredicto | Cuándo |
|---|---|
| Aguanta | factor de seguridad ≥ el pedido en toda la pieza |
| Margen bajo | factor de seguridad entre 1 y el pedido en algún punto |
| Fuera de carga | la tensión supera la resistencia del material a esa temperatura en algún punto |
| Demasiado flexible | aguanta, pero una carga o una zona con límite se desplaza más de lo permitido (mm o % de la mayor dimensión de la pieza) |
| Fuera de temperatura | la temperatura supera la temperatura máxima de servicio del material |
| No aplicable | material elástomero (TPU): el análisis lineal no lo describe |

Si no aguanta, propone materiales de la tabla que sí aguantarían a esa temperatura con el factor pedido (y, si hay
límites de deformación, que sean lo bastante rígidos: el desplazamiento se escala con 1/E).

El límite de deformación se mide como el mayor desplazamiento de los nodos donde se aplica cada carga. Buscar el
relleno mínimo que lo cumpla es el objetivo de la Fase 6 (ver la hoja de ruta).

**Zonas con límite de desplazamiento.** Además del factor de seguridad, se pueden marcar caras que no deben
desplazarse más que un valor (mm o % de la mayor dimensión), sean cuales sean las cargas: un encastre, una cara de
sellado, un mecanismo. Donde no hay límite la pieza puede deformarse mucho; ahí solo se comprueba que no rompa. Cada
zona se mide como el mayor desplazamiento de los nodos sobre sus caras y entra en el veredicto, así que el relleno
mínimo, los refuerzos, el lattice y la orientación recomendada también la respetan.

**Colores.** En las vistas de resultados el rojo es llegar al límite (factor de seguridad 1, o el desplazamiento
permitido más chico de las cargas y zonas en la vista de desplazamiento) y el negro es donde el material rompe
(tensión mayor que la resistencia).

No hay conducción térmica: la temperatura solo cambia las propiedades del material (decisión del usuario).

## Uso (GUI)

1. Modo Experto → botón **Ingeniería** de la columna: se abre la vista 3D con el panel del objeto seleccionado.
2. Material (por defecto el del filamento del objeto), temperatura de trabajo y factor de seguridad.
   «Editar la tabla de materiales…» abre la tabla: se pueden cambiar los valores de un material de Tisma por los de
   tu filamento (hoja de datos o probetas propias) o agregar materiales. Lo cambiado se guarda en
   `<carpeta de datos>/fea_materials.json` (solo las filas distintas de las de Tisma; «Restablecer / quitar» vuelve
   atrás) y se marca con `*` en la lista. Se validan los valores (módulos y resistencias positivos, Poisson entre 0 y
   0,5, temperatura de servicio mayor que 23 °C). Un análisis en curso usa una copia del material, así que editar la
   tabla no lo afecta.
3. «Cara fija»: clic en las caras donde se sujeta la pieza (en piezas STEP se toma la cara CAD completa; en
   mallas, la región plana alrededor del triángulo). Otro clic en la misma cara la quita.
4. «Carga en una cara» o «Carga en un punto»: fuerza X, Y, Z en N (Z hacia arriba, como en la cama), límite de
   deformación opcional y, para cargas puntuales, el radio en el que se reparte (3 mm por defecto: una carga real
   actúa sobre un área; con radios muy pequeños la tensión local crece sin límite).
   Cada carga se dibuja con una flecha en el centro de sus caras (o en el punto), siempre por fuera de la pieza:
   una carga que empuja termina en la superficie y una que tira sale de ella.
5. «Límite de desplazamiento»: clic en las caras que no deben moverse más que el valor indicado (mm y/o %; con los
   dos vale el menor). Otro clic en la misma cara lo quita. Después del análisis cada zona muestra cuánto se
   desplazó, en rojo si superó su límite.
6. «Analizar»: el análisis corre en segundo plano (cancelable) y muestra el veredicto, el factor de seguridad, el
   desplazamiento y el mapa de colores sobre la pieza.

7. «Pieza impresa» (activada por defecto): el análisis usa las paredes, techos, suelos y el relleno del perfil.
8. **Relleno mínimo** (también desde el botón Estructuras): busca el relleno uniforme más bajo y, si ahorra
   material, el relleno por zonas; muestra la masa frente al relleno actual y los aplica al objeto con un clic
   (densidad del objeto + modificadores «Tisma infill zone»). La vista «Relleno» oculta la capa maciza para ver el
   interior.
9. **Piel + núcleo** (Optimizar, un clic): la pieza terminada como inyectada y diseñada para sus cargas.
   - Piel: pone «No planar» de la configuración de impresión en Automático (caras superiores que siguen la superficie
     y menos soportes, dentro de lo que permite el cabezal).
   - Núcleo: si la pieza tiene caras fijas y cargas, busca el relleno mínimo por zonas con nervios según las
     tensiones, ya con las capas curvas de la piel (el análisis lee la configuración de impresión), y lo aplica al
     terminar. Sin cargas aplica solo la piel y avisa que faltan.
   - Es un ajuste de la impresión (afecta a todos los objetos de la placa) más modificadores del objeto; se deshace
     con Ctrl+Z.

Todo se guarda en el proyecto (3MF) y se puede deshacer.

## Método

- **Unidades**: mm, N, MPa, °C. El eje Z es la dirección de impresión.
- **Voxelizado**: se corta la malla en el centro de cada capa de vóxeles (`slice_mesh_ex`); un vóxel es sólido
  si su centro está dentro del corte. Tamaño automático para unos 60 000 vóxeles.
- **Elementos**: cada vóxel es un hexaedro trilineal de 8 nodos con integración 2×2×2.
- **Material**: transversalmente isótropo con eje Z (las capas son más débiles y blandas entre sí que a lo largo):
  E_xy, E_z, ν, G_xy = E_xy / 2(1+ν), G_z = E_z / 2(1+ν). El acoplamiento plano–eje usa ν / √(E_xy E_z), que
  mantiene la matriz simétrica y definida positiva también con fibra de carbono.
- **Temperatura**: rigidez y resistencia × f(T): 1 hasta 23 °C, baja linealmente a 0,5 en la temperatura máxima de
  servicio y cae a 0,02 en los 15 °C siguientes. Es un modelo simplificado.
- **Solver**: gradiente conjugado sin ensamblar la matriz (todos los vóxeles comparten la misma matriz de elemento);
  el producto se hace en paralelo (TBB) en 8 grupos de vóxeles sin nodos comunes. Precondicionador: multigrid
  geométrico (`Multigrid.cpp`, ver abajo); en piezas chicas (≤ 15 000 grados de libertad) o si el multigrid no ayuda,
  Jacobi.

### Multigrid (paso 17)

Con Jacobi, las iteraciones crecen con la cantidad de vóxeles a lo ancho de la pieza (de 600 a 2 900 en las pruebas)
y el análisis fino tardaba decenas de segundos. El multigrid las deja en 15–30 sea cual sea la resolución:

- Nivel 0: el análisis mismo (su producto y su diagonal), así que el gradiente conjugado converge a la misma solución.
- Cada nivel grueso junta 2 × 2 × 2 elementos. Su matriz de elemento es el producto de Galerkin Pᵀ K P de los
  elementos que junta (P: interpolación trilineal de los nodos gruesos), exacto para las paredes, el relleno, el
  relleno alineado y los huecos dentro del elemento grueso. Se guarda en precisión simple (2,3 kB por elemento
  grueso, un octavo de los vóxeles).
- Suavizado: 2 pasadas de Jacobi antes y 2 después (ciclo V simétrico). El amortiguamiento de cada nivel es
  4 / (3 λ_max), con λ_max de D⁻¹A estimado por el método de la potencia: con un valor fijo (0,6) el suavizado
  divergía en piezas con paredes y relleno, y el precondicionador dejaba de ser definido positivo (el gradiente
  conjugado se estancaba).
- El nivel más grueso (≤ 15 000 grados de libertad) se resuelve directo (Cholesky disperso de Eigen).
- Red de seguridad: si con el multigrid el gradiente conjugado pasa de 300 iteraciones o pierde la positividad,
  sigue con Jacobi desde la solución que tiene.

| Caso | Vóxeles | Jacobi | Multigrid |
|---|---|---|---|
| Voladizo macizo | 48 000 | 605 it., 1,8 s | 14 it., 0,45 s |
| Voladizo impreso (paredes y relleno) | 64 000 | 2 275 it., 8,6 s | 20 it., 0,81 s |
| Barra traccionada a 30° (proyecto de prueba) | 63 000 | 1 127 it., 4,4 s | 21 it., 0,83 s |
| La misma barra, vóxel de 0,25 mm | 230 000 | 1 925 it., 25,2 s | 25 it., 3,1 s |

Mismo desplazamiento y tensiones (test «Multigrid preconditioner»).

### GPU (paso 17)

El ciclo V del multigrid corre en la tarjeta gráfica con Vulkan (`Gpu.cpp`, `Multigrid.cpp`, shaders en
`src/libtisma_fea/src/gpu`):

- Vulkan se carga al ejecutar (volk): Tisma funciona sin driver de Vulkan, y entonces todo corre en la CPU como antes.
  Se elige una GPU dedicada, si no una integrada; los dispositivos que emulan la GPU en la CPU (lavapipe,
  SwiftShader) no se usan porque son más lentos que la CPU.
- Todo el ciclo en precisión simple: barridos de Jacobi y residuo (`fea_apply.comp`, un hilo por nodo que junta las
  filas de los elementos que lo rodean, sin operaciones atómicas), restricción (`fea_restrict.comp`) y prolongación
  (`fea_prolong.comp`). La matriz de un elemento del nivel 0 es una combinación de la tabla de matrices de las
  orientaciones de capa y del relleno alineado; la de los niveles gruesos, su producto de Galerkin.
- El nivel más grueso (≤ 15 000 grados de libertad) se sigue resolviendo en la CPU (Cholesky): se lee su lado
  derecho, se resuelve y se sube la solución, entre dos tandas de comandos grabadas una sola vez.
- El gradiente conjugado sigue en la CPU en doble precisión, con el beta flexible (Polak-Ribière) porque el
  precondicionador en precisión simple no es exactamente el mismo operador en cada iteración. La solución es la
  misma que en la CPU (test «Multigrid on the GPU»: mismas iteraciones, desplazamiento dentro de 10⁻⁵).
- Si un búfer no entra en el máximo de la placa (`maxStorageBufferRange`) o algo falla, el análisis sigue en la CPU.
- Preferencias > General > «Análisis estructural en la tarjeta gráfica» (activado). El panel de Ingeniería indica la
  placa usada. Variables de entorno: `TISMA_FEA_GPU=0` la desactiva; `TISMA_FEA_GPU_ALLOW_CPU=1` permite los
  dispositivos emulados (lo usan las pruebas).
- macOS: hace falta MoltenVK (Vulkan sobre Metal), que todavía no se incluye en el paquete: ahí corre en la CPU.

Pendiente: medir la aceleración en placas reales (el entorno de desarrollo solo tiene lavapipe, que emula la GPU en
la CPU y es unas 2–3 veces más lento que el multigrid en la CPU); llevar también el producto del gradiente conjugado
a la GPU si el traspaso del vector por iteración pesa; la aerodinámica (LBM).
- **Condiciones**: los nodos a menos de 0,9 vóxeles de las caras fijas se bloquean; la fuerza de una carga sobre
  caras se reparte entre los nodos cercanos a ellas; la de una carga puntual entre los nodos a menos de 1,5 vóxeles
  (o el radio indicado) del punto.
- **Criterio de rotura** (simplificado): el mayor de von Mises / resistencia en XY, tracción en Z / adhesión entre
  capas y cortante entre capas / (0,6 × adhesión), todo a la temperatura de trabajo.

## Paredes y relleno (Fase 6)

Con `InfillModel` activado la pieza deja de ser maciza:

- **Capa maciza**: la parte de cada vóxel a menos del espesor de pared (perímetros) o de techo/suelo (capas
  sólidas) de la superficie es material macizo; la fracción se calcula con la distancia del centro del vóxel a la
  superficie y la normal de la cara más cercana. El espesor de pared es el material que ponen los perímetros: el
  espaciado del perímetro externo más el de los demás, con espaciado = ancho − altura de capa·(1 − π/4) (el cordón
  redondeado, `Flow::rounded_rectangle_extrusion_spacing` de PrusaSlicer), no perímetros × ancho (que daba un 17 % de
  más).
- **Relleno homogeneizado**: rigidez E·ρ^nE y resistencia σ·ρ^nS con exponentes por patrón (rectilíneo/rejilla 1,2;
  panal 1,4; cúbico 1,5/1,4; giroide 1,6/1,5; lightning 2,5, no estructural). Son aproximados: hay que calibrarlos
  con probetas impresas. ρ es el material que deposita el patrón, no la densidad nominal: ρ = mín(1, k·densidad), con
  k medido en el laminado de Tisma (`infill_deposit_factor`, abajo). El optimizador sigue trabajando con la densidad
  nominal, que es la que se aplica.
- **Zonas**: mallas con su propia densidad (los modificadores de relleno), voxelizadas sobre la misma rejilla.
- **Masa**: suma de la fracción de material de cada vóxel × densidad del material.

## Del objeto al análisis y vuelta (Fase 6)

- `build_analysis_input(..., print_config)` toma del perfil (con los ajustes propios del objeto) los perímetros y
  el ancho de extrusión (espesor de pared), las capas sólidas superiores e inferiores, la densidad y el patrón de
  relleno, y los modificadores del objeto con densidad de relleno como zonas.
- `apply_infill` escribe el resultado: la densidad de relleno del objeto y un modificador de relleno por zona
  («Tisma infill zone NN%»), reemplazando los de una optimización anterior; las regiones de apoyos y cargas
  siguen a sus piezas.

## Relleno mínimo (Fase 6)

`optimize_infill` busca el relleno más ligero que cumple el factor de seguridad, los límites de deformación y la
temperatura:

1. **Uniforme**: bisección de la densidad (los requisitos mejoran al aumentar el relleno) y redondeo a un
   porcentaje entero. Si ni el 100 % basta, lo dice (hacen falta más paredes, otro material u otra forma).
2. **Por zonas**: con las tensiones del relleno uniforme, cada punto del interior recibe la densidad que lleva su
   tensión a la admisible (ρ·(utilización)^(1/nS)); el interior se divide en celdas (3 vóxeles, mínimo 3 mm) y se
   agrupan en 3 niveles. Los niveles se escalan juntos por bisección hasta cumplir los requisitos. Las zonas son
   las mallas de las celdas de cada nivel, tal como se aplicarán (modificadores), de modo que el último análisis es
   el de lo que se imprimirá. Solo se propone si es más ligero que el uniforme.

### Nervios: el relleno en la dirección de las tensiones

`rib_zones` (`Ribs.hpp`) busca dónde la pieza trabaja en una sola dirección dentro de las capas y `apply_ribs` pone ahí
el relleno en líneas paralelas a esa dirección en todas las capas (relleno rectilíneo alineado al ángulo de la zona),
como los nervios de una pieza diseñada para las cargas. Se aplica junto con el relleno mínimo si está marcada la
casilla «Infill ribs along the stresses» (activada por defecto).

- En cada vóxel del relleno, la tensión en el plano de la capa (σxx, σyy, τxy) da dos tensiones principales; la de
  mayor valor absoluto da la dirección θ, y a = (|σ1| − |σ2|) / (|σ1| + |σ2|) cuánto es de una sola dirección.
- Cada celda (como las zonas: 3 vóxeles, mínimo 3 mm) promedia a·(cos 2θ, sin 2θ) pesado por el índice de fallo.
  El ángulo doble hace que tracción y compresión en la misma línea sumen (en flexión, arriba y abajo de la fibra
  neutra), cosa que no pasaría sumando los tensores.
- Se quedan las celdas con al menos el 20 % del índice de fallo máximo y una direccionalidad media de 0,5 o más; se
  agrupan por ángulo en pasos de 15° (como mucho 7,5° de desvío). Cada grupo es un modificador «Infill ribs N°» con
  `fill_pattern = alignedrectilinear` y `fill_angle = N`. La densidad sigue siendo la de la pieza y la de las zonas
  (los modificadores de zonas se añaden después y solo cambian la densidad).
- El ángulo del relleno se mide sobre la cama (las capas se laminan con la rotación de la instancia), igual que las
  direcciones del análisis: no hace falta convertirlo si la pieza está girada.

La idea de imprimir siguiendo las tensiones principales es la de Tam y Mueller, «Additive Manufacturing Along
Principal Stress Lines» (3D Printing and Additive Manufacturing, 2017); aquí se reduce al ángulo del relleno por
zonas.

### Relleno alineado en el análisis (ortótropo)

El relleno rectilíneo alineado (los nervios, o el patrón `alignedrectilinear` del perfil o de un modificador) no es
igual en todas las direcciones: sus líneas son continuas a lo largo (L) y se apilan en paredes a lo largo de la normal
de la capa (N), pero a lo ancho (T) solo las unen los perímetros y las conexiones de las líneas. El análisis le da su
propia matriz, en el marco (L, T, N) de cada vóxel, con factores de las propiedades del material macizo
(`aligned_infill_factors`):

| Propiedad | Factor | Fuente |
|---|---|---|
| E_L, E_N, G_LN y sus resistencias | ρ | regla de las mezclas (cota de Voigt): líneas y paredes continuas |
| E_T, G_LT, G_TN | ρ² | sólidos celulares dominados por flexión (Gibson y Ashby, *Cellular Solids*, 2.ª ed., 1997, cap. 5) |
| Resistencias a lo ancho | 0,3·ρ^1,5 | ídem |

A lo ancho nunca más que el relleno isótropo de la misma densidad. La fórmula de Gibson y Ashby es para densidades
bajas (hasta un 30 % aproximadamente); por encima subestima la resistencia a lo ancho (lado seguro). La rotura del
relleno alineado es por tensión máxima en su marco: |σL|, |σT|, σN de tracción, los cortes τLN y τTN (contra
0,6 de la adherencia entre capas) y τLT (con la relación de von Mises). La parte de cáscara del vóxel sigue con el
material macizo y su criterio. Las matrices se agrupan por orientación de capa, ángulo, densidad y factores.

Consecuencias:

- A lo largo de las líneas el relleno es más rígido (ρ en vez de ρ^1,2), así que toma más carga y la pieza se
  deforma menos. En un elemento las líneas rompen a la misma deformación que el material, de modo que la ganancia de
  resistencia viene por esa rigidez. A lo ancho es mucho más débil.
- El relleno mínimo, con «Infill ribs along the stresses», busca primero el uniforme sin nervios, saca los nervios de
  ese análisis y busca de nuevo con ellos; solo los usa si el relleno queda más liviano, y las zonas se calculan con
  ellos. En una probeta de tracción con mordazas macizas (test «The lightest infill counts the ribs») el relleno
  mínimo baja del 31 % al 27 %, y con las líneas a lo largo la barra es un 10 % más rígida y el relleno trabaja un
  10 % menos cargado; con las líneas a lo ancho, 1,96 veces más cargado.
- `build_analysis_input` lee el patrón y el ángulo del perfil y de los modificadores: un modificador sin
  `fill_density` deja la densidad de lo que tiene debajo, como en el laminado.

**Límites**: los factores son aproximados y hay que calibrarlos con probetas impresas (a lo largo, a lo ancho y a
45°). El modelo isótropo de los demás patrones (ρ^1,2 para una rejilla) es más generoso que el reparto real de las
líneas en dos direcciones, así que la ganancia de los nervios frente a la rejilla que muestra el análisis es menor que
la que se espera en la pieza. No hay pandeo de las líneas a compresión.

## Refuerzos locales (Fase 6)

`reinforcement_zone`: las celdas (2 mm) a menos de un radio (por defecto 2 × el radio de la carga, mínimo 5 mm) de
las caras fijas y de las cargas forman una zona con más perímetros (+2: el análisis la ve como una pared más gruesa
dentro de la zona) y más relleno (40 %, nunca menos que el de la pieza). `optimize_reinforcement` busca el relleno
uniforme más ligero del resto de la pieza con la zona puesta y lo compara con el mejor relleno sin ella.

## Lattice 3D (Fase 6)

- **Geometría imprimible sin soportes**: celdas cúbicas con barras verticales y cruces a 45° en los planos XZ e YZ;
  ninguna barra horizontal (los techos de las celdas los resuelven las capas sólidas y los puentes del laminador).
- **Barras**: prismas cerrados (6 lados) que se solapan en los nodos; se conservan las que tocan la pieza, así que
  llegan hasta las paredes. Se aplican como un modificador macizo con el relleno de la pieza al 0 %: el laminador
  imprime las barras unidas a las paredes, sin operaciones booleanas.
- **Análisis**: cada celda es un material homogeneizado con la densidad relativa de sus barras,
  ρ = π(1 + 4√2)·(r/a)² (longitud de barra por celda (1 + 4√2)·a, sin descontar los solapes), con exponente de
  Gibson–Ashby 1,5 para rigidez y resistencia (aproximado, a calibrar con probetas). Los vóxeles de ~1 mm no
  resuelven las barras: el primer intento con la fracción de volumen de cada vóxel daba tensiones irreales en los
  vóxeles casi vacíos junto a las barras y se descartó.
- **Búsqueda**: diámetro uniforme mínimo por bisección (entre el mínimo imprimible, 0,9 mm, y 3 mm); luego un
  diámetro por celda según su tensión (sección ∝ utilización) escalado en conjunto hasta cumplir los requisitos.

## Resolución, dirección de impresión y adherencia entre capas

**Resolución** (panel de Ingeniería, se guarda como `tisma_fea_quality`):

| Opción | Vóxel |
|---|---|
| Rápida | ~20 000 vóxeles en la pieza |
| Normal | ~60 000 vóxeles (la anterior) |
| Alta | 2 anchos de línea del perímetro |
| Ultra | 1 ancho de línea del perímetro |

El ancho de línea es el de los perímetros del perfil (con los ajustes del objeto; un porcentaje es de la altura de
capa, como en el motor de PrusaSlicer, `Flow::extrusion_width`). No hay límite de vóxeles: se calcula con la resolución elegida. Como referencia, 1,2 millones de vóxeles tardan
86 s en 4 núcleos (barra de 100×50×50 mm) y el relleno mínimo, el refuerzo, el lattice y la orientación hacen muchos
análisis (~9 s cada uno con 250 000). Por encima de esas cantidades el panel avisa que el análisis es grande.
`tisma-fea --max-voxels N` limita la cantidad (por ejemplo en los servidores) agrandando el vóxel.

**Dirección de impresión**: el material es transversalmente isótropo con el eje débil en la dirección de
apilado de las capas (`Setup::build_direction`, Z por defecto). La matriz constitutiva se rota con la matriz de Bond
(`D' = M D Mᵀ`) y las tensiones se evalúan en el sistema de las capas.

**Adherencia entre capas**: la resistencia a través de las capas se multiplica por
`layer_adhesion_factor(material, temperatura de boquilla)`: 1 en la mitad del rango de impresión del material,
1,15 en el máximo, 0,8 en el mínimo y hasta 0,5 a 20 °C por debajo del mínimo. La temperatura es la del perfil de
filamento del extrusor del objeto. Es un modelo aproximado: validar con probetas impresas.

**Orientación recomendada**: «Recomendar orientación» analiza la pieza con las capas a lo largo de Z (como está),
X e Y, con las mismas cargas, y recomienda la de mayor factor de seguridad si mejora más de un 5 %. «Rotar la
pieza» gira la instancia y las fuerzas de las cargas (las caras y puntos siguen a la pieza). No se comprueban
voladizos ni soportes de la nueva orientación.

**Aplicar el relleno mínimo**: el relleno se aplica como ajuste del objeto (`fill_density`) y modificadores
«Tisma infill zone» por zona; el panel y una notificación indican lo aplicado. Hay que laminar de nuevo.

## Capas curvas (no planares, Fase 9b)

Con capas no planares (`Print Settings > Non-planar`) el material ya no tiene una sola dirección débil: en cada
vóxel el eje «a través de las capas» es la normal de la capa curva que pasa por él, calculada con la misma
deformación que usa el laminado (`NonPlanar::Deformation::layer_normal`, el gradiente de la coordenada de laminado
z − D). Para no guardar una matriz por vóxel, las normales se agrupan en direcciones separadas unos 2°
(`LAYER_NORMAL_STEP_DEG`), cada una con su matriz de rigidez; el criterio de falla usa la orientación de cada vóxel.
El panel de Ingeniería indica cuántas direcciones se usaron.

No se modela el encastre mecánico entre capas onduladas (la grieta que tiene que seguir un camino más largo): la
mejora calculada es la de girar el plano débil, y la real puede ser mayor. Prueba: una barra tirada hacia arriba con
capas inclinadas ±25° pasa de un factor de seguridad de 4,7 a 5,2 (PLA).

## Resina (paredes y relleno interior)

Con una impresora de resina, Ingeniería muestra solo el análisis estructural (sin Aerodinámica) y una sola
optimización, **Paredes y relleno interior**. El resto (relleno por zonas, nervios, refuerzos, lattice, orientación,
capas curvas) es de FDM y no aparece.

- **La pieza de resina**: maciza, o ahuecada (`Print Settings > Hollowing`: `hollowing_enable`) con paredes de
  `hollowing_min_thickness` y, nuevo en Tisma, un **relleno interior** (`hollowing_infill_density`, 0–60 %, celda
  `hollowing_infill_cell`, 8 mm). El relleno son barras de sección cuadrada en cinco direcciones (vertical y a 45° en
  XZ e YZ), así que cada barra se apoya en las capas de abajo y la cavidad no necesita soportes. Densidad =
  5 · (ancho / celda)²; las barras de menos de 0,8 mm se avisan porque no se imprimen bien
  (`src/libslic3r/SLA/InteriorInfill.cpp`). Se agregan restando las barras de la malla de la cavidad (con OpenVDB,
  la misma biblioteca del ahuecado de PrusaSlicer), porque el ahuecado quita todo lo que queda dentro de la cavidad.
- **Análisis**: paredes del espesor del perfil y el relleno homogeneizado como un lattice (exponente 1,5, como el
  lattice 3D de FDM). La resina curada es casi isótropa: las propiedades son las mismas a través de las capas y no hay
  factor de adherencia (no hay boquilla).
- **Materiales de resina** (`Materials.cpp`), de las hojas de datos de los fabricantes, postcurados: resina estándar
  Elegoo (33,8 MPa; la hoja solo da el módulo a flexión, 709 MPa), resina estándar Formlabs (FLGPCL03: 65 MPa,
  2,8 GPa, HDT 58,4 °C a 1,82 MPa) y Tough 2000 de Formlabs (40,4 MPa, 1,8 GPa, HDT 57 °C a 1,8 MPa y 70 °C a
  0,45 MPa). Las hojas no dan densidad (se usa 1,15 g/cm³, típica de fotopolímeros curados) ni coeficiente de Poisson
  (0,35). El material se elige por el nombre del perfil de resina: las «Tough» y las «ABS-like» usan la Tough; el
  resto, la estándar (no por `material_type`, que es «Tough» por defecto en PrusaSlicer y en la mayoría de los perfiles
  de resinas estándar). Se puede elegir otro en la tabla.
- **Paredes y relleno interior** (`Fea::optimize_resin`): analiza la pieza maciza y, para paredes de 1, 1,5, 2, 3, 4 y
  5 mm, la cavidad vacía y, si no alcanza, el relleno más bajo que cumple el factor de seguridad y los límites de
  desplazamiento (entre las barras más finas que se imprimen en la celda del perfil, mínimo 5 %, y 60 %). Muestra la
  tabla y la opción más liviana; «Aplicar» pone el ahuecado, el espesor y el relleno en el objeto (se ven en la lista
  de objetos y se deshacen con Ctrl+Z). Si ninguna ahuecada aguanta, propone imprimirla maciza.
- Una pieza ahuecada necesita agujeros de drenaje (herramienta «Hollow and drill») para que salga la resina; el
  detector de ventosas avisa si la cavidad queda cerrada hacia el film.
- En piezas finas la cavidad real puede ser más chica que la del análisis: el ahuecado de PrusaSlicer cierra los
  huecos angostos (`hollowing_closing_distance`, 2 mm por defecto). Queda más resina que la calculada, así que el
  resultado está del lado seguro (prueba en la GUI: una barra de 6 mm con paredes de 1 mm y 51 % lamina 3,60 ml
  frente a 3,74 ml maciza y los 2,9 ml del análisis).

Pruebas: `[Resin]` en `libtisma_fea_tests` (materiales, pieza ahuecada con y sin relleno, la búsqueda de paredes y
relleno de una viga en voladizo, y que los ajustes de resina llegan al análisis) y `[InteriorInfill]` en
`sla_print_tests` (barras autoportantes, densidad del relleno y una impresión SLA laminada con relleno interior).

## Validación (pruebas automáticas)

| Caso | Resultado | Referencia |
|---|---|---|
| Barra 50 × 10 × 10 mm a tracción, 1000 N, PLA | 0,1633 mm; 10,000 MPa | F L / (E A) = 0,1667 mm (−2 %, el empotramiento impide la contracción lateral); 10 MPa |
| Voladizo 100 × 10 × 10 mm, 10 N en el extremo, vóxel 2,5 mm | 1,257 mm | Euler–Bernoulli + cortante = 1,344 mm (−6,5 %) |
| El mismo con vóxel 1,25 mm | 1,306 mm | −2,8 %: converge al refinar (el hexaedro lineal es algo rígido a flexión) |
| Barra de PETG a 23 y 50 °C | razón de desplazamientos = f(23)/f(50) | modelo de temperatura |
| Barra de PA-CF a lo largo de X y de Z | razón = E_xy / E_z (±5 %) | ortotropía |
| PLA a 130 °C | «fuera de temperatura», propone PEI entre otros | tabla |
| 60 MPa en PLA; 35 MPa entre capas | «fuera de carga» | resistencias de la tabla |

| Barra 80 × 10 × 10 con paredes de 0,9 mm y relleno del 20 / 60 % | masa 4,53 / 7,22 g (maciza 9,92 g, exacto); menos relleno, más flexible y menos resistente | coherencia del modelo |
| Límite de deformación fijado con el desplazamiento al 40 % | el optimizador encuentra el 40 % (10 análisis); el 38 % no cumple | bisección |
| Voladizo cargado a resistencia | uniforme 17 % (4,33 g) → por zonas: base 7 % + 2 zonas junto al empotramiento (3,96 g, −8,5 %) | zonas donde el momento es máximo |
| Objeto con el perfil (2 perímetros, giroide), 55 N, FS 1,5 | uniforme 48 % (6,51 g) → por zonas 5,51 g (−15 %); aplicado al objeto como densidad + modificadores y analizado de nuevo: misma masa (±2 %) y cumple | ida y vuelta modelo ↔ análisis |

| Barra con carga puntual, refuerzo de 6 mm (+2 perímetros, 40 %) | FS 2,63 → 2,90 con el mismo relleno; malla cerrada alrededor del empotramiento y de la carga | refuerzo |
| Celda de lattice | 12 barras cerradas, ninguna por debajo de 45° | imprimibilidad |
| Cubo de 30 mm comprimido (6000 N, FS 2) | barras de 2,08 mm, 16,8 g frente a 33,5 g macizo; las barras variables no aportan (tensión uniforme) | lattice uniforme |
| Voladizo 90 × 20 × 20 mm, 200 N, FS 2 | uniforme 2,25 mm (25,4 g) → variable 1,0–2,5 mm (≈ 17 g, −30 %), más gruesas junto al empotramiento | lattice variable |

Rendimiento medido: 62 370 vóxeles, 704 iteraciones, 2,5 s en 4 núcleos.

### Relleno frente a las trayectorias laminadas

Un bloque de 30 × 30 × 20 mm laminado (capas de 0,3 mm, líneas de 0,45 mm) y el mismo bloque como lo ve el análisis
(test «Infill model matches the sliced toolpaths»). Del laminado se mide el volumen de los perímetros (el espesor de
pared equivalente del anillo cuadrado), el del relleno disperso frente al volumen de las superficies de relleno
disperso, y el volumen total extruido.

Material del relleno disperso por densidad nominal, k (media de 15 % y 30 %):

| Patrón | k | Patrón | k |
|---|---|---|---|
| rectilíneo, alineado, monotónico | 1,10 | líneas monotónicas | 1,00 |
| rejilla | 1,07 | triángulos | 1,10 |
| estrellas | 1,05 | cúbico | 1,06 |
| línea | 1,06 | concéntrico | 1,11 |
| panal | 1,33 | panal 3D | 0,885 |
| giroide | 1,04 | Hilbert, cuerdas, espiral octagrama | 1,11–1,13 |

Las líneas se solapan en los cruces y anclajes; el panal deposita un tercio más de lo pedido. Cúbico adaptativo,
cúbico de soporte y lightning dependen de la forma (más denso solo bajo los techos): en el bloque depositan 0,75,
0,6–0,78 y 0,2 de lo pedido; el análisis usa k = 1 y su masa sale alta (+9 a +90 %).

Antes de la corrección el total coincidía por compensación (paredes 17 % de más, relleno de menos), salvo el panal
(−11 %). Después:

| Caso | Pared laminada / modelo | Relleno laminado / modelo | Volumen laminado / modelo |
|---|---|---|---|
| Rectilíneo 20 %, 2 perímetros | 0,778 / 0,771 mm | 0,221 / 0,220 | 6546 / 6490 mm³ |
| Giroide 30 %, 3 perímetros | 1,166 / 1,157 mm | 0,303 / 0,312 | 8285 / 8387 mm³ |
| Panal 20 %, 2 perímetros | 0,778 / 0,771 mm | 0,268 / 0,266 | 7235 / 7168 mm³ |
| Panal 3D 20 %, 2 perímetros | 0,778 / 0,771 mm | 0,178 / 0,177 | 5915 / 5855 mm³ |
| Cúbico 25 %, 4 perímetros | 1,555 / 1,542 mm | 0,266 / 0,265 | 8325 / 8270 mm³ |

En los 19 patrones regulares a 15 y 30 % el volumen total queda dentro de ±3,2 %.

### Comparación con CalculiX

Las mismas piezas, apoyos, cargas y material (PLA de la tabla, transversalmente isótropo: E_xy 3000, E_z 2600 MPa,
ν 0,35, con ν13 = ν·√(E_xy/E_z) como en `constitutive()`) analizadas con CalculiX 2.21 (`ccx`), con hexaedros
cuadráticos de 20 nodos (C3D20R) sobre los mismos cubos de 1 mm, la cara fija empotrada y la fuerza como tracción
uniforme (cargas nodales consistentes). Se comparan los valores en los centros de los vóxeles de 1 mm (media de los 8
nodos de cada cubo, como los da el análisis de Tisma). Test «Agrees with CalculiX» (tolerancia 3 %); los números de
CalculiX los escribe `src/libtisma_fea/test/calculix/run_calculix.py` en `calculix_reference.hpp`, así el test corre
sin CalculiX instalado.

| Caso | Tisma | CalculiX | Diferencia |
|---|---|---|---|
| Barra 60 × 10 × 10 mm, 1000 N de tracción: desplazamiento a 59,5 mm; σxx a mitad | 0,1966 mm; 10,00 MPa | 0,1969 mm; 10,00 MPa | −0,15 %; 0,0 % |
| Voladizo horizontal 60 × 10 × 10 mm, 100 N en el extremo: flecha; σxx en la fibra superior a mitad | −2,849 mm; 15,83 MPa | −2,871 mm; 15,93 MPa | −0,8 %; −0,6 % |
| Voladizo vertical 10 × 10 × 60 mm (a lo largo de Z, E_z), 50 N: flecha; σzz en la fibra a mitad | 1,639 mm; 7,91 MPa | 1,652 mm; 7,96 MPa | −0,8 %; −0,7 % |
| Escuadra en L (base 40 × 10 × 10, montante 10 × 10 × 30), 50 N arriba: desplazamiento; σxx en la base | 1,089 mm; 9,88 MPa | 1,102 mm; 9,94 MPa | −1,2 %; −0,6 % |

CalculiX coincide con la teoría de vigas (σ = 10 MPa = F/A; flecha F L³/(3 E I) = 2,88 mm; σ = M c / I = 15,93 MPa),
y el análisis de Tisma queda 1 % más rígido, lo esperable del hexaedro lineal frente al cuadrático. No se comparan
tensiones pico en esquinas entrantes (singulares en los dos modelos) ni el relleno homogeneizado (CalculiX ve la
pieza maciza).

## Límites que hay que conocer

- **Relleno homogeneizado**: el relleno se trata como un material continuo más blando; no se modelan las
  trayectorias reales ni su orientación. La validación con las trayectorias laminadas queda pendiente.
- **Valores de material aproximados**: valores típicos de fichas técnicas y bibliografía para piezas impresas; hay
  que verificarlos con la ficha del filamento usado. La impresión (temperatura, ventilación, orientación) los cambia
  mucho, sobre todo la adhesión entre capas.
- **Concentraciones**: en las cargas puntuales y en los bordes de los empotramientos la tensión depende del tamaño
  del vóxel (singularidades). El punto crítico se informa para revisarlo.
- **Lineal**: pequeñas deformaciones y material elástico; no hay pandeo, fluencia (creep) ni fatiga. La fluencia
  importa mucho a temperatura alta y con cargas permanentes.
- **Bordes escalonados**: los vóxeles aproximan las superficies curvas; el error baja al reducir el vóxel.
