import { ReactNode } from 'react';
import { useJson } from '../api';
import { Badge, KeyValues, Load, Mono, Section } from '../components/Common';
import { DataTable } from '../components/DataTable';
import { formatNumber, formatTimeUs, serverUrl } from '../format';
import { Link } from '../router';
import { HostPort, MasterEntry, MastersJson, OverviewJson, VersionJson } from '../types';

function yesNo(v: boolean | null | undefined) {
  if (v === undefined) return '';
  if (v === null) return <span className="muted">unknown</span>;
  return v ? <Badge kind="ok">Yes</Badge> : <Badge kind="warn">No</Badge>;
}

export function TabletLimits({ limits }: { limits: OverviewJson['tablet_limits'] }) {
  if (!limits) return null;
  const peers = limits.tablet_peers ?? 0;
  const limit = limits.tablet_peer_limit;
  if (limit === undefined || limit === null) {
    return <>{formatNumber(peers)} tablet peers (no limit)</>;
  }
  const pct = limit > 0 ? Math.round((100 * peers) / limit) : 0;
  return (
    <>
      {formatNumber(peers)} of {formatNumber(limit)} tablet peers ({pct}%){' '}
      {pct >= 90 && <Badge kind="warn">near limit</Badge>}
      {limits.enforced === false && <span className="muted"> (not enforced)</span>}
    </>
  );
}

function Overview() {
  const overview = useJson<OverviewJson>('/api/v1/overview');
  return (
    <Load result={overview}>
      {(o) => (
        <KeyValues
          rows={[
            ['Universe UUID', <Mono>{o.universe_uuid}</Mono>],
            ...(o.cluster_uuid !== undefined
              ? ([['Cluster UUID', <Mono>{o.cluster_uuid}</Mono>]] as [ReactNode, ReactNode][])
              : []),
            ...(o.cluster_config_version !== undefined
              ? ([['Cluster config version', o.cluster_config_version]] as [ReactNode, ReactNode][])
              : []),
            ['Replication factor', o.replication_factor],
            [
              'Tablet servers',
              <>
                <Link href="/tablet-servers">{formatNumber(o.live_tservers)} live</Link>
                {!!o.dead_tservers && (
                  <>
                    {', '}
                    <Badge kind="bad">{formatNumber(o.dead_tservers)} dead</Badge>
                  </>
                )}
              </>,
            ],
            ['Load balancer enabled', yesNo(o.load_balancer_enabled)],
            ['Is load balanced', yesNo(o.is_load_balanced)],
            ['Encryption at rest', o.encryption_at_rest],
            ['Tablet peers', <TabletLimits limits={o.tablet_limits} />],
          ]}
        />
      )}
    </Load>
  );
}

function Version() {
  const version = useJson<VersionJson>('/api/v1/version');
  return (
    <Load result={version}>
      {(v) => (
        <KeyValues
          rows={[
            ['Version', `${v.version_number ?? ''}${v.build_number ? ` build ${v.build_number}` : ''}`],
            ['Build type', v.build_type],
            ['Revision', <Mono>{v.git_hash}</Mono>],
            ['Built', `${v.build_timestamp ?? ''}${v.build_hostname ? ` on ${v.build_hostname}` : ''}`],
          ]}
        />
      )}
    </Load>
  );
}

function addrs(hps: HostPort[] | undefined): string {
  return (hps ?? []).map((hp) => `${hp.host}:${hp.port}`).join(', ');
}

export function MastersTable({ masters }: { masters: MasterEntry[] }) {
  return (
    <DataTable
      rows={masters}
      rowKey={(m, i) => m.instance_id?.permanent_uuid ?? String(i)}
      columns={[
        {
          key: 'server',
          header: 'Server',
          render: (m) => {
            const http = m.registration?.http_addresses?.[0];
            const uuid = m.instance_id?.permanent_uuid ?? '';
            return http ? (
              <>
                <a href={serverUrl(`${http.host}:${http.port}`)}>{uuid || addrs([http])}</a>
              </>
            ) : (
              <Mono>{uuid}</Mono>
            );
          },
        },
        {
          key: 'role',
          header: 'Role',
          render: (m) =>
            m.error ? (
              <Badge kind="bad">{m.error.code ?? 'ERROR'}</Badge>
            ) : m.role === 'LEADER' ? (
              <Badge kind="info">LEADER</Badge>
            ) : (
              m.role
            ),
        },
        { key: 'rpc', header: 'RPC addresses', render: (m) => addrs(m.registration?.private_rpc_addresses) },
        { key: 'broadcast', header: 'Broadcast', render: (m) => addrs(m.registration?.broadcast_addresses) },
        {
          key: 'placement',
          header: 'Cloud / region / zone',
          render: (m) => {
            const c = m.registration?.cloud_info;
            return c ? [c.placement_cloud, c.placement_region, c.placement_zone].join(' / ') : '';
          },
        },
        {
          key: 'started',
          header: 'Started',
          render: (m) => formatTimeUs(m.instance_id?.start_time_us),
        },
        {
          key: 'hb',
          header: 'Heartbeat delay',
          numeric: true,
          render: (m) => (m.heartbeat_delay_ms !== undefined ? `${m.heartbeat_delay_ms} ms` : ''),
        },
        { key: 'error', header: 'Error', render: (m) => m.error?.message },
      ]}
    />
  );
}

const PAGE_LINKS: [string, string, string][] = [
  ['/tablet-servers', 'Tablet Servers', 'Heartbeats, load and disk usage of each tablet server'],
  ['/tables', 'Tables', 'Every table and index, with sizes'],
  ['/namespaces', 'Namespaces', 'Databases and keyspaces'],
  ['/masters', 'Masters', 'The masters and their roles'],
  ['/tablet-replication', 'Tablet Replication Health', 'Leaderless and under-replicated tablets'],
  ['/load-distribution', 'Cluster Balancer View', 'Tablet placement and balancer activity'],
  ['/tablet-server-clocks', 'Tablet Server Clocks', 'Physical and hybrid time per tablet server'],
  ['/cluster-config', 'Cluster Config', 'Placement, blacklists and universe settings'],
  ['/xcluster', 'xCluster', 'Replication groups and streams'],
  ['/tasks', 'Tasks', 'Running background tasks'],
  ['/stateful-services', 'Stateful Services', 'Where each stateful service runs'],
];

export function HomePage() {
  const masters = useJson<MastersJson>('/api/v1/masters');
  return (
    <>
      <div className="columns">
        <Section title="Overview">
          <Overview />
        </Section>
        <Section title="Version">
          <Version />
        </Section>
      </div>
      <Section title="Masters">
        <Load result={masters}>{(m) => <MastersTable masters={m.masters ?? []} />}</Load>
      </Section>
      <Section title="Pages">
        <div className="cards">
          {PAGE_LINKS.map(([href, title, text]) => (
            <Link key={href} href={href} className="card">
              <strong>{title}</strong>
              <span>{text}</span>
            </Link>
          ))}
        </div>
      </Section>
    </>
  );
}
