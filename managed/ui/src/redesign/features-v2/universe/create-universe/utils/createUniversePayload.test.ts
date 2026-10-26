import { describe, expect, it, vi } from 'vitest';
import { getCreateEITPayload, mapCreateUniversePayload } from './createUniversePayload';
import { createUniverseFormProps } from '../CreateUniverseContext';
import { CloudType } from '@app/redesign/features/universe/universe-form/utils/dto';
import { CertType, SecuritySettingsProps } from '../steps/security-settings/dtos';

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

const k8sSettings = (overrides: Partial<SecuritySettingsProps>): SecuritySettingsProps => ({
  enableIPV6: false,
  enableExposingService: false,
  enableNodeToNodeEncryption: true,
  enableClientToNodeEncryption: true,
  certType: CertType.SELF_SIGNED,
  ...overrides
});

describe('getCreateEITPayload (kubernetes)', () => {
  it('disables EIT when the master toggle is off even if N2N/C2N are still true', () => {
    const payload = getCreateEITPayload(
      k8sSettings({
        enableBothEncryption: false,
        certType: CertType.CUSTOM,
        rootCertificate: 'cert-uuid'
      }),
      CloudType.kubernetes
    );
    expect(payload).toEqual({
      root_and_client_root_ca_same: true,
      enable_node_to_node_encrypt: false,
      enable_client_to_node_encrypt: false,
      root_ca: '',
      client_root_ca: ''
    });
  });

  it('enables both sides with a self-signed cert', () => {
    const payload = getCreateEITPayload(
      k8sSettings({ enableBothEncryption: true }),
      CloudType.kubernetes
    );
    expect(payload.enable_node_to_node_encrypt).toBe(true);
    expect(payload.enable_client_to_node_encrypt).toBe(true);
    expect(payload.root_ca).toBe('');
    expect(payload.client_root_ca).toBe('');
  });

  it('sends the custom cert only for the enabled side', () => {
    const payload = getCreateEITPayload(
      k8sSettings({
        enableBothEncryption: true,
        enableClientToNodeEncryption: false,
        certType: CertType.CUSTOM,
        rootCertificate: 'cert-uuid'
      }),
      CloudType.kubernetes
    );
    expect(payload.enable_node_to_node_encrypt).toBe(true);
    expect(payload.enable_client_to_node_encrypt).toBe(false);
    expect(payload.root_ca).toBe('cert-uuid');
    expect(payload.client_root_ca).toBe('');
  });
});
