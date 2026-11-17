import { useJson } from '../api';
import { Load, Section } from '../components/Common';
import { JsonTree } from '../components/JsonTree';

export function ClusterConfigPage() {
  const config = useJson<unknown>('/api/v1/cluster-config');
  return (
    <Section title="Persisted cluster configuration (sys catalog)">
      <Load result={config}>{(c) => <JsonTree value={c} openDepth={3} />}</Load>
    </Section>
  );
}
