# Tisma Slicer — notes for Claude

Tisma Slicer is a fork of PrusaSlicer 2.9.6 (C++17, CMake, wxWidgets, AGPLv3) with non-planar layers, structural
analysis (FEA), resin (SLA) formats and its own GUI design. Windows is the first platform; Linux and macOS are built
by CI too.

## Working rules (from the project owner)

- Answer the owner in Spanish. Code, comments, commit messages and PR titles are in English.
- Audit before modifying: read the code involved and check what exists before writing new code.
- No large refactors without explicit authorization. An authorized refactor goes in a single `refactor:` commit.
- Do not invent APIs, file formats or hardware specifications: use official sources and say where data comes from.
- Check the license before adding a dependency or copying code (AGPLv3 compatible); record it in
  `docs/DEPENDENCIES_AND_LICENSES.md`.
- Build, test, then document. Never report work as done without running the build and the relevant tests (and the
  GUI when the change is visible); say plainly what was not tested.
- Keep the original line endings of edited files (some use CRLF; Python rewrites must preserve them).
- No model identifiers (model ids or version names) in code, commit subjects and bodies, or PR text. The only
  exception is the attribution trailer the session requires at the end of commits and PRs.

## Development flow

Follow `CONTRIBUTING.md` and the `tisma-dev-workflow` skill: branch per task, Conventional Commits, pull request with
CI and code review, squash merge; release-please makes the releases; nightly builds from `main`.

## Build and test (Linux)

```
build-utils/build_linux_system_libs.sh             # system libraries (fast local builds)
cmake --build build --target TismaSlicer           # build/src/tisma-slicer
ctest --test-dir build --output-on-failure -E libseqarrange_tests   # libseqarrange_tests takes > 2 h
```

GUI checks run under `Xvfb :99` with a scratch `--datadir`. The executable is `tisma-slicer` (`tisma-gcodeviewer`
is a link that starts the G-code viewer).

## Names that stay "Prusa" on purpose

`SLIC3R_APP_KEY` ("PrusaSlicer": 3MF metadata read by other slicers, translation catalogs), the `prusaslicer://` URL
scheme (Printables), the Prusa vendor profiles, PrusaLink/Connect, the `Slic3r` namespace and the copyright notices.
