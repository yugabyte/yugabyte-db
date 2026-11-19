import { describe, expect, it } from 'vitest';
import { ResilienceFormMode } from '../steps/resilence-regions/dtos';
import {
  CREATE_UNIVERSE_FORM_MODE_SETTINGS_KEY,
  resolveCreateUniverseFormMode
} from './createUniverseFormModePreference';

describe('resolveCreateUniverseFormMode', () => {
  it('returns Guided when unset', () => {
    expect(resolveCreateUniverseFormMode(undefined)).toBe(ResilienceFormMode.GUIDED);
    expect(resolveCreateUniverseFormMode(null)).toBe(ResilienceFormMode.GUIDED);
    expect(resolveCreateUniverseFormMode({})).toBe(ResilienceFormMode.GUIDED);
  });

  it('returns stored Guided or Expert Mode', () => {
    expect(
      resolveCreateUniverseFormMode({
        [CREATE_UNIVERSE_FORM_MODE_SETTINGS_KEY]: ResilienceFormMode.GUIDED
      })
    ).toBe(ResilienceFormMode.GUIDED);
    expect(
      resolveCreateUniverseFormMode({
        [CREATE_UNIVERSE_FORM_MODE_SETTINGS_KEY]: ResilienceFormMode.EXPERT_MODE
      })
    ).toBe(ResilienceFormMode.EXPERT_MODE);
  });

  it('falls back to Guided for unknown values', () => {
    expect(
      resolveCreateUniverseFormMode({
        [CREATE_UNIVERSE_FORM_MODE_SETTINGS_KEY]: 'EXPERT_MODE'
      })
    ).toBe(ResilienceFormMode.GUIDED);
    expect(
      resolveCreateUniverseFormMode({
        [CREATE_UNIVERSE_FORM_MODE_SETTINGS_KEY]: true
      })
    ).toBe(ResilienceFormMode.GUIDED);
  });
});
