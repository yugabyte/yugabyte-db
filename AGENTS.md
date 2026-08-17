# AGENTS.md

This document provides a guide for agents working on YugabyteDB

### Deploying and running

For agents that want to deploy, configure and run YugabyteDB refer to instructions at ./docs/content/stable/quick-start

### Repo Structure

| Directory | What it contains |
|---|---|
| `src/` | Core database code: PostgreSQL fork (`src/postgres/`), YugabyteDB C++ storage engine (`src/yb/`), Odyssey connection pooler (`src/odyssey/`) |
| `java/` | Java client library, CDC connector, and DB tests |
| `managed/` | YugabyteDB Anywhere (YBA) platform — orchestration UI, CLI, node agent, and backend (Scala/Java) |
| `docs/` | Source files for the docs website (docs.yugabyte.com) |
| `python/` | Python build utilities and test infrastructure scripts |
| `build-support/` | Build system scripts, linting, and third-party dependency tooling |
| `cmake_modules/` | CMake modules for locating dependencies and custom build functions |
| `cloud/` | Docker, Kubernetes, and Grafana deployment configurations |
| `yugabyted-ui/` | Yugabyted web UI (React frontend + Go API server) |
| `architecture/` | Internal design documents and architecture specs |
| `troubleshoot/` | Troubleshooting framework backend and UI |

### Coding and Development

When working on DB code (`src/`), refer to `src/AGENTS.md` for build and test guidance

### Prose discipline — write for the reader, not for volume

AI-written text runs long: PR and diff descriptions that narrate the diff, comments that
restate the line below them, design docs padded with editorial framing. It reads as
thorough, but it costs review time, and every extra sentence is one more claim that has to
stay true as the code moves.

There are no hard limits here. Judge each piece by what a reader who already knows this
repo needs:

- **Code comments** — don't restate the code. Comment the *why* when it isn't obvious: an
  invariant, a cross-component contract, a locking or ordering constraint, a workaround and
  the upstream bug behind it. Not a label for the block below it, not narration of the
  change you just made.
- **PR / Phorge diff descriptions** — the motivation (a reviewer who doesn't know why is
  the expensive case), what changed, and whatever the reader must *act* on: new gflags,
  upgrade/rollback consequences, migration steps. Not a narration of the diff. The test
  plan is a separate section — keep it to what was run.
- **Design docs (`architecture/`) and agent docs (`AGENTS.md`, `.claude/skills/`)** — how
  the system is wired today, why it's built that way, and what the reader has to do. Cut
  restated context and the history of what the code used to do.
- **Commit messages** — subject, plus the why when the why isn't obvious.

The test for a sentence: would a reader who knows this repo do or believe anything
differently without it? If not, cut it.

**Motivation is not filler** — why a design is the way it is is exactly what a reader can't
recover from the code; what's being cut is text that says the same thing twice, not text
that explains. And don't over-correct: the goal is *fewer, load-bearing* words, not
stripped docs. Deleting a section that describes live behavior is a worse outcome than
leaving it wordy, and this applies to your own diff, not a cleanup tour of files you didn't
touch.

This does **not** apply to the user-facing docs website (`docs/`), which follows its own
editorial style guide and is written for readers who do *not* know this repo.

Trim before you publish, not in review: the `create-pr` and `create-diff` skills re-check
this as a step.
