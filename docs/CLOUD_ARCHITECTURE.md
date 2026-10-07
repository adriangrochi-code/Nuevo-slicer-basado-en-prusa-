# Arquitectura de la nube (fases 10 y 11) — propuesta

Estado: **propuesta para decidir, no implementada.** Recoge lo hablado con el usuario el 06-10-2026. Los tiempos y
capacidades son estimaciones hasta medirlos en los equipos reales (ver «Medición»).

## Equipos

| Equipo | Procesador | RAM | GPU | Rol |
|---|---|---|---|---|
| **Central** | i5-9400F (6 núcleos / 6 hilos, AVX2) | 8 GB DDR4 (ampliar a 16–32 GB) | GTX 1060 | Entrada, reparto de trabajos y trabajos pesados |
| **Liviano 1** | i7 móvil 3.ª gen. (2 núcleos / 4 hilos, AVX) | 32 GB DDR3 | — | Trabajos livianos; base de datos y archivos |
| **Liviano 2** | i7 móvil 3.ª gen. (2 núcleos / 4 hilos, AVX) | 32 GB DDR3 | — | Trabajos livianos; copia de la base y de los archivos |

Cada i7 móvil rinde aproximadamente un 30–40 % del i5 en multinúcleo, con mucho menos consumo. Los tres juntos: unas
1,6–1,8 veces el i5 solo. La RAM no es intercambiable (DDR3 de notebook frente a DDR4 de escritorio).

## Flujo de un trabajo

```
Usuario (app / web / Tisma)
        │ HTTPS
        ▼
Router ──puerto 443──► CENTRAL (i5)
                         ├─ proxy HTTPS + API
                         ├─ planificador: estima el costo y elige la cola
                         │     ├─ cola «pesada»  ──► trabajador del i5 (CPU + GPU)
                         │     └─ cola «liviana» ──► trabajadores de los i7 (1 y 2)
                         └─ trabajador pesado
LIVIANO 1 (i7): base de datos, archivos, trabajador liviano
LIVIANO 2 (i7): copia de base y archivos, trabajador liviano
```

Todo el tráfico entre equipos va por la red local (IPs locales fijas, por ejemplo 192.168.1.10/11/12). La IP pública
es la del router y solo se abre el 443 hacia el central.

## Reparto de la carga

El planificador del central estima el costo de cada trabajo **antes** de encolarlo (no hace falta laminarlo):

| Trabajo | Estimación | Cola |
|---|---|---|
| FEA (cualquier resolución), relleno mínimo, refuerzo, lattice, orientación | Siempre pesado | Pesada (i5) |
| Detección de fallas (cámaras, tipo Obico) | GPU | Pesada (i5, en la 1060) |
| Laminado | Triángulos × volumen de la pieza × opciones caras (soportes en árbol, relleno gyroid, alta resolución): costo estimado en segundos del i5 | Liviana si el estimado es < ~15 s en el i5 (~45 s en un i7); pesada si no |
| Piezas que necesitan más RAM que la libre del i5 | Por tamaño de malla | Liviana (los i7 tienen 32 GB), aunque tarde más |

Desborde, para no dejar equipos quietos:
- Si la cola pesada tiene espera y un trabajo pesado **no necesita GPU** (laminado grande), lo puede tomar un i7 libre.
- Si la cola liviana se acumula y el i5 está libre, el i5 toma trabajos livianos.
- Prioridad por plan dentro de cada cola (Business > Pro > Maker > prueba gratis).

Los trabajadores **toman** trabajos de la cola cuando están libres (no se les empuja): si un equipo se apaga, los
demás siguen. Cada trabajador anuncia lo que puede hacer (GPU, AVX2, RAM libre) y la cola solo le da trabajos
compatibles.

Los cuatro hilos de cada i7 trabajan mejor con un trabajo a la vez (el laminado ya usa todos los núcleos). El i5,
con más RAM, puede correr un FEA y un laminado a la vez.

## Fallas

- El central es el punto único de entrada. Mitigación: keepalived en el central y en el Liviano 1; si el central se
  cae, el Liviano 1 toma la IP local de la entrada y sigue atendiendo trabajos livianos (los pesados esperan).
