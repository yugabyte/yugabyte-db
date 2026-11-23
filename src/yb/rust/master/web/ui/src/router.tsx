import { AnchorHTMLAttributes, MouseEvent, useEffect, useState } from 'react';
import { at, fromBrowserPath } from './base';

// The app's own pages. The master serves index.html at each of them (`PAGES` and `HOST_PAGES` in
// master/web/src/ui.rs); any other path is a server page and gets a full load.
export const APP_PATHS = new Set([
  '/',
  '/tablet-servers',
  '/tablet-server-clocks',
  '/tables',
  '/namespaces',
  '/masters',
  '/table',
  '/cluster-config',
  '/xcluster',
  '/tasks',
  '/tablet-replication',
  '/load-distribution',
  '/stateful-services',
]);

export interface Location {
  path: string;
  query: URLSearchParams;
  search: string;
}

function current(): Location {
  const { pathname, search } = window.location;
  return { path: fromBrowserPath(pathname), query: new URLSearchParams(search), search };
}

const listeners = new Set<() => void>();
window.addEventListener('popstate', () => listeners.forEach((l) => l()));

export function navigate(href: string, replace = false): void {
  if (replace) window.history.replaceState(null, '', at(href));
  else window.history.pushState(null, '', at(href));
  window.scrollTo(0, 0);
  listeners.forEach((l) => l());
}

export function useLocation(): Location {
  const [loc, setLoc] = useState(current);
  useEffect(() => {
    const l = () => setLoc(current());
    listeners.add(l);
    return () => {
      listeners.delete(l);
    };
  }, []);
  return loc;
}

function isAppHref(href: string): boolean {
  if (!href.startsWith('/')) return false;
  return APP_PATHS.has(new URL(href, window.location.origin).pathname);
}

export function Link({ href, onClick, ...rest }: AnchorHTMLAttributes<HTMLAnchorElement> & {
  href: string;
}) {
  const handle = (e: MouseEvent<HTMLAnchorElement>) => {
    onClick?.(e);
    if (
      e.defaultPrevented ||
      e.button !== 0 ||
      e.metaKey ||
      e.ctrlKey ||
      e.shiftKey ||
      e.altKey ||
      !isAppHref(href)
    ) {
      return;
    }
    e.preventDefault();
    navigate(href);
  };
  return <a href={at(href)} onClick={handle} {...rest} />;
}

export function tableHref(id: string): string {
  return `/table?id=${encodeURIComponent(id)}`;
}
