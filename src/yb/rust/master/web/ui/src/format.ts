const ENTITIES: Record<string, string> = {
  '&amp;': '&',
  '&lt;': '<',
  '&gt;': '>',
  '&quot;': '"',
  '&#39;': "'",
  '&#x27;': "'",
};

// The C++ table page HTML-escapes column names inside its JSON, and the Rust master keeps those
// bytes; React escapes again, so undo it for display. Other names in the JSON are raw.
export function unescapeHtml(s: string | undefined | null): string {
  if (!s) return '';
  return s.indexOf('&') < 0 ? s : s.replace(/&(amp|lt|gt|quot|#39|#x27);/g, (m) => ENTITIES[m]);
}

// Decimal units, as the server's sizes (HumanizeBytes) are.
export function formatBytes(n: number | undefined | null): string {
  if (n === undefined || n === null || !Number.isFinite(n)) return '';
  const units = ['B', 'KB', 'MB', 'GB', 'TB', 'PB'];
  let v = n;
  let i = 0;
  while (Math.abs(v) >= 1000 && i < units.length - 1) {
    v /= 1000;
    i++;
  }
  return `${i === 0 ? v : v.toFixed(v < 10 ? 2 : 1)} ${units[i]}`;
}

export function formatSeconds(sec: number | undefined | null): string {
  if (sec === undefined || sec === null || !Number.isFinite(sec)) return '';
  if (sec < 60) return `${sec < 10 ? sec.toFixed(1) : Math.round(sec)}s`;
  const s = Math.floor(sec);
  const d = Math.floor(s / 86400);
  const h = Math.floor((s % 86400) / 3600);
  const m = Math.floor((s % 3600) / 60);
  if (d > 0) return `${d}d ${h}h`;
  if (h > 0) return `${h}h ${m}m`;
  return `${m}m ${s % 60}s`;
}

export function formatTimeUs(us: number | undefined | null): string {
  if (us === undefined || us === null || !Number.isFinite(us) || us <= 0) return '';
  return new Date(us / 1000).toISOString().replace('T', ' ').replace('Z', ' UTC');
}

export function formatNumber(n: number | undefined | null, digits = 0): string {
  if (n === undefined || n === null || !Number.isFinite(n)) return '';
  return n.toLocaleString(undefined, { maximumFractionDigits: digits });
}

export function plural(n: number, word: string): string {
  return `${formatNumber(n)} ${word}${n === 1 ? '' : 's'}`;
}

// A browser link to a server's web page; the master's own scheme is the best guess for theirs.
export function serverUrl(hostPort: string, path = ''): string {
  return `${window.location.protocol}//${hostPort}${path}`;
}
