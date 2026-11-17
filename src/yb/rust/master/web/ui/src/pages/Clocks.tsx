import { useMemo } from 'react';
import { useJson } from '../api';
import { Load, Mono, Section } from '../components/Common';
import { DataTable } from '../components/DataTable';
import { formatSeconds, formatTimeUs, serverUrl } from '../format';
import { ClocksJson, TServer, TServersJson } from '../types';
import { flattenTServers, StatusBadge } from './TabletServers';

type ClockRow = NonNullable<ClocksJson['tservers']>[number] & { ts?: TServer };

export function ClocksPage() {
  const clocks = useJson<ClocksJson>('/api/v1/tablet-server-clocks');
  const tservers = useJson<TServersJson>('/api/v1/tablet-servers');
  const byUuid = useMemo(() => {
    const m = new Map<string, TServer>();
    if (tservers.state === 'ok') {
      for (const r of flattenTServers(tservers.data)) {
        if (r.ts.permanent_uuid) m.set(r.ts.permanent_uuid, r.ts);
      }
    }
    return m;
  }, [tservers]);

  return (
    <Section title="Clocks">
      {tservers.state === 'error' && (
        <div className="alert error">Tablet server status unavailable: {tservers.error}</div>
      )}
      <Load result={clocks}>
        {(json) => {
          const rows: ClockRow[] = (json.tservers ?? []).map((c) => ({ ...c, ts: byUuid.get(c.uuid) }));
          return (
            <DataTable
              rows={rows}
              rowKey={(r) => r.uuid}
              filter={(r, n) =>
                [r.uuid, r.host_port].some((s) => s?.toLowerCase().includes(n))
              }
              filterPlaceholder="Filter by address or uuid"
              initialSort={{ key: 'server' }}
              columns={[
                {
                  key: 'server',
                  header: 'Server',
                  sort: (r) => r.host_port ?? r.uuid,
                  render: (r) => (
                    <>
                      {r.host_port ? <a href={serverUrl(r.host_port)}>{r.host_port}</a> : null}
                      <div className="sub mono">{r.uuid}</div>
                    </>
                  ),
                },
                {
                  key: 'status',
                  header: 'Status',
                  render: (r) => (r.ts ? <StatusBadge ts={r.ts} /> : ''),
                },
                {
                  key: 'hb',
                  header: 'Since heartbeat',
                  numeric: true,
                  sort: (r) => r.time_since_hb_sec,
                  render: (r) => formatSeconds(r.time_since_hb_sec),
                },
                {
                  key: 'physical',
                  header: 'Physical time (UTC)',
                  sort: (r) => r.physical_time_us,
                  render: (r) =>
                    r.physical_time_us ? formatTimeUs(r.physical_time_us) : <span className="muted">unknown</span>,
                },
                {
                  key: 'hybrid',
                  header: 'Hybrid time',
                  render: (r) => <Mono>{r.hybrid_time}</Mono>,
                },
              ]}
            />
          );
        }}
      </Load>
    </Section>
  );
}
