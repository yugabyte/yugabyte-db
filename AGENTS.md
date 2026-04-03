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

### Build Prerequisites for Claude Code

Before building YugabyteDB in a Claude Code session, install the following dependencies:

- **CMake >= 3.31** — Ubuntu 24.04's default apt package is too old (3.28). Install via pip:
  ```bash
  pip3 install 'cmake>=3.31'
  ```
- **rsync**
- **gettext**
- **en_US.UTF-8 locale** — required by `initdb`; minimal containers often lack it

On Ubuntu/Debian:
```bash
sudo apt-get install -y rsync gettext
sudo locale-gen en_US.UTF-8
```

### Coding and Development

When working on DB code (`src/`), refer to `src/AGENTS.md` for build and test guidance
