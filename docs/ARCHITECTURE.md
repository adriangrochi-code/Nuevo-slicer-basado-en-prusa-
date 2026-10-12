# Arquitectura — Tisma Slicer

Fecha: 2026-10-05. Estado: **propuesta para revisión**; nada de lo descrito como "nuevo" está implementado.

## 1. Arquitectura existente (PrusaSlicer 2.9.6 + fork)

```
             ┌────────────────────────── GUI (src/slic3r, wxWidgets + OpenGL + ImGui) ─────────────────────────┐
             │ MainFrame ─ NavRail* ─ Plater ─ Sidebar (QuickSettings*) ─ Tab (presets) ─ Gizmos ─ GCodeViewer  │
             │ Jobs/Worker (tareas asíncronas)   BackgroundSlicingProcess (hilo de laminado)                    │
             └───────────────┬─────────────────────────────────────────────────────────────┬───────────────────┘
                             │ Model + DynamicPrintConfig                                  │ GCodeProcessorResult
             ┌───────────────▼──────────────── libslic3r (núcleo) ─────────────────────────┴───────────────────┐
             │ Format/ (STL, 3MF, OBJ, STEP→occt_wrapper)   Model / ModelObject / ModelVolume / FacetsAnnotation │
             │ Print → PrintObject: slice → perimeters → infill (Fill/) → support → GCode (GCode/) → processor   │
             │ NonPlanar* (deformación + transformación del G-code)   calib_* en GCode*                          │
             └───────────────────────────────────────────────────────────────────────────────────────────────────┘
* = fork
```

Puntos fuertes reutilizables:

- **Configuración por niveles** (global → objeto → volumen → rango de capas) y **modificadores**: permiten variar
  densidad, perímetros y patrón por zona sin tocar el motor de relleno.
- **`TriangleSelector`**: pintado por facetas con serialización compacta y guardado en 3MF.
- **Pasos invalidables** (`PrintStep` / `PrintObjectStep`): un cambio solo recalcula lo necesario.
- **Transformación posterior del G-code** (ya usada por `NonPlanar`): el mismo patrón sirve para la impresora de cinta.
- **`Jobs/Worker`** y TBB para tareas largas sin bloquear la interfaz.

Puntos débiles:

- `GLCanvas3D`, `Plater` y `GCode` son monolitos de 4000–8000 líneas; cualquier cambio grande en ellos complica las fusiones con upstream.
- El renderizado no tiene abstracción: OpenGL está repartido por toda la GUI.
- La geometría CAD se descarta al importar (solo queda la malla).

## 2. Principios de la propuesta

1. **Lo experimental fuera del núcleo**: cada subsistema nuevo será una biblioteca CMake propia bajo `src/`, que
   dependa de `libslic3r` y no al revés. El núcleo solo recibe "puntos de enganche" pequeños.
2. **Reutilizar los mecanismos existentes** antes de crear otros (modificadores, pintado, pasos, Jobs).
3. **El proyecto es la fuente de verdad**: todo lo de ingeniería se guarda en el 3MF con versión propia y se
   ignora sin errores en PrusaSlicer y otros laminadores.
4. **Cambios pequeños y verificables**, cada uno con pruebas.

## 3. Correspondencia de subsistemas

| Subsistema (especificación) | Ubicación propuesta | Reutiliza |
|---|---|---|
| Core | `libslic3r` (sin cambios de estructura) | Model, Print, Config |
| Geometry (CAD) | `src/occt_wrapper` ampliado + `libslic3r/Cad/` (nuevo, pequeño) | OCCT 7.x ya integrado |
| Slicing | `libslic3r` | todo |
| Structures | `src/libtisma_structures/` (nuevo) | modificadores, `Fill/` |
| Simulation | `src/libtisma_fea/` (nuevo) | Eigen, TBB |
| Optimization | dentro de `libtisma_structures` al principio | NLopt (ya presente) |
| Manufacturing | `libslic3r/Manufacturing/` (nuevo) para cinta; `NonPlanar` se mantiene donde está | transformación de G-code |
| Graphics | `src/slic3r/GUI` (OpenGL actual); abstracción solo en la Fase 8 | `GLModel`, `GLShader` |
| Profiles | `libslic3r/Preset*` | — |
| GCode | `libslic3r/GCode*` | — |

