import { ReactNode, useDeferredValue, useEffect, useMemo, useState } from 'react';

export interface Column<T> {
  key: string;
  header: ReactNode;
  render: (row: T) => ReactNode;
  // Sortable when set. Called once per row per sort, not per comparison.
  sort?: (row: T) => string | number | undefined | null;
  numeric?: boolean;
  title?: string;
}

interface Props<T> {
  rows: T[];
  columns: Column<T>[];
  rowKey: (row: T, index: number) => string;
  // Shows a filter box; `needle` is lower case and non-empty.
  filter?: (row: T, needle: string) => boolean;
  filterPlaceholder?: string;
  initialSort?: { key: string; desc?: boolean };
  pageSize?: number;
  empty?: ReactNode;
  toolbar?: ReactNode;
  rowClass?: (row: T) => string | undefined;
}

const PAGE_SIZES = [25, 100, 500];

type Key = string | number | undefined | null;

function compareKeys(a: Key, b: Key): number {
  if (a === b) return 0;
  if (a === undefined || a === null) return -1;
  if (b === undefined || b === null) return 1;
  return a < b ? -1 : a > b ? 1 : 0;
}

// Sorting, filtering and paging all happen here over the fetched array, and only one page of rows
// is ever rendered, so a page with a million tables stays responsive.
export function DataTable<T>(props: Props<T>) {
  const { rows, columns, rowKey, filter } = props;
  const [needle, setNeedle] = useState('');
  const deferredNeedle = useDeferredValue(needle.trim().toLowerCase());
  const [sort, setSort] = useState(props.initialSort);
  const [pageSize, setPageSize] = useState(props.pageSize ?? 100);
  const [page, setPage] = useState(0);

  const filtered = useMemo(
    () => (filter && deferredNeedle ? rows.filter((r) => filter(r, deferredNeedle)) : rows),
    [rows, filter, deferredNeedle],
  );

  const sorted = useMemo(() => {
    const col = sort && columns.find((c) => c.key === sort.key);
    if (!col?.sort) return filtered;
    const keys = filtered.map(col.sort);
    const order = filtered.map((_, i) => i);
    const sign = sort?.desc ? -1 : 1;
    order.sort((a, b) => sign * compareKeys(keys[a], keys[b]) || a - b);
    return order.map((i) => filtered[i]);
    // `columns` is usually rebuilt on every render; the sort key names the column.
  }, [filtered, sort?.key, sort?.desc]);

  const pages = Math.max(1, Math.ceil(sorted.length / pageSize));
  useEffect(() => setPage(0), [deferredNeedle, sort?.key, sort?.desc, pageSize]);
  const current = Math.min(page, pages - 1);
  const visible = sorted.slice(current * pageSize, (current + 1) * pageSize);

  const clickHeader = (col: Column<T>) => {
    if (!col.sort) return;
    setSort((s) =>
      s?.key === col.key ? { key: col.key, desc: !s.desc } : { key: col.key, desc: !!col.numeric },
    );
  };

  return (
    <div className="data-table">
      {(filter || props.toolbar) && (
        <div className="toolbar">
          {filter && (
            <input
              type="search"
              className="filter"
              placeholder={props.filterPlaceholder ?? 'Filter'}
              value={needle}
              onChange={(e) => setNeedle(e.target.value)}
            />
          )}
          {props.toolbar}
        </div>
      )}
      <div className="table-scroll">
        <table>
          <thead>
            <tr>
              {columns.map((c) => (
                <th
                  key={c.key}
                  className={(c.numeric ? 'num ' : '') + (c.sort ? 'sortable' : '')}
                  title={c.title}
                  onClick={() => clickHeader(c)}
                >
                  {c.header}
                  {sort?.key === c.key && <span className="sort-mark">{sort.desc ? ' ▼' : ' ▲'}</span>}
                </th>
              ))}
            </tr>
          </thead>
          <tbody>
            {visible.map((row, i) => (
              <tr key={rowKey(row, current * pageSize + i)} className={props.rowClass?.(row)}>
                {columns.map((c) => (
                  <td key={c.key} className={c.numeric ? 'num' : undefined}>
                    {c.render(row)}
                  </td>
                ))}
              </tr>
            ))}
            {visible.length === 0 && (
              <tr>
                <td colSpan={columns.length} className="empty">
                  {deferredNeedle ? 'No rows match the filter.' : (props.empty ?? 'None.')}
                </td>
              </tr>
            )}
          </tbody>
        </table>
      </div>
      <Pager
        total={sorted.length}
        unfiltered={rows.length}
        page={current}
        pages={pages}
        pageSize={pageSize}
        setPage={setPage}
        setPageSize={setPageSize}
      />
    </div>
  );
}

export function Pager(props: {
  total: number;
  unfiltered?: number;
  page: number;
  pages: number;
  pageSize: number;
  setPage: (p: number) => void;
  setPageSize?: (n: number) => void;
  noun?: string;
}) {
  const { total, page, pages, pageSize, setPage } = props;
  if (total <= PAGE_SIZES[0] && pages === 1 && props.unfiltered === total) {
    return null;
  }
  const first = total === 0 ? 0 : page * pageSize + 1;
  const last = Math.min(total, (page + 1) * pageSize);
  return (
    <div className="pager">
      <span>
        {first.toLocaleString()}–{last.toLocaleString()} of {total.toLocaleString()}
        {props.noun ? ` ${props.noun}` : ''}
        {props.unfiltered !== undefined && props.unfiltered !== total
          ? ` (filtered from ${props.unfiltered.toLocaleString()})`
          : ''}
      </span>
      <button disabled={page === 0} onClick={() => setPage(0)} title="First page">
        «
      </button>
      <button disabled={page === 0} onClick={() => setPage(page - 1)} title="Previous page">
        ‹
      </button>
      <span>
        page {(page + 1).toLocaleString()} / {pages.toLocaleString()}
      </span>
      <button disabled={page >= pages - 1} onClick={() => setPage(page + 1)} title="Next page">
        ›
      </button>
      <button disabled={page >= pages - 1} onClick={() => setPage(pages - 1)} title="Last page">
        »
      </button>
      {props.setPageSize && (
        <select value={pageSize} onChange={(e) => props.setPageSize?.(Number(e.target.value))}>
          {PAGE_SIZES.map((n) => (
            <option key={n} value={n}>
              {n} / page
            </option>
          ))}
        </select>
      )}
    </div>
  );
}
