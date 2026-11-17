import { useState } from 'react';

const CHILD_PAGE = 200;

type Json = unknown;

function isContainer(v: Json): v is Record<string, Json> | Json[] {
  return typeof v === 'object' && v !== null;
}

function summary(v: Record<string, Json> | Json[]): string {
  if (Array.isArray(v)) return `[${v.length}]`;
  const n = Object.keys(v).length;
  return `{${n}}`;
}

function Scalar({ value }: { value: Json }) {
  if (value === null) return <span className="json-null">null</span>;
  switch (typeof value) {
    case 'string':
      return <span className="json-string">"{value}"</span>;
    case 'number':
      return <span className="json-number">{String(value)}</span>;
    case 'boolean':
      return <span className="json-bool">{String(value)}</span>;
    default:
      return <span>{String(value)}</span>;
  }
}

// Children render only once expanded, and at most CHILD_PAGE at a time, so a large config or a
// long array costs nothing until opened.
function Node({ name, value, depth, openDepth }: {
  name?: string;
  value: Json;
  depth: number;
  openDepth: number;
}) {
  const [open, setOpen] = useState(depth < openDepth);
  const [shown, setShown] = useState(CHILD_PAGE);
  const label = name !== undefined && <span className="json-key">{name}: </span>;
  if (!isContainer(value)) {
    return (
      <div className="json-row">
        {label}
        <Scalar value={value} />
      </div>
    );
  }
  const entries: [string, Json][] = Array.isArray(value)
    ? value.map((v, i) => [String(i), v])
    : Object.entries(value);
  return (
    <div className="json-row">
      <button className="json-toggle" onClick={() => setOpen(!open)} aria-expanded={open}>
        {open ? '▾' : '▸'}
      </button>
      {label}
      <span className="json-summary" onClick={() => setOpen(!open)}>
        {summary(value)}
      </span>
      {open && (
        <div className="json-children">
          {entries.slice(0, shown).map(([k, v]) => (
            <Node key={k} name={k} value={v} depth={depth + 1} openDepth={openDepth} />
          ))}
          {entries.length > shown && (
            <button className="link-button" onClick={() => setShown(shown + CHILD_PAGE)}>
              show {Math.min(CHILD_PAGE, entries.length - shown)} more of{' '}
              {entries.length - shown} remaining
            </button>
          )}
        </div>
      )}
    </div>
  );
}

export function JsonTree({ value, openDepth = 2 }: { value: Json; openDepth?: number }) {
  // Remounting with a new key applies a new expansion depth to every node.
  const [depth, setDepth] = useState(openDepth);
  const [generation, setGeneration] = useState(0);
  const expand = (d: number) => {
    setDepth(d);
    setGeneration(generation + 1);
  };
  return (
    <div className="json-tree">
      <div className="json-actions">
        <button onClick={() => expand(100)}>Expand all</button>
        <button onClick={() => expand(1)}>Collapse</button>
        <button onClick={() => void navigator.clipboard?.writeText(JSON.stringify(value, null, 2))}>
          Copy JSON
        </button>
      </div>
      <Node key={generation} value={value} depth={0} openDepth={depth} />
    </div>
  );
}

// A collapsed tree without the toolbar, for a value inside a table cell.
export function JsonValue({ value }: { value: Json }) {
  return (
    <div className="json-tree compact">
      <Node value={value} depth={0} openDepth={0} />
    </div>
  );
}