## 4. Modelo de datos de ingeniería (nuevo)

```cpp
// libslic3r/Engineering.hpp (propuesta)
enum class BCType { Fixed, FixedAxes, Force, Pressure, Temperature, HeatFlux };

struct FaceSelection {               // región pintada
    FacetsAnnotation facets;         // mismo formato que el pintado actual (por triángulo)
    std::vector<int> cad_faces;      // ids de cara B-Rep si el volumen viene de STEP (vacío si no)
    uint64_t         mesh_hash;      // malla sobre la que se pintó: detecta selecciones invalidadas
};

struct BoundaryCondition {
    ObjectID       id;
    std::string    name;
    BCType         type;
    FaceSelection  region;
    Vec3d          direction;        // fuerza / presión
    double         magnitude;        // N, MPa, °C…
    int            load_case;
};

struct EngineeringSetup {            // por ModelObject
    std::vector<BoundaryCondition> conditions;
    std::vector<std::string>       load_cases;
    std::string                    material_id;
    int                            version = 1;
};
```

- Se guarda en el 3MF como `Metadata/Tisma_engineering.config` (XML) con atributo `version`. PrusaSlicer ignora
  archivos desconocidos del paquete, así que el 3MF sigue abriéndose en él (pierde solo la parte de ingeniería).
- Si la malla cambia (`mesh_hash` distinto) y no hay ids de cara CAD, la condición se marca **inválida** y se
  avisa al usuario. Si hay ids de cara, se reproyecta sobre la nueva teselación.
- Los resultados de simulación **no** se guardan por defecto (se recalculan); como opción, en un archivo binario
  aparte dentro del 3MF.

## 5. Geometría CAD

- Ampliar `occt_wrapper` para devolver, además de la malla, el **id de cara B-Rep de cada triángulo** y conservar
  el archivo STEP original dentro del 3MF.
- La malla de visualización y laminado sigue siendo `TriangleMesh` (sin cambios aguas abajo). La malla volumétrica
  de FEA la genera `libtisma_fea`.
