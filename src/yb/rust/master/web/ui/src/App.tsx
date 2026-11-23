import { useEffect, useRef, useState } from 'react';
import { RefreshContext } from './api';
import { at } from './base';
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
import { MastersPage } from './pages/Masters';
import logo from './assets/yugabyteDB-logo.svg';

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

// Server pages: full page loads (Link sends any path outside APP_PATHS to the server).
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
  '/masters': { title: 'Masters', render: () => <MastersPage /> },
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

function NavLinks({ items, path }: { items: NavItem[]; path: string }) {
  return (
    <>
      {items.map((item) => (
        <Link
          key={item.href}
          href={item.href}
          className={'nav-link' + (path === item.href ? ' active' : '')}
        >
          {item.label}
        </Link>
      ))}
    </>
  );
}

// On a narrow screen the nav is one row and a group's items drop down over the page.
const NARROW = '(max-width: 800px)';

// A collapsible group of nav links: inline in the sidebar, a dropdown on a narrow screen. It
// starts open in the sidebar when it holds the current page.
function NavGroup({ label, items, path }: { label: string; items: NavItem[]; path: string }) {
  const holdsPage = items.some((i) => i.href === path);
  const [open, setOpen] = useState(() => holdsPage && !window.matchMedia(NARROW).matches);
  const ref = useRef<HTMLDivElement>(null);
  useEffect(() => {
    if (!open) return;
    const close = (e: MouseEvent) => {
      if (window.matchMedia(NARROW).matches && !ref.current?.contains(e.target as Node)) {
        setOpen(false);
      }
    };
    document.addEventListener('mousedown', close);
    return () => document.removeEventListener('mousedown', close);
  }, [open]);
  return (
    <div className={'nav-group' + (open ? ' open' : '')} ref={ref}>
      <button
        className={'nav-link nav-group-toggle' + (holdsPage && !open ? ' active' : '')}
        aria-expanded={open}
        onClick={() => setOpen(!open)}
      >
        {label}
        <span className="chevron">{open ? '\u25b4' : '\u25be'}</span>
      </button>
      {open && (
        <div
          className="nav-group-items"
          onClick={() => window.matchMedia(NARROW).matches && setOpen(false)}
        >
          <NavLinks items={items} path={path} />
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
      <div className="shell">
        <aside className="sidebar">
          <Link href="/" className="logo" title="Home">
            <img src={at(logo)} alt="YugabyteDB" />
            <span>Master</span>
          </Link>
          <nav>
            <NavLinks items={MAIN_NAV} path={loc.path} />
            <NavGroup label="More" items={MORE_NAV} path={loc.path} />
            <NavGroup label="Utilities" items={UTILITIES_NAV} path={loc.path} />
          </nav>
        </aside>
        <div className="content">
          <header className="header">
            <h1>{page?.title ?? 'Not found'}</h1>
            <div className="refresh">
              <span className="muted" title="When this page's data was last requested">
                {loadedAt.toLocaleTimeString()}
              </span>
              <button onClick={() => setRefresh(refresh + 1)} title="Fetch this page's data again">
                Refresh
              </button>
            </div>
          </header>
          <main key={loc.path}>
            {page ? (
              page.render()
            ) : (
              <p>
                No page at <code>{loc.path}</code>. <Link href="/">Home</Link>
              </p>
            )}
          </main>
        </div>
      </div>
    </RefreshContext.Provider>
  );
}
