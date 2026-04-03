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
