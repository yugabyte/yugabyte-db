import { useJson } from '../api';
import { Load, Mono, Section } from '../components/Common';
import { DataTable } from '../components/DataTable';
import { plural } from '../format';
import { NamespaceRow, NamespacesJson } from '../types';

function NamespaceTable({ rows }: { rows: NamespaceRow[] }) {
  return (
    <DataTable
      rows={rows}
      rowKey={(n) => n.id}
      filter={(n, needle) =>
        (n.name ?? '').toLowerCase().includes(needle) || n.id.toLowerCase().includes(needle)
      }
      filterPlaceholder="Filter by name or id"
      initialSort={{ key: 'name' }}
      columns={[
        { key: 'name', header: 'Name', sort: (n) => n.name ?? '', render: (n) => n.name ?? '' },
        { key: 'id', header: 'ID', sort: (n) => n.id, render: (n) => <Mono>{n.id}</Mono> },
        { key: 'language', header: 'Language', sort: (n) => n.language, render: (n) => n.language },
        { key: 'state', header: 'State', sort: (n) => n.state, render: (n) => n.state },
        { key: 'colocated', header: 'Colocated', sort: (n) => String(n.colocated), render: (n) => String(n.colocated ?? '') },
      ]}
    />
  );
}

export function NamespacesPage() {
  const namespaces = useJson<NamespacesJson>('/api/v1/namespaces');
  return (
    <Load result={namespaces}>
      {(json) =>
        Object.entries(json).map(([title, rows]) => (
          <Section
            key={title}
            title={
              <>
                {title} <span className="muted">· {plural((rows ?? []).length, 'namespace')}</span>
              </>
            }
          >
            <NamespaceTable rows={rows ?? []} />
          </Section>
        ))
      }
    </Load>
  );
}
