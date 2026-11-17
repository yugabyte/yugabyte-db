import { useMemo } from 'react';
import { Loaded, useJson } from '../api';
import { Badge, Load, Mono, Note, Section } from '../components/Common';
import { Column, DataTable } from '../components/DataTable';
import { formatBytes, formatNumber, formatSeconds, plural, serverUrl } from '../format';
import { ClocksJson, OverviewJson, PlacementSummary, TServer, TServersJson, YsqlLease } from '../types';
import { TabletLimits } from './Home';
import { Link } from '../router';

export interface TServerRow {
  addr: string;
  placement: string;
  ts: TServer;
  // From /api/v1/tablet-server-clocks; older leaders only have ts.lease_info.
  lease?: YsqlLease;
}

export function flattenTServers(json: TServersJson): TServerRow[] {
  const rows: TServerRow[] = [];
  for (const [placement, servers] of Object.entries(json)) {
    for (const [addr, ts] of Object.entries(servers ?? {})) {
      rows.push({ addr, placement, ts });
    }
  }
  return rows;
}

interface ClusterConfig {
  replication_info?: {
    live_replicas?: { placement_uuid?: string };
    read_replicas?: { placement_uuid?: string }[];
  };
}

// Names a placement the way the C++ page does: the live replicas' placement is the primary cluster.
// The overview says so directly; the cluster config is the fallback for a leader without it.
export function usePlacementLabel(
  placements?: PlacementSummary[],
): (placement: string) => string {
  const config: Loaded<ClusterConfig> = useJson<ClusterConfig>('/api/v1/cluster-config');
  return (placement: string) => {
    const summary = placements?.find((p) => p.placement_uuid === placement);
    if (summary?.is_primary !== undefined) {
      const kind = summary.is_primary ? 'Primary Cluster' : 'Read Replica';
      return placement ? `${kind} (${placement})` : kind;
    }
    const info = config.state === 'ok' ? config.data.replication_info : undefined;
    const primary = info?.live_replicas?.placement_uuid ?? '';
    if (placement === primary) return `Primary Cluster${placement ? ` (${placement})` : ''}`;
    if (info?.read_replicas?.some((r) => r.placement_uuid === placement)) {
      return `Read Replica (${placement})`;
    }
    return placement ? `Placement ${placement}` : 'Primary Cluster';
  };
}

export function StatusBadge({ ts }: { ts: TServer }) {
  return ts.status === 'ALIVE' ? (
    <Badge kind="ok">ALIVE</Badge>
  ) : (
    <Badge kind="bad">{ts.status ?? 'UNKNOWN'}</Badge>
  );
}

function diskUsage(ts: TServer): { used: number; total: number } {
  let used = 0;
  let total = 0;
  for (const p of ts.path_metrics ?? []) {
    used += p.space_used ?? 0;
    total += p.total_space_size ?? 0;
  }
  return { used, total };
}

function filterRow(r: TServerRow, needle: string): boolean {
  const t = r.ts;
  return [r.addr, t.permanent_uuid, t.cloud, t.region, t.zone, t.status].some((s) =>
    s?.toLowerCase().includes(needle),
  );
}

function hasLease(r: TServerRow): boolean {
  return (r.lease !== undefined && r.lease.state !== 'N/A') || r.ts.lease_info !== undefined;
}

function leaseSortKey(r: TServerRow): number | undefined {
  if (r.lease) {
    if (r.lease.state === 'LIVE') return r.lease.seconds ?? 0;
    if (r.lease.state === 'EXPIRED') return -(r.lease.seconds ?? 0) - 1;
    return r.lease.state === 'N/A' ? undefined : -Infinity;
  }
  return r.ts.lease_info?.lease_expiry_sec;
}

function Lease({ row }: { row: TServerRow }) {
  const l = row.lease;
  if (l) {
    switch (l.state) {
      case 'LIVE':
        return (
          <>
            <Badge kind="ok">LIVE</Badge> {formatSeconds(l.seconds)}
          </>
        );
      case 'EXPIRED':
        return (
          <>
            <Badge kind="bad">EXPIRED</Badge> {formatSeconds(l.seconds)} ago
          </>
        );
      case 'N/A':
      case undefined:
        return <span className="muted">N/A</span>;
      default:
        return <Badge kind="bad">{l.state}</Badge>;
    }
  }
  const info = row.ts.lease_info;
  if (!info) return null;
  return (
    <>
      {info.is_live ? <Badge kind="ok">LIVE</Badge> : <Badge kind="bad">EXPIRED</Badge>}{' '}
      {formatSeconds(info.lease_expiry_sec)}
    </>
  );
}

