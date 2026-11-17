import { useJson } from '../api';
import { Load, Mono, Section } from '../components/Common';
import { DataTable } from '../components/DataTable';
import { Link, tableHref } from '../router';
import { CloudInfo, LeaderlessJson, UnderReplicatedJson } from '../types';

function TableLink({ id }: { id?: string }) {
  return id ? (
    <Link href={tableHref(id)}>
      <Mono>{id}</Mono>
    </Link>
  ) : null;
}

function cloud(c?: CloudInfo): string {
  return c ? [c.placement_cloud, c.placement_region, c.placement_zone].join('.') : '';
}

const matches = (n: string, ...values: (string | undefined)[]) =>
  values.some((v) => v?.toLowerCase().includes(n));

export function TabletReplicationPage() {
  const leaderless = useJson<LeaderlessJson>('/api/v1/tablet-replication');
  const under = useJson<UnderReplicatedJson>('/api/v1/tablet-under-replication');
  return (
    <>
      <Section title="Leaderless tablets">
        <Load result={leaderless}>
          {(json) => (
            <DataTable
              rows={json.leaderless_tablets ?? []}
              rowKey={(t) => t.tablet_uuid}
              empty="No leaderless tablets."
              filter={(t, n) => matches(n, t.table_uuid, t.tablet_uuid, t.reason)}
              filterPlaceholder="Filter by table, tablet or reason"
              columns={[
                { key: 'table', header: 'Table', sort: (t) => t.table_uuid, render: (t) => <TableLink id={t.table_uuid} /> },
                { key: 'tablet', header: 'Tablet', sort: (t) => t.tablet_uuid, render: (t) => <Mono>{t.tablet_uuid}</Mono> },
                { key: 'reason', header: 'Reason', sort: (t) => t.reason, render: (t) => t.reason },
              ]}
            />
          )}
        </Load>
      </Section>
      <Section title="Under-replicated tablets">
        <Load result={under}>
          {(json) => (
            <DataTable
              rows={json.underreplicated_tablets ?? []}
              rowKey={(t) => t.tablet_uuid}
              empty="No under-replicated tablets."
              filter={(t, n) => matches(n, t.table_uuid, t.tablet_uuid, ...(t.underreplicated_placements ?? []))}
              filterPlaceholder="Filter by table, tablet or placement"
              columns={[
                { key: 'table', header: 'Table', sort: (t) => t.table_uuid, render: (t) => <TableLink id={t.table_uuid} /> },
                { key: 'tablet', header: 'Tablet', sort: (t) => t.tablet_uuid, render: (t) => <Mono>{t.tablet_uuid}</Mono> },
                {
                  key: 'expected',
                  header: 'Expected replicas',
                  numeric: true,
                  sort: (t) => t.expected_num_replicas,
                  render: (t) => t.expected_num_replicas,
                },
                {
                  key: 'placements',
                  header: 'Under-replicated placements',
                  render: (t) => (t.underreplicated_placements ?? []).join(', '),
                },
                {
                  key: 'missing',
                  header: 'Missing replicas',
                  render: (t) =>
                    (t.missing_replicas ?? []).map((m, i) => (
                      <div key={i}>
                        {m.missing_replicas ?? '?'} in {m.placement_uuid || 'primary'}
                        {m.is_live === false ? ' (read replica)' : ''}
                        {(m.placement_blocks ?? []).map((b, j) => (
                          <div key={j} className="sub">
                            {b.missing_replicas ?? '?'} in {cloud(b.cloud_info)}
                          </div>
                        ))}
                      </div>
                    )),
                },
              ]}
            />
          )}
        </Load>
      </Section>
    </>
  );
}
