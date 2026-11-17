import { useDeferredValue, useMemo, useState } from 'react';
import { useJson } from '../api';
import { Badge, Load, Mono, Section } from '../components/Common';
import { DataTable, Pager } from '../components/DataTable';
import { formatNumber, serverUrl, unescapeHtml } from '../format';
import { Link, tableHref } from '../router';
import { LoadDistributionJson } from '../types';

type Distribution = NonNullable<LoadDistributionJson['tablet_distribution']>;

const TABLES_PER_PAGE = 50;
const TSERVERS_PER_PAGE = 20;

// Tables x tservers, paged both ways: 1M tables or 5000 tservers still render one small grid.
function Matrix({ dist }: { dist: Distribution }) {
  const tservers = dist.tservers ?? [];
  const tables = dist.tables ?? [];
  const [needle, setNeedle] = useState('');
  const deferred = useDeferredValue(needle.trim().toLowerCase());
  const [rowPage, setRowPage] = useState(0);
  const [colPage, setColPage] = useState(0);

  const totals = useMemo(() => {
    const t = new Map<string, { replicas: number; leaders: number }>();
    for (const table of tables) {
      for (const c of table.counts ?? []) {
        const e = t.get(c.uuid) ?? { replicas: 0, leaders: 0 };
        e.replicas += c.replicas ?? 0;
        e.leaders += c.leaders ?? 0;
        t.set(c.uuid, e);
      }
    }
    return t;
  }, [tables]);

  const filtered = useMemo(
    () =>
      deferred
        ? tables.filter(
            (t) =>
              unescapeHtml(t.table_name).toLowerCase().includes(deferred) ||
              unescapeHtml(t.keyspace).toLowerCase().includes(deferred) ||
              t.table_id.toLowerCase().includes(deferred),
          )
        : tables,
    [tables, deferred],
  );
  const rowPages = Math.max(1, Math.ceil(filtered.length / TABLES_PER_PAGE));
  const colPages = Math.max(1, Math.ceil(tservers.length / TSERVERS_PER_PAGE));
  const rp = Math.min(rowPage, rowPages - 1);
  const cp = Math.min(colPage, colPages - 1);
  const rows = filtered.slice(rp * TABLES_PER_PAGE, (rp + 1) * TABLES_PER_PAGE);
  const cols = tservers.slice(cp * TSERVERS_PER_PAGE, (cp + 1) * TSERVERS_PER_PAGE);

  return (
    <div className="data-table">
      <div className="toolbar">
        <input
          type="search"
          className="filter"
          placeholder="Filter tables by name, keyspace or id"
          value={needle}
          onChange={(e) => {
            setNeedle(e.target.value);
            setRowPage(0);
          }}
        />
        <span className="muted">Each cell: replicas (leaders).</span>
      </div>
      {colPages > 1 && (
        <Pager total={tservers.length} page={cp} pages={colPages} pageSize={TSERVERS_PER_PAGE} setPage={setColPage} noun="tablet servers" />
      )}
      <div className="table-scroll">
        <table className="matrix">
          <thead>
            <tr>
              <th>Table</th>
              {cols.map((ts) => (
                <th key={ts.uuid} className="num" title={`${ts.uuid}\n${[ts.cloud, ts.region, ts.zone].join('.')}`}>
                  {ts.host_port ? <a href={serverUrl(ts.host_port)}>{ts.host_port}</a> : <Mono>{ts.uuid}</Mono>}
                  <div className="sub">{[ts.cloud, ts.region, ts.zone].filter(Boolean).join('.')}</div>
                  {ts.alive === false && <Badge kind="bad">dead</Badge>}
                </th>
              ))}
            </tr>
            <tr className="totals">
              <th>All tables</th>
              {cols.map((ts) => {
                const t = totals.get(ts.uuid);
                return (
                  <th key={ts.uuid} className="num">
                    {formatNumber(t?.replicas ?? 0)} ({formatNumber(t?.leaders ?? 0)})
                  </th>
                );
              })}
            </tr>
          </thead>
          <tbody>
            {rows.map((table) => {
              const counts = new Map((table.counts ?? []).map((c) => [c.uuid, c]));
              return (
                <tr key={table.table_id}>
                  <td>
                    <Link href={tableHref(table.table_id)}>
                      {unescapeHtml(table.keyspace)}.{unescapeHtml(table.table_name)}
                    </Link>
                  </td>
                  {cols.map((ts) => {
                    const c = counts.get(ts.uuid);
                    return (
                      <td key={ts.uuid} className={'num' + (c?.replicas ? '' : ' muted')}>
                        {c?.replicas ? `${c.replicas} (${c.leaders ?? 0})` : '·'}
                      </td>
                    );
                  })}
                </tr>
              );
            })}
            {rows.length === 0 && (
              <tr>
                <td colSpan={cols.length + 1} className="empty">
                  No tables.
                </td>
              </tr>
            )}
          </tbody>
        </table>
      </div>
      <Pager
        total={filtered.length}
        unfiltered={tables.length}
        page={rp}
        pages={rowPages}
        pageSize={TABLES_PER_PAGE}
        setPage={setRowPage}
        noun="tables"
      />
    </div>
  );
}

function LoadView({ json }: { json: LoadDistributionJson }) {
  return (
    <>
      <Section title="Ongoing remote bootstraps">
        <DataTable
          rows={json.ongoing_remote_bootstraps ?? []}
          rowKey={(r, i) => `${r.tablet_id}:${r.dest_ts_uuid ?? i}`}
          empty="None."
          columns={[
            { key: 'tablet', header: 'Tablet', sort: (r) => r.tablet_id, render: (r) => <Mono>{r.tablet_id}</Mono> },
            { key: 'table', header: 'Table', sort: (r) => r.table, render: (r) => unescapeHtml(r.table) },
            { key: 'source', header: 'Source', render: (r) => <Mono>{r.source_ts_uuid}</Mono> },
            { key: 'dest', header: 'Destination', render: (r) => <Mono>{r.dest_ts_uuid}</Mono> },
            { key: 'progress', header: 'Progress', render: (r) => String(r.progress ?? '') },
          ]}
        />
      </Section>
      <div className="columns">
        <Section title="Balancer warnings">
          <DataTable
            rows={json.warnings_summary ?? []}
            rowKey={(_, i) => String(i)}
            empty="None."
            initialSort={{ key: 'count', desc: true }}
            columns={[
              { key: 'message', header: 'Example', render: (w) => w.example_message },
              { key: 'count', header: 'Count', numeric: true, sort: (w) => w.count, render: (w) => formatNumber(w.count) },
            ]}
          />
        </Section>
        <Section title="Balancer tasks">
          <DataTable
            rows={json.tasks_summary ?? []}
            rowKey={(_, i) => String(i)}
            empty="None."
            initialSort={{ key: 'count', desc: true }}
            columns={[
              { key: 'description', header: 'Example', render: (t) => t.example_description },
              { key: 'state', header: 'State', sort: (t) => t.state, render: (t) => t.state },
              { key: 'count', header: 'Count', numeric: true, sort: (t) => t.count, render: (t) => formatNumber(t.count) },
              { key: 'status', header: 'Example status', render: (t) => t.example_status },
            ]}
          />
        </Section>
      </div>
      <Section title="Tablet distribution">
        {json.tablet_distribution ? <Matrix dist={json.tablet_distribution} /> : <span className="muted">Not reported.</span>}
      </Section>
    </>
  );
}

export function LoadDistributionPage() {
  const load = useJson<LoadDistributionJson>('/api/v1/load-distribution');
  return <Load result={load}>{(json) => <LoadView json={json} />}</Load>;
}