function columns(withLease: boolean): Column<TServerRow>[] {
  const cols: Column<TServerRow>[] = [
    {
      key: 'server',
      header: 'Server',
      sort: (r) => r.addr,
      render: (r) => (
        <>
          <a href={serverUrl(r.addr)}>{r.addr}</a>
          {r.ts.has_faulty_drive && (
            <>
              {' '}
              <Badge kind="bad">faulty drive</Badge>
            </>
          )}
          <div className="sub mono">{r.ts.permanent_uuid}</div>
        </>
      ),
    },
    {
      key: 'hb',
      header: 'Since heartbeat',
      numeric: true,
      sort: (r) => r.ts.time_since_hb_sec,
      render: (r) => r.ts.time_since_hb ?? formatSeconds(r.ts.time_since_hb_sec),
    },
    {
      key: 'status',
      header: 'Status / uptime',
      sort: (r) => (r.ts.status === 'ALIVE' ? 1 : 0),
      render: (r) => (
        <>
          <StatusBadge ts={r.ts} /> {formatSeconds(r.ts.uptime_seconds)}
        </>
      ),
    },
    {
      key: 'user',
      header: 'User tablets (leaders)',
      numeric: true,
      sort: (r) => r.ts.user_tablets_total,
      render: (r) =>
        `${formatNumber(r.ts.user_tablets_total)} (${formatNumber(r.ts.user_tablets_leaders)})`,
    },
    {
      key: 'system',
      header: 'System tablets (leaders)',
      numeric: true,
      sort: (r) => r.ts.system_tablets_total,
      render: (r) =>
        `${formatNumber(r.ts.system_tablets_total)} (${formatNumber(r.ts.system_tablets_leaders)})`,
    },
    {
      key: 'active',
      header: 'Active tablets',
      numeric: true,
      sort: (r) => r.ts.active_tablets,
      render: (r) => formatNumber(r.ts.active_tablets),
    },
    {
      key: 'ram',
      header: 'RAM used',
      numeric: true,
      sort: (r) => r.ts.ram_used_bytes,
      render: (r) => r.ts.ram_used ?? formatBytes(r.ts.ram_used_bytes),
    },
    {
      key: 'sst',
      header: 'SST size (uncompressed)',
      numeric: true,
      sort: (r) => r.ts.total_sst_file_size_bytes,
      render: (r) => (
        <>
          {r.ts.total_sst_file_size ?? formatBytes(r.ts.total_sst_file_size_bytes)}
          <div className="sub">
            {r.ts.uncompressed_sst_file_size ?? formatBytes(r.ts.uncompressed_sst_file_size_bytes)}
          </div>
        </>
      ),
    },
    {
      key: 'sst_files',
      header: 'SST files',
      numeric: true,
      sort: (r) => r.ts.num_sst_files,
      render: (r) => formatNumber(r.ts.num_sst_files),
    },
    {
      key: 'disk',
      header: 'Disk used',
      numeric: true,
      sort: (r) => {
        const d = diskUsage(r.ts);
        return d.total ? d.used / d.total : 0;
      },
      render: (r) => {
        const d = diskUsage(r.ts);
        if (!d.total) return '';
        const title = (r.ts.path_metrics ?? [])
          .map(
            (p) =>
              `${p.path}${p.storage_tier ? ` [${p.storage_tier}]` : ''}: ` +
              `${formatBytes(p.space_used)} / ${formatBytes(p.total_space_size)}`,
          )
          .join('\n');
        return (
          <span title={title}>
            {formatBytes(d.used)} / {formatBytes(d.total)}
          </span>
        );
      },
    },
    {
      key: 'read',
      header: 'Read ops/s',
      numeric: true,
      sort: (r) => r.ts.read_ops_per_sec,
      render: (r) => formatNumber(r.ts.read_ops_per_sec, 2),
    },
    {
      key: 'write',
      header: 'Write ops/s',
      numeric: true,
      sort: (r) => r.ts.write_ops_per_sec,
      render: (r) => formatNumber(r.ts.write_ops_per_sec, 2),
    },
    { key: 'cloud', header: 'Cloud', sort: (r) => r.ts.cloud, render: (r) => r.ts.cloud },
    { key: 'region', header: 'Region', sort: (r) => r.ts.region, render: (r) => r.ts.region },
    { key: 'zone', header: 'Zone', sort: (r) => r.ts.zone, render: (r) => r.ts.zone },
  ];
  if (withLease) {
    cols.push(
      {
        key: 'lease',
        header: 'YSQL lease expiry',
        numeric: true,
        sort: (r) => leaseSortKey(r),
        render: (r) => <Lease row={r} />,
      },
      {
        key: 'epoch',
        header: 'Lease epoch',
        numeric: true,
        sort: (r) => (r.lease ? r.lease.epoch : r.ts.lease_info?.lease_epoch),
        render: (r) => formatNumber(r.lease ? r.lease.epoch : r.ts.lease_info?.lease_epoch),
      },
    );
  }
  return cols;
}

