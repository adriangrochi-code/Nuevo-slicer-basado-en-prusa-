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
| Demasiado flexible | aguanta, pero una carga se desplaza más que su límite de deformación (mm o % de la mayor dimensión de la pieza) |
| Fuera de temperatura | la temperatura supera la temperatura máxima de servicio del material |
| No aplicable | material elástomero (TPU): el análisis lineal no lo describe |

Si no aguanta, propone materiales de la tabla que sí aguantarían a esa temperatura con el factor pedido (y, si hay
límites de deformación, que sean lo bastante rígidos: el desplazamiento se escala con 1/E).

El límite de deformación se mide como el mayor desplazamiento de los nodos donde se aplica cada carga. Buscar el
relleno mínimo que lo cumpla es el objetivo de la Fase 6 (ver la hoja de ruta).

No hay conducción térmica: la temperatura solo cambia las propiedades del material (decisión del usuario).

## Uso (GUI)

1. Modo Experto → botón **Ingeniería** de la columna: se abre la vista 3D con el panel del objeto seleccionado.
2. Material (por defecto el del filamento del objeto), temperatura de trabajo y factor de seguridad.
3. «Cara fija»: clic en las caras donde se sujeta la pieza (en piezas STEP se toma la cara CAD completa; en
   mallas, la región plana alrededor del triángulo). Otro clic en la misma cara la quita.
4. «Carga en una cara» o «Carga en un punto»: fuerza X, Y, Z en N (Z hacia arriba, como en la cama), límite de
   deformación opcional y, para cargas puntuales, el radio en el que se reparte (3 mm por defecto: una carga real
   actúa sobre un área; con radios muy pequeños la tensión local crece sin límite).
5. «Calcular»: el análisis corre en segundo plano (cancelable) y muestra el veredicto, el factor de seguridad, el
   desplazamiento y el mapa de colores sobre la pieza.

6. «Pieza impresa» (activada por defecto): el análisis usa las paredes, techos, suelos y el relleno del perfil.
7. **Relleno mínimo** (también desde el botón Estructuras): busca el relleno uniforme más bajo y, si ahorra
   material, el relleno por zonas; muestra la masa frente al relleno actual y los aplica al objeto con un clic
   (densidad del objeto + modificadores «Tisma infill zone»). La vista «Relleno» oculta la capa maciza para ver el
   interior.

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
- **Solver**: gradiente conjugado con precondicionador de Jacobi, sin ensamblar la matriz (todos los vóxeles
  comparten la misma matriz de elemento); el producto se hace en paralelo (TBB) en 8 grupos de vóxeles sin nodos
  comunes. Memoria lineal con el número de vóxeles.
- **Condiciones**: los nodos a menos de 0,9 vóxeles de las caras fijas se bloquean; la fuerza de una carga sobre
  caras se reparte entre los nodos cercanos a ellas; la de una carga puntual entre los nodos a menos de 1,5 vóxeles
  (o el radio indicado) del punto.
- **Criterio de rotura** (simplificado): el mayor de von Mises / resistencia en XY, tracción en Z / adhesión entre
  capas y cortante entre capas / (0,6 × adhesión), todo a la temperatura de trabajo.

## Paredes y relleno (Fase 6)

Con `InfillModel` activado la pieza deja de ser maciza:

- **Capa maciza**: la parte de cada vóxel a menos del espesor de pared (perímetros) o de techo/suelo (capas
  sólidas) de la superficie es material macizo; la fracción se calcula con la distancia del centro del vóxel a la
  superficie y la normal de la cara más cercana.
- **Relleno homogeneizado**: rigidez E·ρ^nE y resistencia σ·ρ^nS con exponentes por patrón (rectilíneo/rejilla 1,2;
  panal 1,4; cúbico 1,5/1,4; giroide 1,6/1,5; lightning 2,5, no estructural). Son aproximados: hay que calibrarlos
  con probetas impresas.
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

Pendiente: comparación con CalculiX en una pieza real (prevista en la hoja de ruta).

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
