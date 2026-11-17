import { useJson } from '../api';
import { ErrorBox, Load, Mono, Section } from '../components/Common';
import { DataTable } from '../components/DataTable';
import { JsonTree, JsonValue } from '../components/JsonTree';
import { XClusterJson } from '../types';

interface InboundGroup {
  replication_group_id?: string;
  state?: string;
  tables?: unknown[];
  producer_master_addresses?: { host?: string; port?: number }[];
}

function XClusterView({ x }: { x: XClusterJson }) {
  const outbound = (x.outbound_replication_groups ?? []) as {
    replication_group_id?: string;
    metadata?: unknown;
    error?: string;
  }[];
  const inbound = (x.replication_infos ?? []) as InboundGroup[];
  return (
    <>
      {x.error && <ErrorBox>{x.error}</ErrorBox>}
      <Section title="Outbound replication groups (this universe is the source)">
        <DataTable
          rows={outbound}
          rowKey={(g, i) => g.replication_group_id ?? String(i)}
          empty="None."
          columns={[
            { key: 'id', header: 'Replication group', sort: (g) => g.replication_group_id, render: (g) => g.replication_group_id },
            {
              key: 'metadata',
              header: 'Metadata',
              render: (g) => (g.error ? <span className="error-text">{g.error}</span> : <JsonValue value={g.metadata} />),
            },
          ]}
        />
      </Section>
      <Section title="Outbound streams">
        <DataTable
          rows={x.outbound_streams ?? []}
          rowKey={(s, i) => s.stream_id ?? String(i)}
          empty="None."
          filter={(s, n) => (s.stream_id ?? '').toLowerCase().includes(n)}
          filterPlaceholder="Filter by stream id"
          columns={[
            { key: 'id', header: 'Stream', sort: (s) => s.stream_id, render: (s) => <Mono>{s.stream_id}</Mono> },
            { key: 'metadata', header: 'Metadata', render: (s) => <JsonValue value={s.metadata} /> },
          ]}
        />
      </Section>
      <Section title="Inbound replication groups (this universe is the target)">
        <DataTable
          rows={inbound}
          rowKey={(g, i) => g.replication_group_id ?? String(i)}
          empty="None."
          columns={[
            { key: 'id', header: 'Replication group', sort: (g) => g.replication_group_id, render: (g) => g.replication_group_id },
            { key: 'state', header: 'State', sort: (g) => g.state, render: (g) => g.state },
            { key: 'tables', header: 'Tables', numeric: true, sort: (g) => g.tables?.length, render: (g) => g.tables?.length },
            {
              key: 'source',
              header: 'Source masters',
              render: (g) => (g.producer_master_addresses ?? []).map((a) => `${a.host}:${a.port}`).join(', '),
            },
            { key: 'details', header: 'Details', render: (g) => <JsonValue value={g} /> },
          ]}
        />
      </Section>
      {x.safe_time_map && (
        <Section title="xCluster safe time">
          <DataTable
            rows={x.safe_time_map}
            rowKey={(s, i) => s.consumer_namespace_id ?? String(i)}
            columns={[
              { key: 'ns', header: 'Namespace', sort: (s) => s.consumer_namespace_id, render: (s) => <Mono>{s.consumer_namespace_id}</Mono> },
              { key: 'time', header: 'Safe time', render: (s) => <Mono>{String(s.safe_time ?? '')}</Mono> },
            ]}
          />
        </Section>
      )}
      <Section title="Replication status">
        <JsonTree value={x.replication_status ?? null} openDepth={1} />
      </Section>
      <Section title="Consumer registry">
        <JsonTree value={x.consumer_registry ?? null} openDepth={1} />
      </Section>
      <Section title="xCluster config">
        <JsonTree value={x.xcluster_config ?? null} openDepth={1} />
      </Section>
    </>
  );
}

export function XClusterPage() {
  const xcluster = useJson<XClusterJson>('/api/v1/xcluster');
  return <Load result={xcluster}>{(x) => <XClusterView x={x} />}</Load>;
}
