import { ReactNode } from 'react';
import { Loaded } from '../api';

export function ErrorBox({ children }: { children: ReactNode }) {
  return <div className="alert error">{children}</div>;
}

export function Note({ children }: { children: ReactNode }) {
  return <div className="alert note">{children}</div>;
}

// Renders `render(data)` once loaded, else a loading line or the error, so one failing endpoint
// only blanks its own section.
export function Load<T>({ result, children }: {
  result: Loaded<T>;
  children: (data: T) => ReactNode;
}) {
  switch (result.state) {
    case 'loading':
      return <div className="loading">Loading…</div>;
    case 'error':
      return <ErrorBox>{result.error}</ErrorBox>;
    case 'ok': {
      const data = result.data as unknown;
      // The C++ handlers report a failure as {"error": "..."}.
      if (
        data !== null &&
        typeof data === 'object' &&
        typeof (data as { error?: unknown }).error === 'string' &&
        Object.keys(data).length === 1
      ) {
        return <ErrorBox>{(data as { error: string }).error}</ErrorBox>;
      }
      return <>{children(result.data)}</>;
    }
  }
}

export function Section({ title, children, actions }: {
  title: ReactNode;
  children: ReactNode;
  actions?: ReactNode;
}) {
  return (
    <section>
      <div className="section-head">
        <h2>{title}</h2>
        {actions}
      </div>
      {children}
    </section>
  );
}

export function KeyValues({ rows }: { rows: [ReactNode, ReactNode][] }) {
  return (
    <table className="kv">
      <tbody>
        {rows.map(([k, v], i) => (
          <tr key={i}>
            <th>{k}</th>
            <td>{v}</td>
          </tr>
        ))}
      </tbody>
    </table>
  );
}

export function Badge({ kind, children }: { kind: 'ok' | 'bad' | 'warn' | 'info'; children: ReactNode }) {
  return <span className={`badge ${kind}`}>{children}</span>;
}

export function Tabs<K extends string>({ tabs, value, onChange }: {
  tabs: { key: K; label: ReactNode }[];
  value: K;
  onChange: (k: K) => void;
}) {
  return (
    <div className="tabs" role="tablist">
      {tabs.map((t) => (
        <button
          key={t.key}
          role="tab"
          aria-selected={t.key === value}
          className={t.key === value ? 'active' : undefined}
          onClick={() => onChange(t.key)}
        >
          {t.label}
        </button>
      ))}
    </div>
  );
}

export function Mono({ children }: { children: ReactNode }) {
  return <span className="mono">{children}</span>;
}
