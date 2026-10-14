# Dividir para que entre en la impresora (paso 18)

Un objeto más grande que el volumen de impresión se corta en piezas que entran, con encastres en cada corte para que
las piezas queden alineadas al pegarlas. Código: `src/libslic3r/Fragment.cpp` (núcleo, sin interfaz),
`src/slic3r/GUI/FragmentDialog.cpp` (diálogo) y `Plater::fragment_object()`. Tests: `tests/libslic3r/test_fragment.cpp`.

## Uso

- Clic derecho en el objeto > **Dividir para que entre en la impresora…**, o el enlace **Dividirlo en piezas con
  encastres** del aviso que aparece al cargar un objeto más grande que la impresora.
- El diálogo deja elegir los encastres y muestra en vivo cuántas piezas, cortes y encastres salen:
  - **Pernos** (por defecto): agujeros en las dos piezas y un perno aparte que se imprime de pie. Es lo que mejor se
    imprime: los agujeros horizontales no necesitan soporte y el perno sale redondo.
  - **Espigas**: la espiga sale de una pieza y entra en un agujero de la otra (no hay piezas sueltas, pero en un corte
    vertical la espiga queda horizontal, en voladizo).
  - **Ninguno**: solo pegamento.
  - Diámetro, profundidad en cada pieza y holgura de los agujeros (radial y en profundidad). Se recuerdan.
- Las piezas reemplazan al objeto (se puede deshacer) y se reparten, con los pernos, en las camas que hagan falta.

## Cómo se decide

1. **Tamaño útil**: el rectángulo de la cama (en una cama redonda, el cuadrado inscrito) y la altura máxima. En
   X e Y se deja un margen por lado para lo que el acomodo pone alrededor de cada pieza: la falda (vueltas × ancho +
   distancia), el borde y la mitad de la separación entre objetos. Con espigas se descuenta además lo que la espiga
   sobresale.
2. **Cantidad de piezas por eje**: la menor que entra (`ceil(tamaño / tamaño útil)`), y una más si no hay forma de
   ubicar los cortes respetando el largo mínimo de una pieza (lugar para los encastres de sus dos caras).
3. **Dónde cortar**: se mide la sección del objeto cada 0,5 mm (o tamaño/1500) a lo largo del eje y una programación
   dinámica elige las posiciones que dejan todas las piezas dentro del tamaño útil minimizando la suma de las
   secciones (menos para pegar y una costura más corta), penalizando una sección donde no entra ningún encastre y,
   un poco, la distancia a piezas iguales (en una pieza de sección constante, los cortes quedan parejos). Ejemplo
   del test: dos bloques unidos por un cuello de 20 × 20 mm se cortan en el cuello.
4. **Encastres**: en cada corte, cada parte de la sección entre los otros cortes (cada par de piezas que se tocan)
   recibe encastres:
   - el centro queda a `diámetro/2 + holgura + 2 mm de pared` del borde de la sección, también a una profundidad de
     encastre a cada lado del corte (donde van los agujeros), y a la misma distancia de los otros cortes, así ningún
     encastre cruza otro plano;
   - de 1 a 4 por isla según su área, separados al menos 4 diámetros, el primero cerca del centro de la isla;
   - si la pared no da para el diámetro elegido se prueba con 0,6 veces el diámetro y con 3 mm. En un Benchy al 500 %
     cortado para una 101Hero, los encastres pasan de 2 a 29; las partes que ni así tienen lugar quedan solo para
     pegamento y el diálogo dice cuántas son.
5. **Corte**: se hace con la herramienta de corte de PrusaSlicer (`Cut::perform_with_plane()` con los conectores
   como volúmenes negativos, igual que `GLGizmoCut3D::apply_cut_connectors()`), un plano por vez: primero los de X,
   luego Y y Z, cada uno sobre las piezas que atraviesa.

## Límites conocidos

- El corte de PrusaSlicer deja la malla abierta si el plano pasa exactamente por vértices (por ejemplo por las
  esquinas que dejó otro corte: lo vimos con una placa cortada en X y en Y). Los planos se corren 0,0137 mm de las
  posiciones redondas; el test de la placa verifica que las piezas queden cerradas.
- Las piezas conservan la orientación del objeto; no se giran para imprimir mejor.
- Solo objetos con una copia (con varias, cuál dividir no es claro).
- No hay colas de milano ni lengüeta y ranura automáticas (la herramienta de corte tiene lengüeta y ranura, pero
  para un solo corte por objeto).
- Un proyecto tiene como máximo 9 camas: con muchas piezas en una cama chica (el Benchy al 500 % en una 101Hero da
  24 piezas y 29 pernos) algunas quedan fuera de las camas y Tisma avisa «objeto fuera del área»; el diálogo lo
  advierte cuando hay más piezas que camas.
- Camas no rectangulares ni redondas (formas personalizadas): se usa el rectángulo que las contiene.

## Verificación

- `libslic3r_tests "[Fragment]"`: objeto que entra (sin cortes), barra de 600 mm (3 piezas parejas, pernos,
  volumen conservado), espigas (la espiga cuenta en el largo), cuello (corte en la sección más chica), placa de
  400 × 300 (cortes en X e Y, encastres lejos del otro corte, piezas cerradas), cilindro de 500 mm de alto (cortes en
  Z) y caja hueca de paredes de 8 mm (encastres más finos).
- Interfaz (Xvfb): Benchy al 500 % con una 101Hero (cama redonda de 150 mm): aviso al cargarlo, diálogo con 24
  piezas y 29 encastres, división y acomodo en las 9 camas (con el aviso de piezas fuera, ver límites).
- No probado: imprimir y pegar las piezas (holguras reales), Windows.
