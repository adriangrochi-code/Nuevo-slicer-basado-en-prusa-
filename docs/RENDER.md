# Render moderno (Fase 7)

Objetivo: dar a la vista 3D de Tisma el aspecto del nuevo render de PrusaSlicer 3.0 sin cambiar la arquitectura de
PrusaSlicer 2.9 (autorizado: Fase 7a y 7b).

## Auditoría de PrusaSlicer 3.0 (3.0.0-alpha12, 21-09-2026)

- Licencia AGPLv3, igual que Tisma: los shaders y las técnicas se pueden reutilizar citando el copyright de Prusa Research.
- La 3.0 reescribe el render entero: una capa propia sobre OpenGL (`src/slic3r-render`, inspirada en IGL, Filament,
  LLGL y Diligent), un grafo de escena (`src/slic3r-shared/.../Scene`) y una interfaz nueva. Portarla sería reescribir
  PrusaSlicer, lo que no está autorizado.
- Modos de sombreado (`GraphicsSettings.hpp`): `Legacy`, `Shadows`, `AO` y `PBR` (el predeterminado).
  - **Sombras**: mapa de sombras de 2048 px de la primera luz (la luz "superior", fija a la cámara), cámara ortográfica
    ajustada a la caja de los objetos que proyectan sombra, PCF 3×3, intensidad 0,75 (`phong_shadows.fs`,
    `Scene::render_shadowsmap_pass`).
  - **Oclusión ambiental (SSAO)**: G-buffer con profundidad, normales y color; 32 muestras, ruido 4×4, desenfoque
    horizontal y vertical (`gbuffer_ao*`, `ao_texture.fs`, `ao_hblur.fs`, `ao_vblur.fs`, `ao_lighting.fs`).
  - **PBR**: Cook-Torrance (GGX, Smith, Fresnel-Schlick) con metalicidad, rugosidad e índice de refracción por material,
    tonemapping de Reinhard.
- Las trayectorias del G-code (libvgcode) también reciben sombras y oclusión, y se ocultan al girar la cámara cuando la
  escena es muy densa.

## Lo implementado en Tisma

Archivos nuevos: `src/slic3r/GUI/TismaShading.hpp/.cpp`. Cambios acotados en los shaders `140` y en libvgcode.

### 7a. Iluminación por píxel

Las mismas dos luces de PrusaSlicer (superior con brillo especular y frontal, ambiente 0,3), calculadas en el
fragment shader en lugar de en el vertex shader:

- `gouraud` (piezas), `gouraud_light` (modelo de la cama, marcadores y asas de los gizmos).
- Trayectorias y opciones de la vista previa del G-code (libvgcode): la normal se interpola en la sección de cada
  trayectoria, que se ve redondeada en lugar de en dos caras planas.
- `mm_gouraud` (piezas pintadas) ya calculaba la luz por píxel en PrusaSlicer.

### 7b. Sombras

- Una pasada de profundidad por cuadro desde la luz superior (fija a la cámara, como en la 3.0) a un mapa de
  2048×2048 (`GL_DEPTH_COMPONENT24`). La cámara de la luz es ortográfica y se ajusta a la caja de los objetos opacos y
  de las trayectorias de la cama activa. La pasada usa el shader `flat` y `glPolygonOffset` contra el "acné".
- Proyectan sombra: los volúmenes opacos (no los modificadores) y las trayectorias.
- Reciben sombra: piezas, piezas pintadas, trayectorias, el modelo y la textura de la cama, y la cama por defecto.
- Los receptores pasan de su posición en el espacio de la cámara al mapa de sombras con una sola matriz
  (`eye_to_shadow = proyección_luz · vista_luz · vista_cámara⁻¹`), válida para cualquier matriz de modelo.
- Detrás del plano lejano de la luz (la cama lejos de una pieza alta) el mapa se usa como silueta, como en la 3.0.
- Al terminar el cuadro los shaders dejan de recibir sombras, así las miniaturas y el picking no cambian.

### Preferencias

Preferencias → Cámara → **Calidad de render** (`tisma_render_quality` en `PrusaSlicer.ini`):

