# Firma de Tisma en Windows

Sin firma, Windows muestra «Editor desconocido» y SmartScreen avisa al abrir el instalador. La firma pone el nombre
del editor y permite que SmartScreen vaya acumulando reputación. Desde 2024 ningún certificado (ni EV) quita el aviso
desde el primer día: desaparece cuando el instalador firmado acumula descargas sin denuncias.

Desde junio de 2023 la clave privada de un certificado de firma tiene que estar en hardware seguro (token o HSM en la
nube). Por eso ya no se puede cargar un archivo `.pfx` como secreto de GitHub con un certificado nuevo. El paso
«Sign binaries» con `WINDOWS_SIGN_PFX_BASE64` queda solo para certificados viejos.

## Opción recomendada: SignPath Foundation (gratis)

Firma gratuita para proyectos de código abierto. El editor que muestra Windows es «SignPath Foundation», no Tisma.
El flujo de trabajo ya tiene los pasos (`Sign package (SignPath)` y `Sign installer (SignPath)`).

Condiciones que ya se cumplen en el repositorio:

- licencia AGPL (aprobada por OSI) sin licencia comercial doble;
- compilación automática en GitHub Actions desde el repositorio público;
- política de firma publicada ([CODE_SIGNING_POLICY.md](CODE_SIGNING_POLICY.md)) y enlazada desde el README;
- metadatos propios en los ejecutables (empresa «Tisma», producto «Tisma Slicer»);
- derivado de PrusaSlicer, que publica sus versiones firmadas (requisito para las bifurcaciones).

Pasos del dueño del repositorio:

1. Publicar una versión en GitHub Releases (SignPath pide un proyecto ya publicado y con mantenimiento): crear y
   subir una etiqueta `v<versión>` (por ejemplo `v0.9.0`). El flujo de trabajo de Windows compila, prueba el
   instalador y publica la versión con el instalador, el zip y las notas de esa versión de `CHANGELOG.md`.
2. Activar la verificación en dos pasos en GitHub.
3. Pedir el alta en <https://signpath.org/apply> con el enlace al repositorio y a la política de firma.
4. Cuando lo aprueben, en SignPath: proyecto `tisma-slicer`, política `release-signing` y dos configuraciones de
   artefacto, `package` (los `.exe` y nuestras DLL dentro del zip) e `installer` (el `setup.exe`).
5. En GitHub > Settings > Secrets and variables > Actions:
   - secreto `SIGNPATH_API_TOKEN`;
   - variable `SIGNPATH_ORGANIZATION_ID`.
6. Crear la etiqueta de la versión siguiente (por ejemplo `v0.10.0`): esa ya sale firmada. La firma solo se hace
   en etiquetas y en `main`, y cada pedido de firma se aprueba a mano en SignPath.

### Datos para la solicitud (en inglés)

- **Project name:** Tisma Slicer
- **Repository:** https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-
- **Download page:** https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/releases
- **Code signing policy:** https://github.com/adriangrochi-code/Nuevo-slicer-basado-en-prusa-/blob/<rama principal>/docs/CODE_SIGNING_POLICY.md
- **License:** GNU AGPL v3 or later (OSI approved), no dual licensing.
- **Description:** Tisma Slicer is a free and open source 3D printing slicer for FFF printers, a fork of
  PrusaSlicer 2.9 (which publishes signed Windows releases). It adds non-planar slicing, structural and
  aerodynamic analysis of the parts, infill optimization, calibration tests, multiple plates, USB printing and
  LAN printing to Bambu Lab printers. The fork is clearly named Tisma Slicer in the binaries, installer and UI.
- **Build:** Windows binaries and the Inno Setup installer are built only by GitHub Actions
  (`.github/workflows/build_windows.yml`) from the public repository; no manual builds are signed.
- **What to sign:** `prusa-slicer.exe`, `prusa-slicer-console.exe`, `prusa-gcodeviewer.exe`, `PrusaSlicer*.dll`,
  `OCCTWrapper.dll` and `TismaSlicer-<version>-build<n>-setup.exe`. Third party DLLs are not signed.
- **Team:** single maintainer (committer, reviewer and approver), MFA enabled on GitHub and SignPath.

## Alternativa paga: certificado propio a tu nombre

Por ejemplo Certum «Open Source Code Signing in the Cloud», desde 49 € por año: el editor aparece como
«Open Source Developer, <tu nombre>». La clave queda en la nube de Certum (SimplySign) y cada sesión de firma se
habilita con un código del celular, así que es cómodo para firmar a mano en una PC con Windows, pero no se automatiza
bien en GitHub Actions.

Azure Artifact Signing (antes Trusted Signing) no sirve por ahora: para personas solo está disponible en Estados
Unidos y Canadá.
