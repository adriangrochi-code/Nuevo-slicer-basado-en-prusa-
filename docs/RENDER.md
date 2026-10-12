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
| 2 | Sombras (predeterminado) | 7a + 7b. |

Solo con OpenGL 3.2 o superior; con OpenGL 2.x (shaders `110`) y OpenGL ES se usa siempre el modo Clásico.

## Pendiente (no autorizado todavía)

- 7c: oclusión ambiental (SSAO), necesita un G-buffer y adaptar transparencias, gizmos y antialiasing.
- 7d: brillo por material del filamento (PBR) y ocultar trayectorias al girar en escenas densas.
