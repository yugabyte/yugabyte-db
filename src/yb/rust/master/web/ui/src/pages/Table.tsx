import { ReactNode, useMemo } from 'react';
import { useJson } from '../api';
import { Badge, ErrorBox, KeyValues, Load, Mono, Note, Section } from '../components/Common';
import { DataTable } from '../components/DataTable';
import { JsonTree } from '../components/JsonTree';
import { formatNumber, plural, unescapeHtml } from '../format';
import { Link, useLocation } from '../router';
import { TableJson, TabletLocation, TabletRow } from '../types';

// The C++ table page's is_deleted(): these tablets are hidden unless `show_deleted` is set.
const DELETED_STATES = new Set(['Deleted', 'Replaced']);

function Location({ loc }: { loc: TabletLocation }) {
  const role =
    loc.role === 'LEADER' ? <Badge kind="info">LEADER</Badge> : <span className="role">{loc.role}</span>;
  let target: ReactNode = <Mono>{loc.uuid}</Mono>;
  if (/^https?:\/\//.test(loc.location)) {
    const host = new URL(loc.location).host;
    target = (
      <a href={loc.location} title={loc.uuid}>
        {host}
      </a>
    );
  }
  return (
    <div className="location">
      {role} {target}
    </div>
  );
}

function Tablets({ tablets, showDeleted, toggleHref }: {
  tablets: TabletRow[];
  showDeleted: boolean;
  toggleHref: string;
}) {
  const deleted = useMemo(() => tablets.filter((t) => DELETED_STATES.has(t.state ?? '')).length, [tablets]);
  const rows = useMemo(
    () => (showDeleted ? tablets : tablets.filter((t) => !DELETED_STATES.has(t.state ?? ''))),
    [tablets, showDeleted],
  );
  return (
    <Section
      title={
        <>
          Tablets <span className="muted">· {plural(rows.length, 'tablet')}</span>
        </>
      }
      actions={
        deleted > 0 && (
          <Link href={toggleHref} className="small">
            {showDeleted ? 'Hide' : 'Show'} {formatNumber(deleted)} deleted
          </Link>
        )
      }
    >
      <DataTable
        rows={rows}
        rowKey={(t) => t.tablet_id}
        filter={(t, n) =>
          t.tablet_id.toLowerCase().includes(n) ||
          (t.partition ?? '').toLowerCase().includes(n) ||
          (t.state ?? '').toLowerCase().includes(n) ||
          (t.locations ?? []).some((l) => l.uuid.toLowerCase().includes(n) || l.location.toLowerCase().includes(n))
        }
        filterPlaceholder="Filter by tablet id, partition, state or server"
        columns={[
          { key: 'id', header: 'Tablet ID', sort: (t) => t.tablet_id, render: (t) => <Mono>{t.tablet_id}</Mono> },
          { key: 'partition', header: 'Partition', render: (t) => <span className="mono small">{t.partition}</span> },
          { key: 'split', header: 'Split depth', numeric: true, sort: (t) => t.split_depth, render: (t) => t.split_depth },
          {
            key: 'state',
            header: 'State',
            sort: (t) => t.state,
            render: (t) => (
              <>
                {t.state === 'Running' ? t.state : <Badge kind="warn">{t.state}</Badge>}
                {t.hidden === 'true' && (
                  <>
                    {' '}
                    <Badge kind="info">hidden</Badge>
                  </>
                )}
              </>
            ),
          },
          { key: 'message', header: 'Message', render: (t) => t.message },
          {
            key: 'locations',
            header: 'Replicas',
            render: (t) => (t.locations ?? []).map((l) => <Location key={l.uuid} loc={l} />),
          },
        ]}
      />
    </Section>
  );
}

function TableView({ table, showDeleted, toggleHref }: {
  table: TableJson;
  showDeleted: boolean;
  toggleHref: string;
}) {
  const info: [ReactNode, ReactNode][] = [
    ['Name', unescapeHtml(table.table_name)],
    ['ID', <Mono>{table.table_id}</Mono>],
    ['Version', table.table_version],
    ['Type', table.table_type],
    [
      'State',
      <>
        {table.table_state}
        {table.table_state_message && <span className="muted"> {table.table_state_message}</span>}
      </>,
    ],
  ];
  if (table.table_tablespace_oid !== undefined) info.push(['Tablespace OID', table.table_tablespace_oid]);
  if (table.table_transaction_verifier_state !== undefined) {
    info.push(['DDL transaction', <Mono>{table.table_transaction_verifier_state}</Mono>]);
    info.push(['DDL', table.table_transaction_ddl]);
  }
  const replication = table.table_replication_info;
  return (
    <>
      {table.error && <ErrorBox>{table.error}</ErrorBox>}
      <div className="columns">
        <Section title="Table">
          <KeyValues rows={info} />
        </Section>
        <Section title="Replication info">
          {replication === undefined ? (
            <span className="muted">None</span>
          ) : typeof replication === 'string' ? (
            <Note>{replication}</Note>
          ) : (
            <JsonTree value={replication} openDepth={3} />
          )}
        </Section>
      </div>
      {table.columns && (
        <Section title="Columns">
          <DataTable
            rows={table.columns}
            rowKey={(c) => c.id}
            columns={[
              { key: 'column', header: 'Column', render: (c) => unescapeHtml(c.column) },
              { key: 'id', header: 'ID', render: (c) => c.id },
              { key: 'type', header: 'Type', render: (c) => <Mono>{c.type}</Mono> },
            ]}
          />
        </Section>
      )}
      {table.tablets && <Tablets tablets={table.tablets} showDeleted={showDeleted} toggleHref={toggleHref} />}
      {table.Tasks && table.Tasks.length > 0 && (
        <Section title="Tasks">
          <DataTable
            rows={table.Tasks}
            rowKey={(_, i) => String(i)}
            columns={[
              { key: 'type', header: 'Type', render: (t) => t.task_type },
              { key: 'state', header: 'State', render: (t) => t.task_state },
              { key: 'since', header: 'Started', render: (t) => t.task_time_since_started },
              { key: 'running', header: 'Running for', render: (t) => t.task_running_secs },
              { key: 'description', header: 'Description', render: (t) => t.task_description },
            ]}
          />
        </Section>
      )}
    </>
  );
}

export function TablePage() {
  const { query } = useLocation();
  const showDeleted = query.has('show_deleted');
  const toggle = new URLSearchParams(query);
  if (showDeleted) toggle.delete('show_deleted');
  else toggle.set('show_deleted', '');
  const toggleHref = `/table?${toggle.toString().replace(/show_deleted=(&|$)/, 'show_deleted$1')}`;
  // The JSON page takes the same arguments: id, or keyspace_name and table_name.
  const api = new URLSearchParams(query);
  api.delete('show_deleted');
  const table = useJson<TableJson>(`/api/v1/table?${api.toString()}`);
  return (
    <Load result={table}>
      {(t) => <TableView table={t} showDeleted={showDeleted} toggleHref={toggleHref} />}
    </Load>
  );
}