function Placement({ title, rows }: { title: string; rows: TServerRow[] }) {
  const withLease = useMemo(() => rows.some(hasLease), [rows]);
  const dead = useMemo(() => rows.filter((r) => r.ts.status !== 'ALIVE').length, [rows]);
  return (
    <Section
      title={
        <>
          {title} <span className="muted">· {plural(rows.length, 'server')}</span>
          {dead > 0 && (
            <>
              {' '}
              <Badge kind="bad">{dead} not alive</Badge>
            </>
          )}
        </>
      }
    >
      <DataTable
        rows={rows}
        columns={columns(withLease)}
        rowKey={(r) => r.addr}
        filter={filterRow}
        filterPlaceholder="Filter by address, uuid, placement or status"
        initialSort={{ key: 'server' }}
        rowClass={(r) => (r.ts.status === 'ALIVE' ? undefined : 'row-bad')}
      />
    </Section>
  );
}

function Placements({ json, clocks, label }: {
  json: TServersJson;
  clocks: ClocksJson | undefined;
  label: (p: string) => string;
}) {
  const groups = useMemo(() => {
    const leases = new Map<string, YsqlLease>();
    for (const c of clocks?.tservers ?? []) {
      if (c.ysql_lease) leases.set(c.uuid, c.ysql_lease);
    }
    return Object.entries(json).map(([placement, servers]) => {
      const rows = flattenTServers({ [placement]: servers });
      for (const r of rows) r.lease = leases.get(r.ts.permanent_uuid ?? '');
      return [placement, rows] as const;
    });
  }, [json, clocks]);
  if (groups.length === 0) return <Note>No tablet servers have registered.</Note>;
  return (
    <>
      {groups.map(([placement, rows]) => (
        <Placement key={placement} title={label(placement)} rows={rows} />
      ))}
    </>
  );
}

function UniverseSummary({ placements }: { placements: PlacementSummary[] }) {
  const n = (v: number | undefined) => formatNumber(v);
  return (
    <Section title="Universe summary">
      <DataTable
        rows={placements}
        rowKey={(p) => p.placement_uuid}
        columns={[
          {
            key: 'cluster',
            header: 'Cluster',
            render: (p) => (
              <>
                {p.is_primary ? 'Primary Cluster' : 'Read Replica'}
                <div className="sub mono">{p.placement_uuid}</div>
              </>
            ),
          },
          { key: 'live', header: 'Live servers', numeric: true, render: (p) => n(p.live_tservers) },
          {
            key: 'blacklisted',
            header: 'Blacklisted',
            numeric: true,
            render: (p) => (p.blacklisted_tservers ? <Badge kind="warn">{n(p.blacklisted_tservers)}</Badge> : n(p.blacklisted_tservers)),
          },
          {
            key: 'dead',
            header: 'Dead servers',
            numeric: true,
            render: (p) => (p.dead_tservers ? <Badge kind="bad">{n(p.dead_tservers)}</Badge> : n(p.dead_tservers)),
          },
          { key: 'user', header: 'User tablet peers', numeric: true, render: (p) => n(p.user_tablet_peers) },
          { key: 'system', header: 'System tablet peers', numeric: true, render: (p) => n(p.system_tablet_peers) },
          { key: 'hidden', header: 'Hidden tablet peers', numeric: true, render: (p) => n(p.hidden_tablet_peers) },
          { key: 'active', header: 'Active tablet peers', numeric: true, render: (p) => n(p.active_tablet_peers) },
          {
            key: 'limit',
            header: 'Tablet peer limit',
            numeric: true,
            render: (p) =>
              p.tablet_peer_limit === null || p.tablet_peer_limit === undefined ? (
                <span className="muted">none</span>
              ) : (
                n(p.tablet_peer_limit)
              ),
          },
        ]}
      />
    </Section>
  );
}

export function TabletServersPage() {
  const tservers = useJson<TServersJson>('/api/v1/tablet-servers');
  const overview = useJson<OverviewJson>('/api/v1/overview');
  const clocks = useJson<ClocksJson>('/api/v1/tablet-server-clocks');
  const o = overview.state === 'ok' ? overview.data : undefined;
  const label = usePlacementLabel(o?.placements);
  return (
    <>
      {o?.placements && o.placements.length > 0 && <UniverseSummary placements={o.placements} />}
      {o?.tablet_limits && (
        <Note>
          Tablet peers: <TabletLimits limits={o.tablet_limits} />
        </Note>
      )}
      <Load result={tservers}>
        {(json) => (
          <Placements json={json} clocks={clocks.state === 'ok' ? clocks.data : undefined} label={label} />
        )}
      </Load>
      <p className="muted">
        <Mono>/api/v1/tablet-servers</Mono> has this data as JSON. See also{' '}
        <Link href="/tablet-server-clocks">tablet server clocks</Link>.
      </p>
    </>
  );
}
