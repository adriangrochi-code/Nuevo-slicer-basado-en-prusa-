# Perímetros escalonados (alternativa a Brick Layers)

## La patente US11331848B2

Revisión técnica, no asesoramiento legal. Para distribuir Tisma de forma comercial conviene que la confirme un
abogado de patentes.

| Dato | Valor |
| --- | --- |
| Título | 3D printing bead configuration |
| Titular | Addman Intermediate Holdings LLC (inventor: Mark Saberton) |
| Presentada / prioridad | 24-06-2020 / 26-11-2019 |
| Concedida | 17-05-2022 |
| Estado | Vigente: pagó la tasa de mantenimiento del 4.º año el 14-10-2025 |
| Vence | 24-06-2040 (si se siguen pagando las tasas) |

Fuente: [Google Patents](https://patents.google.com/patent/US11331848B2/en).

**¿Es una «renovación» de la patente vencida de Stratasys?** No en sentido legal: una patente no se renueva. La
patente de Stratasys [US5653925](https://patents.google.com/patent/US5653925A/en) (presentada en 1995, vencida en
2015) ya describía cordones de capas vecinas desplazados medio diámetro para bajar la porosidad, y la patente de
2020 la cita con un número equivocado (5,659,925, que es un cierrapuertas). Eso es un argumento de *arte previo*
para pedir la nulidad, pero mientras ningún tribunal o la oficina de patentes la anule, la patente se presume válida.

**Qué protege.** Tiene una sola reivindicación independiente (la 1) y todos sus elementos tienen que estar para
que haya infracción:

1. capas de cordones, cada capa en un plano horizontal;
2. en cada capa, un cordón terminal en cada extremo y cordones intermedios entre ellos;
3. el segundo cordón terminal depositado **perpendicular** a los demás de su capa;
4. ese cordón terminal perpendicular de **la mitad del ancho** de los otros;
5. los cordones terminales alternando de extremo entre capas vecinas.

La reivindicación 2 suma el desplazamiento lateral de los cordones intermedios entre capas. Es una configuración
de «medio ladrillo» en los extremos de paredes con principio y fin, como en mampostería.

## El diseño de Tisma

**Perímetros escalonados en Z.** Uno de cada dos perímetros internos se imprime medio alto de capa más arriba. Así
las juntas entre capas de perímetros vecinos no quedan alineadas y la pared se traba como una pared de ladrillos.

Queda fuera de la reivindicación 1 porque:

- los perímetros son lazos **cerrados**: no tienen extremos ni cordones terminales;
- no se deposita ningún cordón perpendicular a los demás ni de medio ancho: todos los cordones de un lazo
  tienen el mismo ancho y siguen el contorno;
- el desfase es **vertical** (en Z, fuera del plano de la capa), no lateral dentro de capas horizontales;
- el perímetro externo nunca se desplaza: la superficie queda igual.

Detalles:

- Se desplazan los perímetros internos impares (el 1.º interno, el 3.º, ...) solo donde la capa siguiente
  también tiene pieza encima.
- Primera capa: el perímetro desplazado sale con 1,5 veces el flujo, porque va de la cama a media capa más
  arriba.
- Última capa sobre ese lugar: el perímetro vuelve a la altura normal con la mitad del flujo, para que nada
  sobresalga de la superficie superior.
- Con altura de capa variable, el flujo se calcula con la altura real entre el cordón de abajo y el de arriba.

El ensayo de CNC Kitchen con la técnica original midió de 10 % (PETG) a 14 % (PLA) más resistencia a tracción
entre capas, sin material extra. Hay que medir la de Tisma con probetas.
