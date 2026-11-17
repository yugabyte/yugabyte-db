import { useJson } from '../api';
import { Load, Section } from '../components/Common';
import { DataTable } from '../components/DataTable';
import { formatNumber } from '../format';
import { TasksJson } from '../types';

export function TasksPage() {
  const tasks = useJson<TasksJson>('/api/v1/tasks');
  return (
    <Section title="Running tasks">
      <Load result={tasks}>
        {(json) => (
          <DataTable
            rows={json.running ?? []}
            rowKey={(t, i) => t.name ?? String(i)}
            initialSort={{ key: 'count', desc: true }}
            empty="No tasks are running."
            columns={[
              { key: 'name', header: 'Task', sort: (t) => t.name, render: (t) => t.name },
              {
                key: 'count',
                header: 'Running',
                numeric: true,
                sort: (t) => t.count,
                render: (t) => formatNumber(t.count),
              },
            ]}
          />
        )}
      </Load>
    </Section>
  );
}