| Valor | Modo | Descripción |
|---|---|---|
| 0 | Clásico | Como PrusaSlicer 2.9 (luz por vértice). |
| 1 | Por píxel | 7a. |
| 2 | Sombras | 7a + 7b. |
| 3 | Oclusión ambiental (predeterminado) | 7b + 7c, luz Phong. |
| 4 | Materiales realistas | 7b + 7c + 7d, luz PBR. |

Solo con OpenGL 3.2 o superior; con OpenGL 2.x (shaders `110`) y OpenGL ES se usa siempre el modo Clásico. Si el
G-buffer no se puede crear, los modos 3 y 4 quedan como Sombras.

### 7c. G-buffer y oclusión ambiental (autorizado como refactor del render)

Como los modos «AO» y «PBR» de PrusaSlicer 3.0, con luz diferida:

1. **G-buffer** (`TismaShading::begin_gbuffer()`): las piezas opacas, la cama vista desde arriba (modelo y textura) y
   las trayectorias se dibujan con sus mismos shaders a un framebuffer propio: profundidad (24 bits), color antes de la
   luz con el material en el alfa (RGBA8) y la normal en el espacio de la cámara codificada en dos números (RG16F,
   codificación octaédrica de la 3.0). Los shaders `gouraud`, `mm_gouraud`, `gouraud_light`, `printbed` y los de
   libvgcode lo escriben cuando su uniforme `tisma_gbuffer` está activo; al terminar la pasada se apaga.
2. **Oclusión ambiental** a media resolución (`tisma_ssao.fs`): 32 muestras en una semiesfera alrededor de la normal,
   rotadas por una textura de ruido de 4×4, radio 30 mm, sesgo 1,5, umbral 5 mm; luego un desenfoque de caja de 5×5
   en dos pasadas (`tisma_blur.fs`). Son los valores de la 3.0 (`GraphicsSettings.hpp`).
3. **Luz** (`tisma_deferred.fs`): una pasada a pantalla completa ilumina cada píxel con las sombras de la luz principal
   y la oclusión sobre la luz ambiente, y escribe la profundidad del G-buffer. Después se dibuja lo demás como antes
   (objetos transparentes, selección, gizmos, cama vista desde abajo, ejes, leyendas), sobre esa profundidad.
   Los shaders que no escriben material (contornos, pintura de los gizmos, alturas variables) quedan como se
   dibujaron, sin luz nueva.

Queda fuera del G-buffer, como en la 3.0, lo transparente: la cama por defecto (sin modelo ni textura, o con modelos
superpuestos en varias planchas), la cama vista desde abajo y los objetos transparentes. El G-buffer no tiene
antialiasing multimuestra: los bordes de las piezas opacas se ven como en la 3.0.

### 7d. Materiales (PBR)

Cook-Torrance (GGX, Smith, Fresnel-Schlick) con tonemapping de Reinhard y las mismas dos luces, como `ao_lighting.fs`
de la 3.0 (intensidad 20, ambiente 0,5). Metalicidad, rugosidad e índice de refracción de la 3.0
(`LightingHelper.hpp`):

| Material | Metal | Rugosidad | IOR |
|---|---|---|---|
| Piezas | 0 | 0,25 | 1,5 |
| Piezas de filamento «Silk» (nombre del perfil) | 0,2 | 0,12 | 1,5 |
| Piezas de filamento «Matte» | 0 | 0,7 | 1,5 |
| Soportes y pad SLA | 0 | 0,5 | 1,4 |
| Cama (textura y modelo) | 0,5 | 0,5 | 1,5 |
| Trayectorias | 0 | 0,5 | 1,5 |
| Opciones de la vista previa (retracciones, ...) | 0 | 0,2 | 2,0 |

«Silk» y «Matte» son una aproximación visual de Tisma (más brillante y más áspero que el valor de la 3.0), no datos
de los fabricantes. El brillo por material se aplica a las piezas; las trayectorias usan el material de la 3.0 porque
su color depende de la vista (tipo de extrusión, velocidad, ...).

## Pendiente

- Ocultar trayectorias al girar la cámara en escenas muy densas (la 3.0 lo hace).
- Medir el rendimiento de los modos 3 y 4 en gráficos integrados: en el entorno de desarrollo solo hay render por
  software (llvmpipe).
