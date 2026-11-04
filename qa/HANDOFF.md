# Spec and QA session: handoff

Read this first in a new session of the spec and QA side. Keep it current. A session that passes about 500 000 tokens
of context no longer hands over: no session is renewed, this one included (owner, 2026-11-04, after one renewal).

## Role

- This session writes the feature specs ("fichas") of the clean-room engine `adriangrochi-code/tisma_engine` in
  `docs/spec/features/`, on branch `spec/next`. The PRs go to `dev`; the owner merges them.
- It does black-box QA of the engine, and later of the Android app.
- The server (`server/`) belongs to the server session since 2026-11-04 (branch `server/next`); this session tests it
  end to end and reports its bugs there.
- Push access to `tisma_engine` granted by the owner on 2026-11-04, only for `spec/next` and only with the clean room
  respected (add it with `add_repo` in a new session).
- **Never read or write the engine's `src/` or `apps/`.** This side has read PrusaSlicer and Cura code (clean room,
  `CLEANROOM.md` of the engine).
- **Never create sessions without the owner's authorization.** No session is renewed (owner, 2026-11-04).
- Answer the owner in Spanish. Code, comments, commit messages and PR titles in English. No model names in code or
  commits.

## Sessions

| Session | Role |
|---|---|
| `session_01JDAJxTVh345Lfm4794rJF8` | Engine (tisma_engine, branch dev). No renewal (engine CLAUDE.md, b77b454) |
| `session_01QadJfKGJU2WtuAunZW5p9B` | Android app (tisma_engine `apps/android`, branch `android`) |
| `session_01QadJfL79pTm8Ks4houx1oK` | Tisma Cloud server (`server/`, branch `server/next`), created 2026-11-04 with the owner's authorization. Like the engine and Android sessions, it may read all of Tisma's own code (engine `src/` and `apps/` included) but never PrusaSlicer or Cura code (here only `server/`, `qa/`, docs); it never writes to `tisma_engine`. Only this spec/QA session may not read the engine's code (owner, 2026-11-04) |
| `session_01QadJfL7KViJEaJL92p8Swb` | This one: specs, QA, server (renewed 2026-11-04 from `session_015csYE6YXftDTgfC23i81xG`, now archived) |

## Routine

One scheduled task, «Tisma: control combinado cada 3 h» (`trig_01L2ps35vnEbjWRzEkxsjNbf`, bound to this session), runs every three hours.
If `dev` has not changed since `last_checked.txt`, it writes one line and stops. Otherwise it tests only what changed.
The 7:37 firing leaves the owner a morning summary. The old routines are disabled.

## Context and token rules (owner, 2026-11-04)

- State lives in files: this file and `qa/*.json`. Do not carry it in the conversation.
- Show only the end or a summary of command output; read large files in parts.
- Crop and shrink screenshots; prefer `--dump-ui` text.
- When checking another session, read its status and its last message only.
- Short messages between sessions: what changed, the commit, how to try it.
- Message the owner only for a milestone, a decision, something broken, a new or fixed bug, or the morning summary.

## Way of working (owner, 2026-11-04)

- Follow the January roadmap, with no detours. The engine works in this order:
  1. the cloud (`orient`);
  2. Ingeniería (EP1 to EP10, a faster CFD);
  3. Design D4, then D5b;
  4. the rest of PLAN.md.
- When a session is unsure, it picks what the roadmap and the specs say, writes the decision down and keeps going.
- Dates (owner, 2026-11-04): closed beta in December 2026 with what is ready, used to debug: every bug seen (the
  `known.json` lists and the beta's reports) gets fixed; hardware checks run alongside, without blocking the software.
  Version 1.0 in January 2027 (second week).
- The owner's real prints no longer block anything. They stay marked as not tested on hardware.

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

See `known.json`, `ui/known.json` and `i18n/known.json`. Highest ids: BH26, UI52, TR29, AND3 (`android/known.json`).

## Open items (2026-11-04)

- Engine: `orient` (cloud rule 19c), BH24 and the aero resolution check are on `dev` at 832341d. Next: Design D4.
  The server's aero estimates are raised by 20 % for the resolution check (`AERO_RESOLUTION_CHECK_FACTOR`).
- Cloud end to end on engine 8930804 (2026-11-04): slice, aero friction and fast (cd_low/cd_high present), orient
  FDM (OR1-like T: upside down, cost -900) and resin (least_peel) pass; the cloud result equals the local one. fea
  without conditions fails cleanly (needs_conditions, Spanish text); a fea success needs a project saved by the
  interface. aero fast took 442 s with a second job on the node (estimate 360 s, order only). The owner's real server
  needs the Cloudflare tunnel (`server/deploy/cloudflare-tunnel-windows.md`).
- Inside-out FDM part oriented wrong: fixed in engine 5326a49, checked (upside down, bed area 900).
- Android black box (owner authorized 2026-11-04): branch `android` 7dfe406 built with `./gradlew :app:assembleDebug`
  (no source read), API 30 x86_64 emulator without KVM (`-no-accel`, boots in about 6 min; SDK in the scratchpad),
  real master+node at `http://10.0.2.2:8765`. Sign in and account pass. AND1: "Send to the cloud" sends nothing
  (blocks). AND2: an unlimited account shows hours left. Both sent to the Android session. In the emulator, taps in
  the system file picker do not register: pick with DPAD and Enter.
- Owner decisions:
  - whether to write the woven walls spec, with the patent warning;
  - PR #53 of the engine (`dev` → `alpha`);
  - real prints: Kobra 3 cube with and without the ACE Pro, zigzag tensile specimens, M8 screw and gears before
    15 Nov.
- Ctrl+A in the save dialog's name field does not select all (seen 2026-11-03, not yet reported).
