// Parse number inputs; empty -> null. Min/max checked on submit.

export const sanitizeUnsignedIntegerString = (raw: string): string => {
  return raw.replace(/\D/g, '');
};

export const parsePositiveIntegerInput = (raw: string): number | null => {
  const digits = sanitizeUnsignedIntegerString(raw);
  if (digits === '') {
    return null;
  }
  const n = parseInt(digits, 10);
  if (!Number.isFinite(n)) {
    return null;
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

export const parsePositiveDecimalInput = (raw: string): number | null => {
  let s = sanitizePositiveDecimalString(raw);
  const parts = s.split('.');
  if (parts.length > 2) {
    s = parts[0] + '.' + parts.slice(1).join('');
  }
  if (s === '' || s === '.') {
    return null;
  }
  const n = Number(s);
  if (!Number.isFinite(n) || n < 0) {
    return null;
  }
  return n;
};
