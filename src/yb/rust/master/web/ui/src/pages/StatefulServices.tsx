import { useMemo } from 'react';
import { useJson } from '../api';
import { ErrorBox, Load, Mono, Section } from '../components/Common';
import { DataTable } from '../components/DataTable';
import { serverUrl } from '../format';
import { Link, tableHref } from '../router';
import { StatefulServicesJson, TServersJson } from '../types';
import { flattenTServers } from './TabletServers';

export function StatefulServicesPage() {
  const services = useJson<StatefulServicesJson>('/api/v1/stateful-services');
  const tservers = useJson<TServersJson>('/api/v1/tablet-servers');
  const addrByUuid = useMemo(() => {
    const m = new Map<string, string>();
    if (tservers.state === 'ok') {
      for (const r of flattenTServers(tservers.data)) {
        if (r.ts.permanent_uuid) m.set(r.ts.permanent_uuid, r.addr);
      }
    }
    return m;
  }, [tservers]);
  return (
    <Section title="Services">
      {tservers.state === 'error' && <ErrorBox>Tablet server addresses unavailable: {tservers.error}</ErrorBox>}
      <Load result={services}>
        {(json) => (
          <DataTable
            rows={json.stateful_services ?? []}
            rowKey={(s, i) => s.service_name ?? String(i)}
            empty="No stateful services."
            columns={[
              { key: 'name', header: 'Service', sort: (s) => s.service_name, render: (s) => s.service_name },
              {
                key: 'server',
                header: 'Hosting server',
                render: (s) => {
                  const addr = s.hosting_server && addrByUuid.get(s.hosting_server);
                  return (
                    <>
                      {addr && <a href={serverUrl(addr)}>{addr}</a>}
                      <div className="sub mono">{s.hosting_server}</div>
                    </>
                  );
                },
              },
              {
                key: 'table',
                header: 'Table',
                render: (s) =>
                  s.table_id && (
                    <Link href={tableHref(s.table_id)}>
                      <Mono>{s.table_id}</Mono>
                    </Link>
                  ),
              },
              { key: 'tablet', header: 'Tablet', render: (s) => <Mono>{s.tablet_id}</Mono> },
            ]}
          />
        )}
      </Load>
    </Section>
  );
}
