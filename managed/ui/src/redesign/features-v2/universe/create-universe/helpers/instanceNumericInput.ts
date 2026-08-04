export const sanitizeUnsignedIntegerString = (raw: string): string => {
  return raw.replace(/\D/g, '');
};

export const parsePositiveIntegerInput = (
  raw: string,
  defaultValue: number,
  max?: number
): number => {
  const digits = sanitizeUnsignedIntegerString(raw);
  if (digits === '') {
    return defaultValue;
  }
  let n = parseInt(digits, 10);
  if (!Number.isFinite(n) || n <= 0) {
    return defaultValue;
  }
  if (max !== undefined && n > max) {
    n = max;
  }
  return n;
};

export const sanitizePositiveDecimalString = (raw: string): string => {
  let out = '';
  let dotSeen = false;
  for (const ch of String(raw)) {
    if (ch >= '0' && ch <= '9') {
      out += ch;
    } else if (ch === '.' && !dotSeen) {
      dotSeen = true;
      out += ch;
    }
  }
  return out;
};

export const parsePositiveDecimalInput = (
  raw: string,
  defaultValue: number,
  min: number,
  max: number
): number => {
  let s = sanitizePositiveDecimalString(raw);
  const parts = s.split('.');
  if (parts.length > 2) {
    s = parts[0] + '.' + parts.slice(1).join('');
  }
  if (s === '' || s === '.') {
    return defaultValue;
  }
  const n = Number(s);
  if (!Number.isFinite(n) || n <= 0) {
    return defaultValue;
  }
  return Math.min(max, Math.max(min, n));
};
