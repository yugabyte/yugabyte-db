import { beforeEach, describe, expect, it } from 'vitest';
import { RunTimeConfig } from '@app/redesign/features/universe/universe-form/utils/dto';
import { RuntimeConfigKey } from '@app/redesign/helpers/constants';
import {
  isUniverseRevampExperienceEnabled,
  syncOnboardingNewExperienceEnabled
} from './helper-methods';
import { resetTourProgress } from './tour-progress';

const runtimeConfig = (entries: Record<string, string>) =>
  (({
    configEntries: Object.entries(entries).map(([key, value]) => ({ key, value }))
  } as unknown) as RunTimeConfig);

const FEATURE_ON = {
  [RuntimeConfigKey.ENABLE_V2_EDIT_UNIVERSE_UI]: 'true'
};

beforeEach(() => {
  resetTourProgress();
});

describe('isUniverseRevampExperienceEnabled', () => {
  it('is undefined while no source has loaded, so callers do not route to the v1 UI', () => {
    expect(isUniverseRevampExperienceEnabled(undefined, 'SuperAdmin')).toBeUndefined();
    expect(isUniverseRevampExperienceEnabled(undefined, 'ReadOnly')).toBeUndefined();
  });

  it('falls back to runtime config for SuperAdmin before the mirror hydrates', () => {
    expect(isUniverseRevampExperienceEnabled(runtimeConfig(FEATURE_ON), 'SuperAdmin')).toBe(true);
    expect(isUniverseRevampExperienceEnabled(runtimeConfig({}), 'SuperAdmin')).toBe(false);
  });

  it('prefers the hydrated mirror for SuperAdmin so the banner toggle wins', () => {
    syncOnboardingNewExperienceEnabled(true);
    expect(isUniverseRevampExperienceEnabled(runtimeConfig({}), 'SuperAdmin')).toBe(true);
  });

  it('still requires the for-all-users flag for non-SuperAdmin', () => {
    expect(isUniverseRevampExperienceEnabled(runtimeConfig(FEATURE_ON), 'ReadOnly')).toBe(false);
    expect(
      isUniverseRevampExperienceEnabled(
        runtimeConfig({
          ...FEATURE_ON,
          [RuntimeConfigKey.ENABLE_NEW_UNIVERSE_EXPERIENCE_FOR_ALL_USERS]: 'true'
        }),
        'ReadOnly'
      )
    ).toBe(true);
  });
});
