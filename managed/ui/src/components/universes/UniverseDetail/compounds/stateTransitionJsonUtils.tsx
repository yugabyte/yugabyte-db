// Copyright (c) YugabyteDB, Inc.

import { colors } from '@app/redesign/theme/variables';

/** Allowed values for $deltaType → theme colors. */
export const DELTA_TYPE_COLORS: Record<string, string> = {
  DELETE: colors.error[500],
  REPLACE: colors.warning[500],
  ADD: colors.success[500]
};

type JsonSegment = { text: string; color?: string };

function isDeltaObject(value: unknown): value is Record<string, unknown> {
  return (
    value !== null &&
    typeof value === 'object' &&
    !Array.isArray(value) &&
    typeof (value as Record<string, unknown>)['$deltaType'] === 'string' &&
    ((value as Record<string, unknown>)['$deltaType'] as string) in DELTA_TYPE_COLORS
  );
}

function indentMultiline(text: string, indent: number): string {
  const pad = '  '.repeat(indent);
  return text
    .split('\n')
    .map((line, index) => (index === 0 ? line : pad + line))
    .join('\n');
}

/** Walk the value and append pretty-printed segments covering the full JSON. */
function appendJson(value: unknown, indent: number, out: JsonSegment[]): void {
  const pad = '  '.repeat(indent);
  const padInner = '  '.repeat(indent + 1);

  if (isDeltaObject(value)) {
    out.push({
      text: indentMultiline(JSON.stringify(value, null, 2), indent),
      color: DELTA_TYPE_COLORS[value['$deltaType'] as string]
    });
    return;
  }

  if (value === null || typeof value !== 'object') {
    out.push({ text: JSON.stringify(value) });
    return;
  }

  if (Array.isArray(value)) {
    if (value.length === 0) {
      out.push({ text: '[]' });
      return;
    }
    out.push({ text: '[\n' });
    value.forEach((item, index) => {
      out.push({ text: padInner });
      appendJson(item, indent + 1, out);
      out.push({ text: index < value.length - 1 ? ',\n' : '\n' });
    });
    out.push({ text: `${pad}]` });
    return;
  }

  const entries = Object.entries(value as Record<string, unknown>);
  if (entries.length === 0) {
    out.push({ text: '{}' });
    return;
  }

  out.push({ text: '{\n' });
  entries.forEach(([key, child], index) => {
    out.push({ text: `${padInner}${JSON.stringify(key)}: ` });
    appendJson(child, indent + 1, out);
    out.push({ text: index < entries.length - 1 ? ',\n' : '\n' });
  });
  out.push({ text: `${pad}}` });
}

function escapeHtml(text: string): string {
  return text
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

/** Full pretty-printed text (for tests / snapshots). */
export function formatJsonText(value: unknown): string {
  const segments: JsonSegment[] = [];
  appendJson(value, 0, segments);
  return segments.map((segment) => segment.text).join('');
}

/**
 * Full pretty-printed HTML with ADD/DELETE/REPLACE coloring.
 * Single HTML string avoids Chrome scrollHeight bugs with many React spans
 * inside a height-constrained <pre> (global CSS also sets pre { overflow-x: hidden }).
 */
export function formatJsonHtml(value: unknown): string {
  const segments: JsonSegment[] = [];
  appendJson(value, 0, segments);
  return segments
    .map((segment) => {
      const text = escapeHtml(segment.text);
      if (!segment.color) {
        return text;
      }
      return `<span style="color:${segment.color};font-weight:600">${text}</span>`;
    })
    .join('');
}
