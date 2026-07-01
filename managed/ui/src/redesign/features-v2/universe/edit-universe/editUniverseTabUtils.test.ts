import { describe, expect, it } from 'vitest';
import { EditUniverseTabs } from './EditUniverseContext';
import {
  getEditUniverseSettingsRoute,
  isValidEditUniverseTab,
  parseEditUniverseTabFromQuery
} from './editUniverseTabUtils';

const UNIVERSE_UUID = '11111111-1111-1111-1111-111111111111';

describe('parseEditUniverseTabFromQuery', () => {
  it('returns the tab when query value is valid', () => {
    expect(parseEditUniverseTabFromQuery('placement')).toBe(EditUniverseTabs.PLACEMENT);
    expect(parseEditUniverseTabFromQuery('hardware')).toBe(EditUniverseTabs.HARDWARE);
  });

  it('returns general when query value is missing', () => {
    expect(parseEditUniverseTabFromQuery(undefined)).toBe(EditUniverseTabs.GENERAL);
  });

  it('returns general when query value is invalid', () => {
    expect(parseEditUniverseTabFromQuery('foo')).toBe(EditUniverseTabs.GENERAL);
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
  it('builds settings route without tab query param', () => {
    expect(getEditUniverseSettingsRoute(UNIVERSE_UUID)).toBe(
      `/universes/${UNIVERSE_UUID}/settings`
    );
  });

  it('builds settings route with tab query param', () => {
    expect(getEditUniverseSettingsRoute(UNIVERSE_UUID, EditUniverseTabs.PLACEMENT)).toBe(
      `/universes/${UNIVERSE_UUID}/settings?tab=placement`
    );
  });
});
