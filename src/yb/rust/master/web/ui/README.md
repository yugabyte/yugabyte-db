# yb-master web UI

The Rust master's HTML pages: one React + TypeScript app built with Vite. The master serves its
`index.html` at every old C++ page URL (`/`, `/tables`, `/table?id=...`, `/masters`, ...; `PAGES`
and `HOST_PAGES` in `../src/ui.rs`) and the other built files under `/ui/`. The app picks the page
from the URL and reads only the master's JSON pages, `/api/v1/*`. Pages that stay server-rendered
(`/varz`, `/logs`, `/ObjectLockManager`, ...) are plain links.

It looks like yugabyted's UI: `src/styles.css` takes its palette, sizes and layout (a white sidebar
and header, white cards on a light grey page) from `yugabyted-ui/ui/src/theme`, without Material
UI, and `index.html` loads its font, Inter, from Google Fonts (offline, the system font is used).
The logo is `yugabyted-ui/ui/src/assets/yugabyteDB-logo.svg`.

## Build

```bash
./yb_build.sh release --build-master-ui ...   # builds into $BUILD_ROOT/master-ui, then embeds it
```

`build.sh` (what that runs) needs Node.js >= 22 on `PATH` or under `~/.nvm`. Built files are not
committed. By hand:

```bash
npm ci
npm run typecheck
npm run build                     # into dist/; `-- --outDir <dir>` for elsewhere
cd ../../.. && YB_MASTER_UI_DIR=$PWD/master/web/ui/dist cargo build -p master-web
```

## Develop against a running master

```bash
MASTER_URL=http://127.0.0.1:7000 npm run dev
```

The dev server serves the app at the page URLs and proxies `/api` and the server-rendered pages to
`MASTER_URL`.

## Where pages get data

| Page | JSON |
|---|---|
| `/` | `/api/v1/overview`, `/api/v1/masters`, `/api/v1/version` |
| `/tablet-servers` | `/api/v1/tablet-servers`, `/api/v1/overview`, `/api/v1/tablet-server-clocks` (YSQL lease), `/api/v1/cluster-config` (placement names on leaders without `overview.placements`) |
| `/tablet-server-clocks` | `/api/v1/tablet-server-clocks`, `/api/v1/tablet-servers` |
| `/tables`, `/table` | `/api/v1/tables`, `/api/v1/table` |
| `/namespaces` | `/api/v1/namespaces` |
| `/masters` | `/api/v1/masters` (any master answers it itself) |
| `/cluster-config`, `/xcluster` | `/api/v1/cluster-config`, `/api/v1/xcluster` |
| `/tasks` | `/api/v1/tasks` |
| `/tablet-replication` | `/api/v1/tablet-replication`, `/api/v1/tablet-under-replication` |
| `/load-distribution` | `/api/v1/load-distribution` |
| `/stateful-services` | `/api/v1/stateful-services`, `/api/v1/tablet-servers` |

`src/types.ts` has the shapes. Each section renders its own error, so a master that does not serve
an endpoint (a C++ leader, for the Rust-only ones) only blanks that section. Tables sort, filter
and page over the fetched arrays and render one page of rows, which keeps 1M tables or 5000 tablet
servers usable.

## Scale test

`scale-test/scale-test.mjs` serves `dist/` with synthetic JSON for 5000 tablet servers and 1M
tables (`TSERVERS`, `TABLES`), loads every page in headless Chromium (`CHROME`, default
Playwright's headless shell under `~/.cache/ms-playwright`), and fails when a page shows an error
or does not render within `LIMIT_SECS` (60). On `/tables` it also filters, clears and pages.

```bash
npm run build && npm run scale-test
```

Measured on an 8-core dev server: `/api/v1/tables` is 296 MB and `/api/v1/load-distribution` 325 MB
at that size; `/tables` rendered in 2.8 s, `/load-distribution` in 3.9 s, every other page in about
0.1 s, and filtering 1M tables took 0.26 s.
