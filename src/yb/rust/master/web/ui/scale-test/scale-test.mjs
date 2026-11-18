// Loads every page of the built UI (dist/) in headless Chromium against a fake master that answers
// the JSON pages for TSERVERS tablet servers and TABLES tables, and fails if a page does not
// render, shows an error, or takes longer than LIMIT_SECS. On /tables it also filters and pages.
//
//   npm run build && node scale-test/scale-test.mjs
//
// Env: TSERVERS (5000), TABLES (1000000), LIMIT_SECS (60), CHROME (headless Chromium binary;
// default: Playwright's headless shell under ~/.cache/ms-playwright).

import { spawn } from 'node:child_process';
import { existsSync, mkdtempSync, readdirSync, readFileSync, rmSync } from 'node:fs';
import { createServer } from 'node:http';
import { homedir, tmpdir } from 'node:os';
import { extname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const TSERVERS = Number(process.env.TSERVERS ?? 5000);
const TABLES = Number(process.env.TABLES ?? 1_000_000);
const LIMIT_MS = Number(process.env.LIMIT_SECS ?? 60) * 1000;
const DIST = fileURLToPath(new URL('../dist/', import.meta.url));
const RF = 3;

const hex = (n, width = 32) => n.toString(16).padStart(width, '0');
const tsUuid = (i) => hex(0xa0000000 + i);
const tableId = (i) => '0000' + hex(0x4000, 4) + '00003000800000' + hex(0x10000 + i, 10);
const tabletId = (i) => hex(0xb000000000 + i);

// The JSON pages, built once. Large ones are joined from parts; V8 caps a string at ~512 MB.
function buildPages() {
  const t0 = Date.now();
  const tservers = {};
  const live = {};
  for (let i = 0; i < TSERVERS; i++) {
    live[`10.${(i >> 16) & 255}.${(i >> 8) & 255}.${i & 255}:9000`] = {
      time_since_hb: '0.5s',
      time_since_hb_sec: 0.5,
      status: 'ALIVE',
      uptime_seconds: 86400 + i,
      ram_used: '1.00 GB',
      ram_used_bytes: 1e9,
      num_sst_files: 100,
      total_sst_file_size: '10.00 GB',
      total_sst_file_size_bytes: 1e10,
      uncompressed_sst_file_size: '20.00 GB',
      uncompressed_sst_file_size_bytes: 2e10,
      path_metrics: [{ path: '/mnt/d0', space_used: 1e10, total_space_size: 1e12 }],
      read_ops_per_sec: i % 100,
      write_ops_per_sec: i % 50,
      user_tablets_total: Math.round((TABLES * RF) / TSERVERS),
      user_tablets_leaders: Math.round(TABLES / TSERVERS),
      system_tablets_total: 10,
      system_tablets_leaders: 3,
      active_tablets: Math.round((TABLES * RF) / TSERVERS) + 10,
      cloud: 'cloud1',
      region: `region${i % 3}`,
      zone: `zone${i % 9}`,
      permanent_uuid: tsUuid(i),
    };
  }
  tservers[''] = live;
  const hostPort = (i) => `10.${(i >> 16) & 255}.${(i >> 8) & 255}.${i & 255}:9000`;

  const tableParts = [];
  const distParts = [];
  for (let i = 0; i < TABLES; i++) {
    const index = i % 1000 === 999;
    const row = {
      keyspace: `db${i % 100}`,
      table_name: index ? `t${i}_idx` : `t${i}`,
      state: 'Running',
      message: '',
      uuid: tableId(i),
      ysql_oid: 16384 + i,
      hidden: false,
      on_disk_size: {
        wal_files_size_bytes: 1024 * (i % 97),
        sst_files_size_bytes: 1048576 * (i % 89),
        uncompressed_sst_file_size_bytes: 2097152 * (i % 89),
        has_missing_size: false,
      },
    };
    tableParts.push((index ? 'i' : 'u') + JSON.stringify(row));
    const counts = [];
    for (let r = 0; r < RF; r++) {
      counts.push({ uuid: tsUuid((i * RF + r) % TSERVERS), replicas: 1, leaders: r === 0 ? 1 : 0 });
    }
    distParts.push(
      JSON.stringify({
        table_id: tableId(i),
        keyspace: row.keyspace,
        table_name: row.table_name,
        tablet_count: 1,
        counts,
      }),
    );
  }
  const user = tableParts.filter((p) => p[0] === 'u').map((p) => p.slice(1));
  const index = tableParts.filter((p) => p[0] === 'i').map((p) => p.slice(1));
  const tables =
    `{"user":[${user.join(',')}],"index":[${index.join(',')}],"parent":[],"system":[]}`;

  const distTservers = [];
  const clocks = [];
  for (let i = 0; i < TSERVERS; i++) {
    distTservers.push({
      uuid: tsUuid(i),
      host_port: hostPort(i),
      cloud: 'cloud1',
      region: `region${i % 3}`,
      zone: `zone${i % 9}`,
      placement_uuid: '',
      alive: true,
    });
    clocks.push({
      uuid: tsUuid(i),
      host_port: hostPort(i),
      time_since_hb_sec: 0.5,
      physical_time_us: 1.7e15,
      hybrid_time: '1700000000000000',
      ysql_lease: { state: 'LIVE', epoch: 1, seconds: 10 },
    });
  }
  const loadDistribution =
    '{"ongoing_remote_bootstraps":[],"warnings_summary":[],' +
    '"tasks_summary":[{"example_description":"Stepdown Leader RPC","state":"kComplete",' +
    '"count":12,"example_status":"OK"}],"tablet_distribution":{"tservers":' +
    JSON.stringify(distTservers) +
    `,"tables":[${distParts.join(',')}]}}`;

  const namespaces = { 'User Namespaces': [], 'System Namespaces': [] };
  for (let i = 0; i < 100; i++) {
    namespaces['User Namespaces'].push({
      name: `db${i}`,
      id: '0000' + hex(0x4000 + i, 4) + '000030008000000000000000',
      language: 'ysql',
      state: 'RUNNING',
      colocated: 'false',
    });
  }

  const table = {
    table_name: 'db0.t0',
    table_id: tableId(0),
    table_version: 0,
    table_type: 'PGSQL_TABLE_TYPE',
    table_state: 'Running',
    table_state_message: '',
    table_replication_info: { live_replicas: { num_replicas: RF } },
    columns: [{ column: 'k', id: '0', type: 'int32 NOT NULL HASH' }],
    tablets: [
      {
        tablet_id: tabletId(0),
        partition: 'hash_split: [0x0000, 0xFFFF]',
        split_depth: 0,
        state: 'Running',
        hidden: 'false',
        message: '',
        locations: [0, 1, 2].map((r) => ({
          uuid: tsUuid(r),
          role: r === 0 ? 'LEADER' : 'FOLLOWER',
          location: `http://${hostPort(r)}/tablet?id=${tabletId(0)}`,
        })),
      },
    ],
    Tasks: [],
  };

  const pages = {
    '/api/v1/tablet-servers': JSON.stringify(tservers),
    '/api/v1/tables': tables,
    '/api/v1/load-distribution': loadDistribution,
    '/api/v1/tablet-server-clocks': JSON.stringify({ tservers: clocks }),
    '/api/v1/overview': JSON.stringify({
      universe_uuid: hex(1),
      cluster_uuid: hex(2),
      cluster_config_version: 3,
      replication_factor: RF,
      live_tservers: TSERVERS,
      dead_tservers: 0,
      load_balancer_enabled: true,
      is_load_balanced: true,
      encryption_at_rest: 'Disabled',
      tablet_limits: { tablet_peer_limit: null, tablet_peers: TABLES * RF, enforced: false },
      placements: [
        {
          placement_uuid: '',
          is_primary: true,
          live_tservers: TSERVERS,
          blacklisted_tservers: 0,
          dead_tservers: 0,
          user_tablet_peers: TABLES * RF,
          system_tablet_peers: 30,
          hidden_tablet_peers: 0,
          active_tablet_peers: TABLES * RF + 30,
          tablet_peer_limit: null,
        },
      ],
    }),
    '/api/v1/namespaces': JSON.stringify(namespaces),
    '/api/v1/table': JSON.stringify(table),
    '/api/v1/cluster-config': JSON.stringify({ version: 3, cluster_uuid: hex(2) }),
    '/api/v1/xcluster': JSON.stringify({ xcluster_config: {}, outbound_replication_groups: [] }),
    '/api/v1/tasks': JSON.stringify({ running: [{ name: 'cluster-balancer', count: 1 }] }),
    '/api/v1/tablet-replication': '{"leaderless_tablets":[]}',
    '/api/v1/tablet-under-replication': '{"underreplicated_tablets":[]}',
    '/api/v1/stateful-services': '{"stateful_services":[]}',
    '/api/v1/masters': JSON.stringify({
      masters: [
        {
          instance_id: { permanent_uuid: hex(9), instance_seqno: 1 },
          registration: { http_addresses: [{ host: '127.0.0.1', port: 7000 }] },
          role: 'LEADER',
        },
      ],
    }),
    '/api/v1/version': JSON.stringify({ version_number: '2.29.0.0', build_number: '1' }),
    '/api/v1/health-check': '{"dead_nodes":[],"under_replicated_tablets":[]}',
  };
  const sizes = Object.fromEntries(
    Object.entries(pages).map(([k, v]) => [k, `${(Buffer.byteLength(v) / 1e6).toFixed(1)} MB`]),
  );
  console.log(`Built the JSON pages in ${Date.now() - t0} ms:`, sizes);
  return Object.fromEntries(Object.entries(pages).map(([k, v]) => [k, Buffer.from(v)]));
}

const TYPES = {
  '.js': 'text/javascript',
  '.css': 'text/css',
  '.html': 'text/html',
  '.svg': 'image/svg+xml',
};

function startServer(pages) {
  const index = readFileSync(join(DIST, 'index.html'));
  const server = createServer((req, res) => {
    const path = new URL(req.url, 'http://x').pathname;
    if (pages[path]) {
      res.writeHead(200, { 'Content-Type': 'text/plain' });
      res.end(pages[path]);
    } else if (path.startsWith('/ui/') && existsSync(join(DIST, path.slice(4)))) {
      res.writeHead(200, { 'Content-Type': TYPES[extname(path)] ?? 'application/octet-stream' });
      res.end(readFileSync(join(DIST, path.slice(4))));
    } else if (path.startsWith('/api/')) {
      res.writeHead(404);
      res.end('not served');
    } else {
      res.writeHead(200, { 'Content-Type': 'text/html' });
      res.end(index);
    }
  });
  return new Promise((resolve) => server.listen(0, '127.0.0.1', () => resolve(server)));
}

function findChrome() {
  if (process.env.CHROME) return process.env.CHROME;
  const cache = join(homedir(), '.cache', 'ms-playwright');
  for (const dir of existsSync(cache) ? readdirSync(cache).sort().reverse() : []) {
    for (const bin of ['chrome-linux/headless_shell', 'chrome-linux/chrome']) {
      if (existsSync(join(cache, dir, bin))) return join(cache, dir, bin);
    }
  }
  throw new Error('No headless Chromium found; set CHROME');
}

// A minimal Chrome DevTools Protocol client for one page.
async function openPage(chrome) {
  const userDir = mkdtempSync(join(tmpdir(), 'yb-ui-scale-'));
  const proc = spawn(
    chrome,
    ['--headless', '--no-sandbox', '--disable-gpu', `--user-data-dir=${userDir}`,
     '--remote-debugging-port=0', '--js-flags=--max-old-space-size=8192', 'about:blank'],
    { stdio: ['ignore', 'ignore', 'pipe'] },
  );
  const wsUrl = await new Promise((resolve, reject) => {
    let err = '';
    proc.stderr.on('data', (d) => {
      err += d;
      const m = err.match(/DevTools listening on (ws:\/\/\S+)/);
      if (m) resolve(m[1]);
    });
    proc.on('exit', (code) => reject(new Error(`Chromium exited ${code}: ${err}`)));
  });
  const port = new URL(wsUrl).port;
  const targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
  const pageTarget = targets.find((t) => t.type === 'page');
  const ws = new WebSocket(pageTarget.webSocketDebuggerUrl);
  await new Promise((resolve, reject) => {
    ws.onopen = resolve;
    ws.onerror = reject;
  });
  let nextId = 1;
  const pending = new Map();
  const errors = [];
  ws.onmessage = (event) => {
    const msg = JSON.parse(event.data);
    if (msg.id && pending.has(msg.id)) {
      const { resolve, reject } = pending.get(msg.id);
      pending.delete(msg.id);
      if (msg.error) reject(new Error(JSON.stringify(msg.error)));
      else resolve(msg.result);
    } else if (msg.method === 'Runtime.exceptionThrown') {
      const details = msg.params.exceptionDetails;
      errors.push(details.exception?.description ?? details.text);
    } else if (msg.method === 'Runtime.consoleAPICalled' && msg.params.type === 'error') {
      errors.push(msg.params.args.map((a) => a.value ?? a.description).join(' '));
    }
  };
  const send = (method, params = {}) =>
    new Promise((resolve, reject) => {
      const id = nextId++;
      pending.set(id, { resolve, reject });
      ws.send(JSON.stringify({ id, method, params }));
    });
  await send('Runtime.enable');
  await send('Page.enable');
  await send('Performance.enable');
  const evaluate = async (expression) =>
    (await send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true }))
      .result.value;
  const close = () => {
    ws.close();
    proc.kill('SIGKILL');
    rmSync(userDir, { recursive: true, force: true });
  };
  return { send, evaluate, errors, close };
}

