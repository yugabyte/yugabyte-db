// Copyright (c) YugabyteDB, Inc.

import { describe, expect, it } from 'vitest';
import {
  DELTA_TYPE_COLORS,
  formatJsonHtml,
  formatJsonText
} from './stateTransitionJsonUtils';

/** Minimal delta-shaped payload covering ADD / DELETE / REPLACE and nesting. */
const DUMMY_STATE_TRANSITION = {
  clusters: [
    {
      userIntent: {
        numNodes: {
          $deltaType: 'REPLACE',
          $oldValue: 3,
          $newValue: 5
        }
      }
    }
  ],
  nodeDetailsSet: [
    {
      $deltaType: 'ADD',
      $newValue: {
        nodeName: 'yb-n4',
        cloudInfo: { az: 'us-west-1a' }
      }
    },
    {
      $deltaType: 'DELETE',
      $oldValue: {
        nodeName: 'yb-n1'
      }
    }
  ],
  communicationPorts: {
    masterHttpPort: 7000
  }
};

describe('formatJson (state transition UI)', () => {
  it('formats the full dummy delta JSON with themed ADD/DELETE/REPLACE colors', () => {
    const fullText = formatJsonText(DUMMY_STATE_TRANSITION);
    expect(fullText.startsWith('{')).toBe(true);
    expect(fullText.trimEnd().endsWith('}')).toBe(true);
    expect(fullText).toContain('"masterHttpPort": 7000');
    expect(fullText).toContain('"numNodes"');
    expect(fullText).toContain('yb-n4');
    expect(fullText).toContain('yb-n1');

    const html = formatJsonHtml(DUMMY_STATE_TRANSITION);
    expect(html).toContain(`color:${DELTA_TYPE_COLORS.ADD}`);
    expect(html).toContain(`color:${DELTA_TYPE_COLORS.DELETE}`);
    expect(html).toContain(`color:${DELTA_TYPE_COLORS.REPLACE}`);
    expect(html).toContain('ADD');
    expect(html).toContain('DELETE');
    expect(html).toContain('REPLACE');
    // Plain text and HTML should represent the same full document.
    expect(html.replace(/<[^>]+>/g, '')).toBe(fullText);
  });

  it('does not color plain objects that lack a valid $deltaType', () => {
    const html = formatJsonHtml({ foo: { $deltaType: 'UNKNOWN', value: 1 } });
    expect(html).not.toContain('color:');
    expect(html).toContain('"$deltaType"');
    expect(html).toContain('"UNKNOWN"');
  });
});
