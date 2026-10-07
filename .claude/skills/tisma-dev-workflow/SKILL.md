---
name: tisma-dev-workflow
description: Use when starting, committing, opening a pull request, reviewing, merging or releasing work in Tisma Slicer - branch per task, Conventional Commits in English, pull request with CI on Windows/Linux/macOS and a code review, squash merge, release-please releases and nightly builds
---

# Tisma Slicer development workflow

The full policy is in `CONTRIBUTING.md`; this is the checklist for a Claude session.

## 1. Start

- Audit first: read the code involved, `git log` for related work, and the docs in `docs/`.
- Work on a branch for the task. In a cloud session the branch may be assigned by the session: use it.
- Large refactors only with explicit authorization from the owner.

## 2. Commit

- Conventional Commits, in English: `type(scope): imperative description` (types: feat, fix, perf, refactor, docs,
  test, build, ci, chore, revert). Body: what and why, wrapped at 72 columns, and how it was verified.
- One logical change per commit. A refactor is a single `refactor:` commit with no behavior change inside.
- `feat!:` / `BREAKING CHANGE:` footer for incompatible changes (profiles, 3MF, config keys).
- Keep each file's line endings; never commit build output or downloaded tools.
- End with the attribution lines the session asks for. No model identifiers.

## 3. Verify before pushing

Use `verification-before-completion`. At least: build `TismaSlicer`, run the suites the change touches
(`ctest --test-dir build -R <suite>`, never `libseqarrange_tests`: > 2 h), and check the GUI under Xvfb when the change
is visible. For CI failures use `systematic-debugging` and reproduce locally when possible.

## 4. Pull request

- Title = the Conventional Commit that will land on `main` (checked by `pr-title.yml`). Body from
  `.github/pull_request_template.md`: summary, how it was tested, what was not tested.
- Wait for CI (`ci.yml`: Windows, Linux with tests, macOS). A red check is work, never "flaky" without evidence.
- Review: run the `/code-review` skill on the PR diff, post the findings on the PR, fix them in new commits (or answer
  why not), and repeat until no blocking findings remain.
- Merge only when CI is green and the review is clean: **squash** by default; **rebase** when the commits must stay
  separate (a refactor commit). Merging needs the owner's go-ahead unless they said otherwise.

## 5. Releases and nightly

- Never edit `TISMA_VERSION` by hand: release-please opens a release pull request (version bump + `CHANGELOG.md`).
  Before merging it, check the CHANGELOG section reads well (add context from the PRs if needed).
- Merging the release PR tags `vX.Y.Z` and `release.yml` attaches the packages of the three platforms.
- `nightly.yml` publishes `main` every night as the `nightly` pre-release when it changed.
- After a release: confirm the assets are there and that the Windows installer smoke test passed.
