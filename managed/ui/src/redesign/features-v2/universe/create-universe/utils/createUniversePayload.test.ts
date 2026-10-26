import { describe, expect, it, vi } from 'vitest';
import { mapCreateUniversePayload } from './createUniversePayload';
import { createUniverseFormProps } from '../CreateUniverseContext';
import { CloudType } from '@app/redesign/features/universe/universe-form/utils/dto';

// Placement and node-spec derivation are covered elsewhere; stub them so the fixture stays small.
vi.mock('./placementAndAvailability', () => ({
  getNodeCount: () => 3,
  getPlacementRegions: () => [{ uuid: 'region-uuid' }]
}));
vi.mock('./createUniverseNodeSpec', () => ({
  effectiveUseDedicatedNodes: () => false,
  getNodeSpec: () => ({})
}));
vi.mock('./resilienceReplication', () => ({
  getEffectiveReplicationFactorForResilience: () => 3
}));

const AZ_OVERRIDES = { 'us-west-1a': 'tserver:\n  resources: {}' };
const UNIVERSE_OVERRIDES = 'master:\n  resources: {}';

const buildFormValues = (
  code: CloudType,
  overrides: { universeOverrides?: string; azOverrides?: Record<string, string> }
) =>
  ({
    generalSettings: {
      universeName: 'test',
      databaseVersion: '2025.1.0.0',
      providerConfiguration: { uuid: 'provider-uuid', code }
    },
    resilienceAndRegionsSettings: {},
    nodesAvailabilitySettings: { availabilityZones: {} },
    instanceSettings: {},
    databaseSettings: { gFlags: [] },
    securitySettings: {},
    proxySettings: {},
    otherAdvancedSettings: { instanceTags: [], ...overrides }
  }) as unknown as createUniverseFormProps;

const getProviderSpec = (formValues: createUniverseFormProps) =>
  mapCreateUniversePayload(formValues).spec.clusters[0].provider_spec!;

describe('mapCreateUniversePayload helm overrides', () => {
  it('sends AZ overrides without universe overrides', () => {
    const spec = getProviderSpec(
      buildFormValues(CloudType.kubernetes, { universeOverrides: '', azOverrides: AZ_OVERRIDES })
    );
    expect(spec.az_helm_overrides).toEqual(AZ_OVERRIDES);
    expect(spec).not.toHaveProperty('helm_overrides');
  });

  it('sends universe overrides without AZ overrides', () => {
    const spec = getProviderSpec(
      buildFormValues(CloudType.kubernetes, { universeOverrides: UNIVERSE_OVERRIDES })
    );
    expect(spec.helm_overrides).toBe(UNIVERSE_OVERRIDES);
    expect(spec).not.toHaveProperty('az_helm_overrides');
  });

  it('sends both overrides', () => {
    const spec = getProviderSpec(
      buildFormValues(CloudType.kubernetes, {
        universeOverrides: UNIVERSE_OVERRIDES,
        azOverrides: AZ_OVERRIDES
      })
    );
    expect(spec.helm_overrides).toBe(UNIVERSE_OVERRIDES);
    expect(spec.az_helm_overrides).toEqual(AZ_OVERRIDES);
  });

  it('sends neither when both are empty', () => {
    const spec = getProviderSpec(
      buildFormValues(CloudType.kubernetes, { universeOverrides: '', azOverrides: {} })
    );
    expect(spec).not.toHaveProperty('helm_overrides');
    expect(spec).not.toHaveProperty('az_helm_overrides');
  });

  it('ignores overrides for non-K8s providers', () => {
    const spec = getProviderSpec(
      buildFormValues(CloudType.aws, {
        universeOverrides: UNIVERSE_OVERRIDES,
        azOverrides: AZ_OVERRIDES
      })
    );
    expect(spec).not.toHaveProperty('helm_overrides');
    expect(spec).not.toHaveProperty('az_helm_overrides');
  });
});
