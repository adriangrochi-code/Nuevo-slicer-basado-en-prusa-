# Análisis aerodinámico (Ingeniería > Aerodinámica)

Estimación del aire alrededor de la pieza **tal como se imprime**: arrastre, fuerza transversal, mapa de presión y
efecto de la rugosidad de las capas. Sirve para comparar diseños y orientaciones; no reemplaza un túnel de viento ni un
CFD profesional.

## Cómo se calcula

El arrastre se separa en dos partes, porque la grilla no puede resolver la capa límite real de una pieza a una
velocidad real:

1. **Presión (forma)**: simulación lattice Boltzmann propia (D3Q19, BGK con modelo de submalla de Smagorinsky) sobre
   la pieza vóxelizada, girada para que el aire vaya por +X. Dominio: 1,5 tamaños frontales delante, 3 detrás y 1,5 a
   los lados (laterales periódicos), entrada a velocidad fija y salida de gradiente nulo. La presión media de la última
   tercera parte de la simulación se integra sobre las caras de la pieza. La simulación corre a un Reynolds limitado por
   la resolución (40 × celdas del tamaño frontal); el arrastre de presión de formas romas cambia poco por encima de
   ~1000.
2. **Rozamiento**: correlaciones de placa plana integradas sobre las caras (subdivididas cada ~2 mm) según la
   distancia al borde de ataque: laminar de Blasius, turbulento liso, o totalmente rugoso de Schlichting. La capa límite
   pasa a turbulenta por encima de Re_x = 5·10⁵ o cuando la rugosidad la dispara (U·ks/ν > 300). Las caras que miran
   aguas abajo (estela separada) no suman rozamiento.

**Rugosidad de la impresión** (por cara, Z = dirección de impresión): Ra = máx(0,05·h, h·|n_z|/4) en caras inclinadas
(escalones de altura h·cos φ, perfil diente de sierra), 0,05·h en paredes verticales y caras planas (borde redondeado
del cordón). Rugosidad equivalente de arena ks ≈ 4·Ra. h es la altura de capa del perfil de impresión.

Código: `src/libtisma_fea/src/Aero.cpp`. Interfaz: pestaña Aerodinámica del gizmo de Ingeniería.

## Validación (`libtisma_fea_tests "[Aero]"`)

| Caso | Tisma | Referencia |
|---|---|---|
| Placa cuadrada de frente | Cd 1,16 | 1,17–1,2 (Hoerner) |
| Cubo de frente | Cd 1,13 | 1,05 (Hoerner); +8 % por bloqueo del dominio |
| Mismo cubo con el aire en otra dirección | igual | — |
| Prisma 3:1 romo / con nariz piramidal | 1,06 / 0,34 | menos arrastre con nariz |
| Rozamiento laminar de un prisma 6:1 | a < 5 % de Blasius | 1,328/√Re_L |
| Prisma impreso inclinado 45° | Ra 15 → 35 µm, rozamiento ×2,4 | caras escalonadas más rugosas |

## Uso

- Dirección del aire (ejes o «hacia adentro de la vista»), velocidad en m/s, calidad (Rápida 16, Normal 24, Alta 32
  celdas en el tamaño frontal; «Solo rozamiento» es instantáneo, sin simulación).
- Tiempo: unos 5 minutos un Benchy en calidad Normal con 4 núcleos; escala con el número de núcleos.
- Resultado: arrastre en N, Cd (presión + rozamiento), fuerza transversal, área frontal, Reynolds real y simulado,
  rugosidad media y su parte del rozamiento. Mapa de colores del coeficiente de presión (de −1,5 succión a 1
  estancamiento) o de la rugosidad.

## Límites

- Presión simulada a Reynolds bajo: no capta la crisis de arrastre (esferas y cilindros por encima de ~2·10⁵) ni
  separaciones finas en cuerpos fuselados.
- Bloqueo del dominio: los cuerpos romos dan unos 5–10 % más de Cd.
- Los detalles más finos que una celda (≈ tamaño frontal / 24) no se ven.
