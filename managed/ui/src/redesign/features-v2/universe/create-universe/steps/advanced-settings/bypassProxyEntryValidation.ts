export const isBypassEntryAsUrl = (entry: string): boolean => {
  const s = entry.trim();
  if (!s.length) return false;
  return /^[a-z][a-z0-9+.-]*:\/\//i.test(s);
};

export const isValidBypassProxyEntry = (entry: string): boolean => {
  const s = entry.trim();
  if (!s.length) return false;
  if (isBypassEntryAsUrl(s)) return false;
  if (/\s/.test(s)) return false;
  if (s.length > 1024) return false;
  if (!/[a-zA-Z0-9]/.test(s)) return false;
  return /^[a-zA-Z0-9._\-:*\/[\]%]+$/.test(s);
};
