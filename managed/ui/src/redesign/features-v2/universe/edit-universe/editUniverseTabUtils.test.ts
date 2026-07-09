import { describe, expect, it } from 'vitest';
import { EditUniverseTabs } from './EditUniverseContext';
import {
  getEditUniverseSettingsRoute,
  isValidEditUniverseTab,
  parseEditUniverseTabFromPath
} from './editUniverseTabUtils';

const UNIVERSE_UUID = '11111111-1111-1111-1111-111111111111';

describe('parseEditUniverseTabFromPath', () => {
  it('returns the tab when path segment is valid', () => {
    expect(parseEditUniverseTabFromPath('placement')).toBe(EditUniverseTabs.PLACEMENT);
    expect(parseEditUniverseTabFromPath('hardware')).toBe(EditUniverseTabs.HARDWARE);
  });

  it('returns general when path segment is missing', () => {
    expect(parseEditUniverseTabFromPath(undefined)).toBe(EditUniverseTabs.GENERAL);
  });

  it('returns general when path segment is invalid', () => {
    expect(parseEditUniverseTabFromPath('foo')).toBe(EditUniverseTabs.GENERAL);
  });
});

describe('isValidEditUniverseTab', () => {
  it('returns true for valid tab values', () => {
    expect(isValidEditUniverseTab('security')).toBe(true);
  });

  it('returns false for invalid or missing values', () => {
    expect(isValidEditUniverseTab(undefined)).toBe(false);
    expect(isValidEditUniverseTab('invalid')).toBe(false);
  });
});

describe('getEditUniverseSettingsRoute', () => {
  it('builds settings route with general tab by default', () => {
    expect(getEditUniverseSettingsRoute(UNIVERSE_UUID)).toBe(
      `/universes/${UNIVERSE_UUID}/settings/general`
    );
  });

  it('builds settings route with the requested tab path segment', () => {
    expect(getEditUniverseSettingsRoute(UNIVERSE_UUID, EditUniverseTabs.PLACEMENT)).toBe(
      `/universes/${UNIVERSE_UUID}/settings/placement`
    );
  });
});
