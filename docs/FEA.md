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

Rendimiento medido: 62 370 vóxeles, 704 iteraciones, 2,5 s en 4 núcleos.

Pendiente: comparación con CalculiX en una pieza real (prevista en la hoja de ruta).

## Límites que hay que conocer

- **Pieza maciza**: el análisis supone la pieza 100 % sólida. Las paredes y el relleno reales se tendrán en
  cuenta en la Fase 6 (relleno adaptativo y validación con la estructura real). Con relleno bajo, la pieza real
  es más blanda y menos resistente que el resultado.
- **Valores de material aproximados**: valores típicos de fichas técnicas y bibliografía para piezas impresas; hay
  que verificarlos con la ficha del filamento usado. La impresión (temperatura, ventilación, orientación) los cambia
  mucho, sobre todo la adhesión entre capas.
- **Concentraciones**: en las cargas puntuales y en los bordes de los empotramientos la tensión depende del tamaño
  del vóxel (singularidades). El punto crítico se informa para revisarlo.
- **Lineal**: pequeñas deformaciones y material elástico; no hay pandeo, fluencia (creep) ni fatiga. La fluencia
  importa mucho a temperatura alta y con cargas permanentes.
- **Bordes escalonados**: los vóxeles aproximan las superficies curvas; el error baja al reducir el vóxel.
