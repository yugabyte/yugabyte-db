import { describe, expect, it } from 'vitest';
import { TFunction } from 'i18next';
import { ResilienceType } from '../steps/resilence-regions/dtos';
import { getCreateUniverseSteps } from './createUniverseSteps';

const t = ((key: string) => key) as TFunction;

const titles = (isK8s: boolean, resilienceType = ResilienceType.REGULAR) =>
  getCreateUniverseSteps(t, resilienceType, isK8s).flatMap((group) =>
    group.subSteps.map((step) => step.title)
  );

describe('getCreateUniverseSteps', () => {
  it('includes proxy settings when the provider is not Kubernetes', () => {
    expect(titles(false)).toContain('proxySettings optional');
  });

  it('omits proxy settings for Kubernetes and keeps the following steps', () => {
    const steps = titles(true);
    expect(steps.some((title) => title.startsWith('proxySettings'))).toBe(false);
    expect(steps).toContain('otherAdvancedSettings optional');
    expect(steps).toContain('summaryAndCost');
  });

  it('omits both the nodes step and proxy settings for a single-node Kubernetes universe', () => {
    const steps = titles(true, ResilienceType.SINGLE_NODE);
    expect(steps.some((title) => title.startsWith('proxySettings'))).toBe(false);
    expect(steps.some((title) => title.startsWith('nodesAndAvailabilityZone'))).toBe(false);
    expect(steps).toContain('otherAdvancedSettings optional');
  });
});