async function waitFor(page, expression, what, start = Date.now()) {
  for (;;) {
    const value = await page.evaluate(expression);
    if (value) return Date.now() - start;
    if (Date.now() - start > LIMIT_MS) throw new Error(`Timed out after ${LIMIT_MS} ms: ${what}`);
    await new Promise((r) => setTimeout(r, 100));
  }
}

// Rendered: nothing is loading (each page also names an element it must show).
const RENDERED = `!document.querySelector('.loading')`;

const PAGES = [
  ['/', 'tbody tr'],
  ['/tablet-servers', '.pager'],
  ['/tablet-server-clocks', '.pager'],
  ['/tables', '.pager'],
  ['/table?id=' + tableId(0), 'tbody tr'],
  ['/namespaces', 'tbody tr'],
  ['/load-distribution', 'tbody tr'],
  ['/tablet-replication', 'section'],
  ['/tasks', 'tbody tr'],
  ['/cluster-config', 'section'],
  ['/xcluster', 'section'],
  ['/stateful-services', 'section'],
];

async function main() {
  if (!existsSync(join(DIST, 'index.html'))) throw new Error(`Build the UI first: ${DIST}`);
  const pages = buildPages();
  const server = await startServer(pages);
  const base = `http://127.0.0.1:${server.address().port}`;
  const page = await openPage(findChrome());
  const results = [];
  let failed = false;
  try {
    for (const [path, selector] of PAGES) {
      page.errors.length = 0;
      const start = Date.now();
      await page.send('Page.navigate', { url: base + path });
      let ms;
      try {
        ms = await waitFor(
          page,
          `${RENDERED} && !!document.querySelector(${JSON.stringify(selector)})`,
          path,
          start,
        );
      } catch (e) {
        failed = true;
        const text = await page.evaluate(`document.body.innerText.slice(0, 300)`);
        results.push({ page: path, ms: null, error: `${e.message}; page shows: ${text}` });
        continue;
      }
      const shownErrors = await page.evaluate(
        `[...document.querySelectorAll('.alert.error')].map(e => e.textContent)`,
      );
      const metrics = (await page.send('Performance.getMetrics')).metrics;
      const heap = metrics.find((m) => m.name === 'JSHeapUsedSize')?.value ?? 0;
      const errors = [...shownErrors, ...page.errors];
      if (errors.length) failed = true;
      results.push({ page: path, ms, heap_mb: Math.round(heap / 1e6), errors });
    }

    // /tables: filter to one table, clear it, and page forward, each within the limit.
    try {
      results.push(await tablesInteractions(page, base));
    } catch (e) {
      failed = true;
      results.push({ page: '/tables interactions', error: e.message });
    }
  } finally {
    page.close();
    server.close();
  }
  console.table(results);
  if (failed) {
    console.error(JSON.stringify(results.filter((r) => r.error || r.errors?.length), null, 2));
    process.exit(1);
  }
  console.log(
    `All pages rendered within ${LIMIT_MS / 1000} s at ${TSERVERS} tservers and ${TABLES} tables.`,
  );
}

