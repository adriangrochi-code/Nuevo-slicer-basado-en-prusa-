# Contributing to Tisma Slicer

## Branches and pull requests

- `main` is the integration branch: it always builds, and every push to it is a candidate for the nightly build.
- Work happens on a short branch per task (`feat/resin-exposure-tower`, `fix/goo-header`, ...) and reaches `main`
  through a pull request. Nothing is pushed to `main` directly.
- Every pull request runs **CI** (`.github/workflows/ci.yml`): Windows, Linux (with the unit tests) and macOS builds.
  The packages of the run stay 14 days as artifacts, for testing the branch before merging.
- Every pull request gets a code review before merging (for Claude sessions: the `/code-review` skill, findings posted
  on the pull request and fixed or answered).
- Merge when CI is green and the review has no open findings:
  - **Squash and merge** (default): the pull request title becomes the commit on `main`.
  - **Rebase and merge** only when the pull request has several commits that must stay separate (for example a
    refactor commit followed by the fixes that build on it).

## Conventional Commits

Commit messages and pull request titles follow [Conventional Commits 1.0](https://www.conventionalcommits.org/),
in English:

```
<type>(<optional scope>): <description in the imperative, lower case, no final period>

<body: what and why, wrapped at 72 columns>

<footers: BREAKING CHANGE: ..., Refs #12, Co-Authored-By: ...>
```

| Type | Use | Version bump (while 0.x) |
| --- | --- | --- |
| `feat` | new user-visible feature | minor |
| `fix` | bug fix | patch |
| `perf` | faster or lighter, same behavior | patch |
| `refactor` | code change without behavior change | none |
| `docs`, `test`, `build`, `ci`, `chore` | documentation, tests, build system, CI, maintenance | none |
| `revert` | reverts a previous commit | depends |

`feat!:` or a `BREAKING CHANGE:` footer marks an incompatible change (a minor bump while 0.x, then a major one).
Common scopes: `resin`, `fea`, `nonplanar`, `gui`, `gcode`, `profiles`, `calibration`, `build`, `ci`.

The `PR title` check (`.github/workflows/pr-title.yml`) rejects titles that are not Conventional Commits.

**Refactors go in a single commit** with a `refactor:` type and nothing else in it (no behavior changes, no fixes),
so it can be reviewed, reverted or skipped when comparing with upstream as one unit. Example:
`refactor: rebrand PrusaSlicer and NonPlanarSlicer names to Tisma`.

## Versions and releases

- The version is `TISMA_VERSION` in `version.inc` ([semantic versioning](https://semver.org/)). `SLIC3R_VERSION` is
  the PrusaSlicer version the fork is based on and does not change with Tisma releases.
- **release-please** (`.github/workflows/release-please.yml`) keeps an open *release pull request* that bumps the
  version and writes the `CHANGELOG.md` section from the Conventional Commits merged since the last release.
  Merging it tags `vX.Y.Z`, creates the GitHub release and attaches the Windows installer and zip, the Linux AppImage,
  the macOS disk images (Apple silicon and Intel) and `SHA256SUMS.txt` (`.github/workflows/release.yml`).
- **Nightly**: every night, when `main` changed, `.github/workflows/nightly.yml` replaces the `nightly` pre-release
  with fresh packages of the three platforms. They are development builds, not tested on printers.
- Windows packages of `main` and of tags are code signed through SignPath when it is set up
  (`docs/CODE_SIGNING_POLICY.md`); macOS packages are signed ad hoc only (not notarized).

## Before opening a pull request

- Build and run the relevant tests locally (`ctest --test-dir build -R <suite>`), and the app when the change is
  visible in the GUI.
- Keep the original line endings of the files you edit (some files use CRLF).
- Update `docs/` when behavior changes, and the Spanish translation (`resources/localization/es/`) for new strings.
- New dependencies: check that the license is compatible with the AGPLv3 and record it in
  `docs/DEPENDENCIES_AND_LICENSES.md`.

## License

Tisma Slicer is a fork of PrusaSlicer and is released under the GNU Affero General Public License v3 or later.
Contributions are accepted under the same license.