- Retesselar con otra tolerancia = volver a leer el STEP guardado; las selecciones se reproyectan por id de cara.
- Riesgo conocido: los ids de cara no son estables si el usuario cambia el CAD (problema del "nombrado
  topológico"). Solo se garantiza la estabilidad al retesselar el mismo STEP.

## 6. FEA (`libtisma_fea`)

Primera versión propuesta (a validar con el usuario):

- **Malla de vóxeles hexaédricos** generada desde la malla de la pieza (ya existen utilidades de vóxeles con
  OpenVDB en `libslic3r/OpenVDBUtils.*`). Ventajas: robusta con mallas imperfectas, trivial de paralelizar, encaja con densidades por capa.
  Inconveniente: superficies escalonadas (tensiones locales menos fiables en los bordes).
- Elasticidad lineal, pequeñas deformaciones, material **ortótropo** (dirección Z de impresión más débil).
- Solver: gradiente conjugado con precondicionador (Eigen) en CPU multihilo. GPU en la Fase 8.
- Salidas: desplazamiento, tensión de von Mises, tensiones principales y sus direcciones, por vóxel.
- Interfaz:

```cpp
struct FeaInput  { VoxelGrid grid; MaterialModel material; std::vector<BoundaryCondition> bcs; int load_case; };
struct FeaResult { Field<Vec3f> displacement; Field<float> von_mises; Field<Mat3f> stress; Diagnostics diag; };
FeaResult solve(const FeaInput&, const CancellationToken&, ProgressFn);   // se ejecuta dentro de un Job
```

- Cada resultado lleva sus **hipótesis y límites** (lineal, sin pandeo, sin fatiga, propiedades de material
  supuestas) y se presenta como estimación, nunca como garantía.

## 7. Relleno adaptativo híbrido (`libtisma_structures`)

Flujo de la especificación (§8.2) sobre el código existente:

| Etapa | Implementación propuesta |
|---|---|
| 1. Geometría | Sin cambios (modelo + perímetros configurados). |
| 2. Condiciones | `EngineeringSetup` (§4) con un gizmo nuevo derivado de `GLGizmoPainterBase`. |
| 3. Modelo preliminar | FEA sobre el volumen interior con propiedades **homogeneizadas** del relleno base. |
| 4. Generación | Campo de densidad y orientación desde las tensiones. **v1**: se materializa como **modificadores internos** por bandas de densidad (reutiliza el pipeline actual, riesgo bajo). **v2**: un `Fill` nuevo que lee el campo continuo. **v3**: lattice 3D como volúmenes de malla. |
| 5. Validación | Se voxeliza la pieza **ya laminada** (perímetros y relleno reales, con propiedades efectivas por densidad) y se repite el FEA. |
| 6. Iteración | Manual en v1; bucle automático después. |
| 7. Fabricación | Laminado y G-code normales. |

Los objetivos de optimización (masa, rigidez, tiempo…) serán parámetros configurables; no se fija ninguno por defecto.

## 8. Fabricación

- **No planar**: se mantiene `NonPlanar` y se añade detección de colisiones con un modelo simplificado del
  cabezal (cilindro + cono de la boquilla), configurable por impresora.
- **Impresora de cinta**: nuevo `libslic3r/Manufacturing/BeltTransform` como transformación afín
  modelo → fabricación (cizalla según el ángulo configurable de la cinta) antes del laminado, y fabricación →
  máquina al escribir el G-code. Requiere ajustes en primera capa, soportes, vista previa y límites de cama
  (cama "infinita" en el eje de la cinta).
- **Trackhead**: pendiente de definición del usuario; no se diseña nada todavía.

## 9. Gráficos

- Fases 1–7: OpenGL actual, sin cambios de arquitectura.
- Fase 8: introducir una capa de abstracción mínima (buffers, shaders, estados) empezando por las piezas nuevas
  (visualización de resultados FEA). Migrar la escena entera a Vulkan exige reescribir `GLCanvas3D`, `GLModel`,
  `libvgcode` y el backend de ImGui: es la tarea más cara de toda la especificación y conviene decidirla con datos
  de rendimiento reales.
- El cómputo (FEA) queda **desacoplado** del renderizado: CPU primero; GPU (Vulkan Compute u otra API) opcional,
  con resultados comparados contra la CPU dentro de una tolerancia.

## 10. Concurrencia

- Tareas largas de la GUI (FEA, generación de estructuras, retesselado) → `Jobs/Worker` existente: progreso,
  cancelación y errores ya resueltos.
- Paralelismo interno → TBB (ya usado en todo el núcleo).
- Laminado → `BackgroundSlicingProcess` existente. El FEA de validación (etapa 5) se ejecuta después del paso de
  relleno, como un paso nuevo invalidable.

## 11. Compatibilidad

- Proyectos: el 3MF sigue el formato de PrusaSlicer; lo nuevo va en archivos aparte versionados.
- Perfiles: claves nuevas con valores por defecto neutros; los perfiles existentes cargan sin cambios.
- G-code: sin cambios si no se activa ninguna función nueva.

## 12. Plan de pruebas

| Nivel | Qué | Cómo |
|---|---|---|
| Unitarias | FEA: barra a tracción, voladizo (δ = FL³/3EI), patch test, convergencia con el tamaño de vóxel | Catch2, tolerancias documentadas |
| Unitarias | Transformación de cinta: ida y vuelta modelo ↔ máquina | Catch2 |
| Unitarias | Calibraciones: comandos y valores por tramo en el G-code | Catch2 + `Print` en memoria |
| Integración | 3MF con condiciones de contorno: guardar, abrir, abrir en formato antiguo | Catch2 |
| Regresión | G-code de modelos de referencia comparado con el de la versión anterior | script + CI |
| Validación física | Probetas impresas y ensayadas contra la predicción | Manual, documentado |
| GUI | Arranque, navegación y flujos principales | Xvfb + capturas (manual por ahora) |
