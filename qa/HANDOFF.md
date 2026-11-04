# Spec and QA session: handoff

Read this first in a new session of the spec and QA side. Keep it current. A session that passes about 500 000 tokens
of context writes it up to date, then hands over to a fresh session. The owner authorized this on 2026-11-04.

## Role

- This session writes the feature specs ("fichas") of the clean-room engine `adriangrochi-code/tisma_engine` in
  `docs/spec/features/`, on branch `spec/next`. The PRs go to `dev`; the owner merges them.
- It does black-box QA of the engine, and later of the Android app.
- It maintains the proprietary Tisma Cloud server in this repo (`server/`), on branch `ccr-7e2013d9-qwmclz`.
- **Never read or write the engine's `src/` or `apps/`.** This side has read PrusaSlicer and Cura code (clean room,
  `CLEANROOM.md` of the engine).
- **Never create sessions without the owner's authorization.** It is authorized to renew a session past the limit,
  with a handoff, archiving the old one.
- Answer the owner in Spanish. Code, comments, commit messages and PR titles in English. No model names in code or
  commits.

## Sessions

| Session | Role |
|---|---|
| `session_01JDAJxTVh345Lfm4794rJF8` | Engine (tisma_engine, branch dev) |
| `session_01QadJfKGJU2WtuAunZW5p9B` | Android app (tisma_engine `apps/android`, branch `android`) |
| `session_015csYE6YXftDTgfC23i81xG` | This one: specs, QA, server |

## Routine

One scheduled task, «Tisma: control combinado cada 3 h» (`trig_017Majyx7RbJpqLTBoSwTDgu`), runs every three hours.
If `dev` has not changed since `last_checked.txt`, it writes one line and stops. Otherwise it tests only what changed.
The 7:37 firing leaves the owner a morning summary. The old routines are disabled.

## Context and token rules (owner, 2026-11-04)

- State lives in files: this file and `qa/*.json`. Do not carry it in the conversation.
- Show only the end or a summary of command output; read large files in parts.
- Crop and shrink screenshots; prefer `--dump-ui` text.
- When checking another session, read its status and its last message only.
- Short messages between sessions: what changed, the commit, how to try it.
- Message the owner only for a milestone, a decision, something broken, a new or fixed bug, or the morning summary.

## Tools (in `qa/`; copy to the scratchpad to run)

- `hunt.py N seed`: random slices from the keys in `keys.json`, which `keys.py` builds from the engine's
  `docs/CONFIG.md`. Models come from `mkmodels.py`. It flags crashes, hangs, NaN, negative Z and missing extrusion.
  Flavors `machinekit` and `no_extrusion` are false positives.
- `edges.py`: every numeric key alone, at its minimum and its maximum.
- `p10.py`, `p10b.py`: top and bottom surface patterns against spec P10.
- `zzm.py`, `vz.py`: zigzag infill heights and Z speed.
- `ui/t50.sh "<xdotool steps>" <name>`: starts the interface on Xvfb :77, at 1440×900, with a fresh HOME, opens
  Ingeniería › Estructural, runs the steps and takes a screenshot.
- `perf/meas.py <binary> <s80|s200> <preview|prepare> [layer]` and `perf/baseline.md`.
- Engine build: clone `tisma_engine` into `/home/user/tisma_engine`, then
  `cmake -G Ninja -DCMAKE_BUILD_TYPE=Release` in `<scratchpad>/eb`. Packages: `libssl-dev libx11-dev libxrandr-dev
  libxinerama-dev libxcursor-dev libxi-dev libgl-dev glslang-tools mesa-vulkan-drivers xdotool imagemagick`.
- Cloud end to end: master `tisma-cloud master --port 8765 --env .env` with sqlite. Plan, user and node come from
  `python -m tisma_cloud.admin`, with `TISMA_DATABASE_URL` exported. The node runs `tisma-cloud node --config
  node.toml` with `tisma_engine` set.
- Never use `pkill -f` with a pattern that the shell's own command line matches: it kills the shell. Kill by pid.

## Known findings

See `known.json`, `ui/known.json` and `i18n/known.json`. Highest ids: BH24, UI50, TR29.

## Open items (2026-11-04)

- Engine: `orient` (cloud rule 19c), then Design D4. BH24 (extreme quality capped) and the aero resolution check are
  still to push. When the resolution check lands, raise the server's aero estimates by 20 %.
- Cloud end to end: slice, fea and aero pass. Orient is missing. The owner's real server needs the Cloudflare tunnel
  (`server/deploy/cloudflare-tunnel-windows.md`).
- Owner decisions:
  - whether to write the woven walls spec, with the patent warning;
  - PR #53 of the engine (`dev` → `alpha`);
  - real prints: Kobra 3 cube with and without the ACE Pro, zigzag tensile specimens, M8 screw and gears before
    15 Nov.
- Ctrl+A in the save dialog's name field does not select all (seen 2026-11-03, not yet reported).
