import { useEffect, useRef, useState } from 'react';
import { RefreshContext } from './api';
import { Link, useLocation } from './router';
import { HomePage } from './pages/Home';
import { TabletServersPage } from './pages/TabletServers';
import { ClocksPage } from './pages/Clocks';
import { TablesPage } from './pages/Tables';
import { TablePage } from './pages/Table';
import { NamespacesPage } from './pages/Namespaces';
import { ClusterConfigPage } from './pages/ClusterConfig';
import { XClusterPage } from './pages/XCluster';
import { TasksPage } from './pages/Tasks';
import { TabletReplicationPage } from './pages/TabletReplication';
import { LoadDistributionPage } from './pages/LoadDistribution';
import { StatefulServicesPage } from './pages/StatefulServices';

interface NavItem {
  href: string;
  label: string;
}

const MAIN_NAV: NavItem[] = [
  { href: '/', label: 'Home' },
  { href: '/tablet-servers', label: 'Tablet Servers' },
  { href: '/tables', label: 'Tables' },
  { href: '/namespaces', label: 'Namespaces' },
  { href: '/masters', label: 'Masters' },
];

const MORE_NAV: NavItem[] = [
  { href: '/tablet-server-clocks', label: 'Tablet Server Clocks' },
  { href: '/cluster-config', label: 'Cluster Config' },
  { href: '/xcluster', label: 'xCluster' },
  { href: '/tasks', label: 'Tasks' },
  { href: '/tablet-replication', label: 'Tablet Replication Health' },
  { href: '/load-distribution', label: 'Cluster Balancer View' },
  { href: '/stateful-services', label: 'Stateful Services' },
  { href: '/ObjectLockManager', label: 'Object Lock Manager' },
];

// Server pages: plain links, full page loads.
const UTILITIES_NAV: NavItem[] = [
  { href: '/varz', label: 'Flags' },
  { href: '/logs', label: 'Logs' },
  { href: '/mem-trackers', label: 'Memory (trackers)' },
  { href: '/memz', label: 'Memory (tcmalloc)' },
  { href: '/threadz', label: 'Threads' },
  { href: '/rpcz', label: 'RPCs' },
  { href: '/metrics', label: 'Metrics' },
  { href: '/prometheus-metrics', label: 'Prometheus metrics' },
  { href: '/api/v1/varz', label: 'Flags (JSON)' },
];

const PAGES: Record<string, { title: string; render: () => JSX.Element }> = {
  '/': { title: 'Home', render: () => <HomePage /> },
  '/tablet-servers': { title: 'Tablet Servers', render: () => <TabletServersPage /> },
  '/tablet-server-clocks': { title: 'Tablet Server Clocks', render: () => <ClocksPage /> },
  '/tables': { title: 'Tables', render: () => <TablesPage /> },
  '/table': { title: 'Table', render: () => <TablePage /> },
  '/namespaces': { title: 'Namespaces', render: () => <NamespacesPage /> },
  '/cluster-config': { title: 'Cluster Config', render: () => <ClusterConfigPage /> },
  '/xcluster': { title: 'xCluster', render: () => <XClusterPage /> },
  '/tasks': { title: 'Tasks', render: () => <TasksPage /> },
  '/tablet-replication': {
    title: 'Tablet Replication Health',
    render: () => <TabletReplicationPage />,
  },
  '/load-distribution': { title: 'Cluster Balancer View', render: () => <LoadDistributionPage /> },
  '/stateful-services': { title: 'Stateful Services', render: () => <StatefulServicesPage /> },
};

function Menu({ label, groups, active }: {
  label: string;
  groups: { title?: string; items: NavItem[] }[];
  active: boolean;
}) {
  const [open, setOpen] = useState(false);
  const ref = useRef<HTMLDivElement>(null);
  useEffect(() => {
    if (!open) return;
    const close = (e: MouseEvent) => {
      if (!ref.current?.contains(e.target as Node)) setOpen(false);
    };
    document.addEventListener('mousedown', close);
    return () => document.removeEventListener('mousedown', close);
  }, [open]);
  return (
    <div className="menu" ref={ref}>
      <button
        className={'nav-link' + (active ? ' active' : '')}
        aria-expanded={open}
        onClick={() => setOpen(!open)}
      >
        {label} ▾
      </button>
      {open && (
        <div className="menu-items" onClick={() => setOpen(false)}>
          {groups.map((g, i) => (
            <div key={i} className="menu-group">
              {g.title && <div className="menu-title">{g.title}</div>}
              {g.items.map((item) => (
                <Link key={item.href} href={item.href}>
                  {item.label}
                </Link>
              ))}
            </div>
          ))}
        </div>
      )}
    </div>
  );
}

export function App() {
  const loc = useLocation();
  const [refresh, setRefresh] = useState(0);
  const [loadedAt, setLoadedAt] = useState(() => new Date());
  const page = PAGES[loc.path];

  useEffect(() => {
    document.title = `${page?.title ?? 'Not found'} - YugabyteDB Master`;
  }, [page]);
  useEffect(() => setLoadedAt(new Date()), [loc.path, loc.search, refresh]);

  return (
    <RefreshContext.Provider value={refresh}>
      <header className="topbar">
        <Link href="/" className="brand">
          YugabyteDB Master
        </Link>
        <nav>
          {MAIN_NAV.map((item) => (
            <Link
              key={item.href}
              href={item.href}
              className={'nav-link' + (loc.path === item.href ? ' active' : '')}
            >
              {item.label}
            </Link>
          ))}
          <Menu
            label="More"
            active={MORE_NAV.some((i) => i.href === loc.path)}
            groups={[{ items: MORE_NAV }]}
          />
          <Menu label="Utilities" active={false} groups={[{ items: UTILITIES_NAV }]} />
        </nav>
        <div className="refresh">
          <span className="muted" title="When this page's data was last requested">
            {loadedAt.toLocaleTimeString()}
          </span>
          <button onClick={() => setRefresh(refresh + 1)} title="Fetch this page's data again">
            ⟳ Refresh
          </button>
        </div>
      </header>
      <main key={loc.path}>
        {page ? (
          <>
            <h1>{page.title}</h1>
            {page.render()}
          </>
        ) : (
          <>
            <h1>Not found</h1>
            <p>
              No page at <code>{loc.path}</code>. <Link href="/">Home</Link>
            </p>
          </>
        )}
      </main>
    </RefreshContext.Provider>
  );
}
