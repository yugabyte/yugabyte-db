import { useJson } from '../api';
import { Load, Section } from '../components/Common';
import { MastersJson } from '../types';
import { MastersTable } from './Home';

// Any master answers /api/v1/masters itself, so this page works on a follower whose leader is
// down.
export function MastersPage() {
  const masters = useJson<MastersJson>('/api/v1/masters');
  return (
    <Section title="Masters">
      <Load result={masters}>{(m) => <MastersTable masters={m.masters ?? []} />}</Load>
    </Section>
  );
}
