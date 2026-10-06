# Instalador de Windows

`TismaSlicer.iss` es el script del instalador (Inno Setup 6, licencia permisiva, uso comercial permitido; solo se usa
para construir el instalador, no se distribuye con Tisma). Lo construye el CI (`.github/workflows/build_windows.yml`,
paso «Installer») y lo publica como artefacto `TismaSlicer-windows-installer`.

El instalador:
- Instala para el usuario actual (sin permisos de administrador) o para todos los usuarios, a elección.
- Crea accesos en el menú Inicio (Tisma Slicer y el visor de G-code) y, opcionalmente, en el escritorio.
- Asocia, si se elige, `.3mf`, `.stl`, `.obj`, `.step`/`.stp` con Tisma y `.gcode`/`.bgcode` con el visor (como «Abrir
  con», sin quitar otros programas).
- Una versión nueva reemplaza a la anterior (mismo `AppId`, no cambiarlo nunca).
- El desinstalador conserva la configuración, los perfiles y los proyectos del usuario.

El CI prueba cada instalador: instalación silenciosa, el programa instalado arranca, desinstalación silenciosa.

## Firma de código

Sin firma, Windows SmartScreen muestra «Windows protegió su PC» la primera vez. Con firma aparece el nombre del
publicador; la advertencia desaparece a medida que el certificado gana reputación (descargas sin incidentes).

El CI firma los ejecutables, las DLL propias y el instalador si existen dos secretos del repositorio (Settings →
Secrets and variables → Actions):

| Secreto | Contenido |
|---|---|
| `WINDOWS_SIGN_PFX_BASE64` | El certificado de firma de código (.pfx) en base64 |
| `WINDOWS_SIGN_PFX_PASSWORD` | Su contraseña |

Opciones para obtener el certificado (la decisión y el trámite de identidad son del titular del proyecto):

1. **SignPath Foundation** (gratuito para proyectos de código abierto): exige licencia OSI (AGPL lo es), repositorio
   público, proyecto mantenido y ya publicado, y compilación en runners de GitHub. El publicador que muestra Windows es
   «SignPath Foundation». Se integra con su acción de GitHub (`signpath/github-action-submit-signing-request`) en lugar
   del paso con `.pfx`.
2. **Certificado OV comprado** (Certum, Sectigo, SSL.com, etc.): el publicador es el titular. Desde 2023 la clave debe
   estar en un token o un servicio en la nube (HSM); para el CI hace falta firma en la nube (por ejemplo eSigner de
   SSL.com o SimplySign de Certum) y el paso de firma se adapta a ese servicio.
3. **Azure Artifact Signing** (ex Trusted Signing, ~10 USD/mes): hoy solo para personas de EE. UU. y Canadá u
   organizaciones de EE. UU., Canadá, la UE y el Reino Unido.