- Base de datos con copia continua al Liviano 2; archivos sincronizados al Liviano 2.

## Conexión a internet (a confirmar con el usuario)

- **CGNAT**: si el proveedor no da IP pública real no se pueden abrir puertos. Solución: Cloudflare Tunnel o un VPS
  barato con WireGuard como entrada.
- **IP dinámica**: DNS dinámico.
- **Ancho de banda (dato del usuario: 32 Mbps de subida, 220 Mbps de bajada).** Lo que entra al servidor (modelos que
  suben los usuarios, fotos de las cámaras: unas 150 impresoras con una foto cada 10 s ≈ 10 Mbps) usa la bajada y
  sobra. Lo que sale (G-code, resultados del FEA, vista de cámara en vivo) usa la subida de 32 Mbps ≈ 4 MB/s:
  - G-code binario (`.bgcode`, ~1/3 del texto) y resultados del FEA comprimidos: un G-code de 10 MB tarda ~2,5 s.
  - En el panel de granja, fotos cada pocos segundos en lugar de video en vivo (un video de 720p usa 1–2 Mbps por
    espectador); limitar los videos simultáneos.
  - Reservar parte de la subida para la API (límite de velocidad por descarga).

## Componentes y licencias

| Componente | Uso | Licencia |
|---|---|---|
| Tisma sin interfaz (línea de comandos) | Laminado y FEA | AGPL-3.0 (el código del servicio se publica) |
| Caddy | Proxy HTTPS | Apache 2.0 |
| Valkey | Cola | BSD-3 (Redis cambió de licencia; Valkey es la versión libre) |
| PostgreSQL | Usuarios, planes, trabajos | PostgreSQL (permisiva) |
| keepalived | IP de entrada de respaldo | GPL-2.0 (programa aparte) |
| Docker Compose | Instalación en cada equipo | Apache 2.0 |
| Obico (servidor) | Detección de fallas | AGPL-3.0 según su repositorio — **verificar** la licencia de los pesos del modelo y no usar la marca |

La cola, la API, la facturación y la app pueden ser código propio con la licencia que se elija mientras no incluyan
código de PrusaSlicer y hablen con el laminador como programa aparte. El laminador y el FEA siguen siendo AGPL y se
publican con sus cambios (también las optimizaciones para el servidor, como opciones de compilación).

## Planes (propuesta, sin decidir)

Cobro por créditos de cómputo (1 crédito ≈ 20 s del i5): laminar 1 (placas grandes 3), FEA Rápida/Normal 1, Alta 3,
Ultra 10, orientación 5, relleno mínimo / refuerzo / lattice 15.

| Plan | Precio (USD/mes) | Créditos | Notas |
|---|---|---|---|
| Local | Gratis | Sin límite | Todo en la PC del usuario |
| Prueba | Gratis | 20 | |
| Básico | 10 (anual 96) | 1.000 | Precio regional en LATAM; paquetes sueltos de 3–5 USD |
| Business | 49 con 3 usuarios + 12 por usuario extra | 5.000 + 1.000 por usuario extra, compartidos | Biblioteca de perfiles y materiales del equipo, proyectos compartidos con historial, roles, prioridad, factura, panel de granja con detección de fallas |

## Aceleración del FEA (fase propuesta)

1. Multigrid geométrico sobre la grilla de vóxeles (todas las PC): menos iteraciones del gradiente conjugado.
2. Cálculo en la GPU con **Vulkan** (NVIDIA, AMD, Intel, Android; Mac con MoltenVK), precisión mixta (multiplicación en
   simple, acumulación en doble). Si no hay GPU, sigue en el procesador.
3. En el servidor: opciones de compilación para el i5 (`-march=native`, AVX2) y bloques ajustados a la 1060, publicadas
   como el resto.

## Medición (antes de fijar precios)

En cada equipo: `build-utils/benchmark_slicing.sh` (laminado) y `libtisma_fea_tests "[.FEA-bench]"` (FEA con 60 000,
250 000 y 1,2 M de vóxeles). Con eso se ajustan la tabla de créditos y el umbral entre colas.
