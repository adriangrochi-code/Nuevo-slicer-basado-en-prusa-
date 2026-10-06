# Code signing policy / Política de firma de código

Free code signing provided by [SignPath.io](https://about.signpath.io), certificate by
[SignPath Foundation](https://signpath.org).

*(Firma de código gratuita de SignPath.io, certificado de SignPath Foundation; vigente cuando el proyecto sea aprobado.)*

## What is signed / Qué se firma

The Windows builds of Tisma Slicer produced by the GitHub Actions workflow `.github/workflows/build_windows.yml`
from this public repository, on the main branch and on release tags:

- `prusa-slicer.exe`, `prusa-slicer-console.exe`, `prusa-gcodeviewer.exe`, `PrusaSlicer*.dll`, `OCCTWrapper.dll`;
- the installer `TismaSlicer-<version>-build<n>-setup.exe`.

Third party libraries keep their own signatures. Nothing is signed outside of that automated build.

## Team roles / Roles

| Role | Members |
|---|---|
| Committers and reviewers | Repository owner ([@adriangrochi-code](https://github.com/adriangrochi-code)) |
| Approvers (approve each signing request) | Repository owner ([@adriangrochi-code](https://github.com/adriangrochi-code)) |

## Privacy policy / Privacidad

This program will not transfer any information to other networked systems unless specifically requested by the user
or the person installing or operating it. The optional network features (printer hosts, Printables, update checks of
the profiles inherited from PrusaSlicer) only connect when the user uses or enables them.

*(El programa no envía información a otros sistemas salvo que el usuario lo pida o lo active.)*
