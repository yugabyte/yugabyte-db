import { useMemo, useState } from 'react';
import { useJson } from '../api';
import { Badge, Load, Mono, Tabs } from '../components/Common';
import { Column, DataTable } from '../components/DataTable';
import { formatBytes, formatNumber, unescapeHtml } from '../format';
import { Link, tableHref } from '../router';
import { TableRow, TablesJson } from '../types';

type Category = 'user' | 'index' | 'parent' | 'system';
const CATEGORIES: { key: Category; label: string }[] = [
  { key: 'user', label: 'User tables' },
  { key: 'index', label: 'Index tables' },
  { key: 'parent', label: 'Colocation parents' },
  { key: 'system', label: 'System tables' },
];

interface Row {
  category: Category;
  table: TableRow;
  name: string;
  keyspace: string;
}

function onDisk(t: TableRow): number | undefined {
  const d = t.on_disk_size;
  if (!d) return undefined;
  return (d.sst_files_size_bytes ?? 0) + (d.wal_files_size_bytes ?? 0);
}

function filterRow(r: Row, needle: string): boolean {
  return (
    r.name.toLowerCase().includes(needle) ||
    r.keyspace.toLowerCase().includes(needle) ||
    r.table.uuid.toLowerCase().includes(needle) ||
    String(r.table.ysql_oid ?? '').includes(needle)
  );
}

function stateBadge(state: string | undefined) {
  if (!state || state === 'Running') return state;
  return <Badge kind="warn">{state}</Badge>;
}

function columns(showCategory: boolean, showSizes: boolean): Column<Row>[] {
  const cols: Column<Row>[] = [];
  if (showCategory) {
    cols.push({ key: 'category', header: 'Type', sort: (r) => r.category, render: (r) => r.category });
  }
  cols.push(
    { key: 'keyspace', header: 'Keyspace', sort: (r) => r.keyspace, render: (r) => r.keyspace },
    {
      key: 'name',
      header: 'Name',
      sort: (r) => r.name.toLowerCase(),
      render: (r) => (
        <>
          <Link href={tableHref(r.table.uuid)}>{r.name}</Link>
          {r.table.hidden && (
            <>
              {' '}
              <Badge kind="info">hidden</Badge>
            </>
          )}
        </>
      ),
    },
    {
      key: 'state',
      header: 'State',
      sort: (r) => r.table.state,
      render: (r) => (
        <>
          {stateBadge(r.table.state)}
          {r.table.message && <div className="sub">{r.table.message}</div>}
        </>
      ),
    },
    { key: 'uuid', header: 'UUID', sort: (r) => r.table.uuid, render: (r) => <Mono>{r.table.uuid}</Mono> },
    {
      key: 'oid',
      header: 'YSQL OID',
      numeric: true,
      sort: (r) => Number(r.table.ysql_oid) || undefined,
      render: (r) => r.table.ysql_oid,
    },
    {
      key: 'colocation',
      header: 'Parent OID / colocation id',
      render: (r) =>
        [r.table.parent_oid, r.table.colocation_id].filter((v) => v !== undefined && v !== '').join(' / '),
    },
  );
  if (showSizes) {
    cols.push(
      {
        key: 'size',
        header: 'On disk (SST + WAL)',
        numeric: true,
        sort: (r) => onDisk(r.table),
        render: (r) => {
          const d = r.table.on_disk_size;
          if (!d) return '';
          return (
            <span
              title={
                `SST ${formatBytes(d.sst_files_size_bytes)}, WAL ${formatBytes(d.wal_files_size_bytes)}` +
                (d.vector_index_size_bytes ? `, vector index ${formatBytes(d.vector_index_size_bytes)}` : '') +
                (d.has_missing_size ? '\nSome tablet sizes were not reported' : '')
              }
            >
              {formatBytes(onDisk(r.table))}
              {d.has_missing_size && ' *'}
            </span>
          );
        },
      },
      {
        key: 'sst',
        header: 'SST',
        numeric: true,
        sort: (r) => r.table.on_disk_size?.sst_files_size_bytes,
        render: (r) => formatBytes(r.table.on_disk_size?.sst_files_size_bytes),
      },
      {
        key: 'uncompressed',
        header: 'Uncompressed SST',
        numeric: true,
        sort: (r) => r.table.on_disk_size?.uncompressed_sst_file_size_bytes,
        render: (r) => formatBytes(r.table.on_disk_size?.uncompressed_sst_file_size_bytes),
      },
      {
        key: 'wal',
        header: 'WAL',
        numeric: true,
        sort: (r) => r.table.on_disk_size?.wal_files_size_bytes,
        render: (r) => formatBytes(r.table.on_disk_size?.wal_files_size_bytes),
      },
    );
  }
  return cols;
}

function TablesView({ json }: { json: TablesJson }) {
  const all = useMemo(() => {
    const rows: Row[] = [];
    for (const { key } of CATEGORIES) {
      for (const table of json[key] ?? []) {
        rows.push({
          category: key,
          table,
          name: unescapeHtml(table.table_name),
          keyspace: unescapeHtml(table.keyspace),
        });
      }
    }
    return rows;
  }, [json]);
  const counts = useMemo(() => {
    const c: Record<string, number> = {};
    for (const r of all) c[r.category] = (c[r.category] ?? 0) + 1;
    return c;
  }, [all]);
  const [tab, setTab] = useState<'all' | Category>('user');
  const rows = useMemo(() => (tab === 'all' ? all : all.filter((r) => r.category === tab)), [all, tab]);
  const showSizes = useMemo(() => rows.some((r) => r.table.on_disk_size), [rows]);

  const tabs = [
    { key: 'all' as const, label: `All (${formatNumber(all.length)})` },
    ...CATEGORIES.filter((c) => c.key !== 'parent' || counts.parent).map((c) => ({
      key: c.key,
      label: `${c.label} (${formatNumber(counts[c.key] ?? 0)})`,
    })),
  ];

  return (
    <>
      <Tabs tabs={tabs} value={tab} onChange={setTab} />
      <DataTable
        rows={rows}
        columns={columns(tab === 'all', showSizes)}
        rowKey={(r) => `${r.category}:${r.table.uuid}`}
        filter={filterRow}
        filterPlaceholder="Filter by name, keyspace, uuid or OID"
        empty="No tables."
      />
    </>
  );
}

export function TablesPage() {
  const tables = useJson<TablesJson>('/api/v1/tables');
  return <Load result={tables}>{(json) => <TablesView json={json} />}</Load>;
}
