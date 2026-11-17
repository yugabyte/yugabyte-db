import { createContext, useContext, useEffect, useState } from 'react';

export type Loaded<T> =
  | { state: 'loading' }
  | { state: 'ok'; data: T }
  | { state: 'error'; error: string };

// Bumped by the Refresh button; every useJson refetches when it changes.
export const RefreshContext = createContext(0);

// The pages answer text/plain, and a master that does not serve an endpoint (a follower whose C++
// leader predates it) answers HTML or a 404, so the body is parsed by hand.
export async function fetchJson<T>(url: string, signal?: AbortSignal): Promise<T> {
  const r = await fetch(url, { signal, headers: { Accept: 'application/json' } });
  const text = await r.text();
  if (!r.ok) {
    throw new Error(`HTTP ${r.status} ${r.statusText} from ${url}${snippet(text)}`);
  }
  try {
    return JSON.parse(text) as T;
  } catch {
    throw new Error(`${url} did not answer JSON; this master may not serve it${snippet(text)}`);
  }
}

function snippet(text: string): string {
  const plain = text.replace(/<[^>]*>/g, ' ').replace(/\s+/g, ' ').trim();
  if (!plain) return '';
  return `: ${plain.length > 200 ? plain.slice(0, 200) + '…' : plain}`;
}

export function useJson<T>(url: string | null): Loaded<T> {
  const refresh = useContext(RefreshContext);
  const [result, setResult] = useState<{ url: string | null; loaded: Loaded<T> }>({
    url,
    loaded: { state: 'loading' },
  });
  useEffect(() => {
    if (url === null) return;
    const abort = new AbortController();
    const set = (loaded: Loaded<T>) => setResult({ url, loaded });
    fetchJson<T>(url, abort.signal).then(
      (data) => set({ state: 'ok', data }),
      (e: unknown) => {
        if (!abort.signal.aborted) {
          set({ state: 'error', error: e instanceof Error ? e.message : String(e) });
        }
      },
    );
    return () => abort.abort();
  }, [url, refresh]);
  // A refresh keeps showing the previous answer until the new one arrives; another URL does not.
  return result.url === url ? result.loaded : { state: 'loading' };
}