async function tablesInteractions(page, base) {
  {
    await page.send('Page.navigate', { url: base + '/tables' });
    await waitFor(page, `${RENDERED} && !!document.querySelector('.pager')`, '/tables');
    const setFilter = (value) => `(() => {
      const input = document.querySelector('input.filter');
      const setter = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set;
      setter.call(input, ${JSON.stringify(value)});
      input.dispatchEvent(new Event('input', { bubbles: true }));
      return true;
    })()`;
    const pagerText = `(document.querySelector('.pager')?.textContent ?? '')`;
    let start = Date.now();
    await page.evaluate(setFilter(`t${TABLES - 2}`));
    const filterMs = await waitFor(page, `${pagerText}.includes('filtered from')`, 'filter', start);
    start = Date.now();
    await page.evaluate(setFilter(''));
    const clearMs = await waitFor(page, `!${pagerText}.includes('filtered from')`, 'clear', start);
    start = Date.now();
    await page.evaluate(
      `[...document.querySelectorAll('.pager button')].find(b => b.title === 'Next page').click()`,
    );
    const nextMs = await waitFor(page, `${pagerText}.includes('page 2 /')`, 'next page', start);
    return {
      page: '/tables interactions',
      filter_ms: filterMs,
      clear_ms: clearMs,
      next_page_ms: nextMs,
    };
  }
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
