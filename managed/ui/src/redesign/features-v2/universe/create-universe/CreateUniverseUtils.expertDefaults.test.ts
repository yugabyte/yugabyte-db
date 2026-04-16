import { describe, expect, it } from 'vitest';
import { ArchitectureType } from '@app/components/configRedesign/providerRedesign/constants';
import type { Region } from '@app/redesign/features/universe/universe-form/utils/dto';
import { CloudType } from '@app/redesign/features/universe/universe-form/utils/dto';
import {
  effectiveUseDedicatedNodes,
  getAZCount,
  getInferredOutageCount,
  getExpertNodesStepDefaultPlacement,
  getNodeCount,
  getNodeSpec,
  inferResilience,
  mapCreateUniversePayload,
  reduceExpertNodeCountsToAtMostRf
} from './CreateUniverseUtils';
import type { createUniverseFormProps } from './CreateUniverseContext';
import type { OtherAdvancedProps } from './steps/advanced-settings/dtos';
import {
  FaultToleranceType,
  ResilienceFormMode,
  ResilienceType
} from './steps/resilence-regions/dtos';
import {
  FAULT_TOLERANCE_TYPE,
  NODE_COUNT,
  REGIONS_FIELD,
  RESILIENCE_FACTOR,
  RESILIENCE_FORM_MODE,
  RESILIENCE_TYPE,
  SINGLE_AVAILABILITY_ZONE
} from './fields/FieldNames';

function makeRegion(code: string, zoneCount: number): Region {
  return {
    uuid: `uuid-${code}`,
    code,
    name: code,
    ybImage: '',
    longitude: 0,
    latitude: 0,
    active: true,
    securityGroupId: null,
    details: null,
    zones: Array.from({ length: zoneCount }, (_, i) => ({
      code: `${code}-z${i}`,
      name: `Z${i}`,
      uuid: `${code}-zu-${i}`,
      active: true,
      subnet: ''
    }))
  };
}

function expertBase(regions: Region[], resilienceFactor = 1) {
  return {
    [RESILIENCE_TYPE]: ResilienceType.REGULAR,
    [RESILIENCE_FORM_MODE]: ResilienceFormMode.EXPERT_MODE,
    [FAULT_TOLERANCE_TYPE]: FaultToleranceType.AZ_LEVEL,
    [RESILIENCE_FACTOR]: resilienceFactor,
    [NODE_COUNT]: 1,
    [REGIONS_FIELD]: regions
  };
}

function expertBaseRegionLevel(regions: Region[], resilienceFactor = 1) {
  return {
    ...expertBase(regions, resilienceFactor),
    [FAULT_TOLERANCE_TYPE]: FaultToleranceType.REGION_LEVEL
  };
}

describe('getExpertNodesStepDefaultPlacement', () => {
  it('returns null for guided mode', () => {
    expect(
      getExpertNodesStepDefaultPlacement({
        ...expertBase([makeRegion('a', 3)]),
        [RESILIENCE_FORM_MODE]: ResilienceFormMode.GUIDED
      } as any)
    ).toBeNull();
  });

  it('returns null for SINGLE_NODE', () => {
    expect(
      getExpertNodesStepDefaultPlacement({
        ...expertBase([makeRegion('a', 3)]),
        [RESILIENCE_TYPE]: ResilienceType.SINGLE_NODE
      } as any)
    ).toBeNull();
  });

  it('returns null for NODE_LEVEL fault tolerance', () => {
    expect(
      getExpertNodesStepDefaultPlacement({
        ...expertBase([makeRegion('a', 3)]),
        [FAULT_TOLERANCE_TYPE]: FaultToleranceType.NODE_LEVEL
      } as any)
    ).toBeNull();
  });

  it('returns null when region count is outside Figma table (e.g. 8)', () => {
    const regions = Array.from({ length: 8 }, (_, i) => makeRegion(`r${i}`, 4));
    expect(getExpertNodesStepDefaultPlacement(expertBase(regions) as any)).toBeNull();
  });

  it('1 region, >2 AZs: RF 3, up to 3 AZs, total nodes >= 3', () => {
    const out = getExpertNodesStepDefaultPlacement(expertBase([makeRegion('a', 5)]) as any);
    expect(out).not.toBeNull();
    expect(out!.replicationFactor).toBe(3);
    expect(getAZCount(out!.availabilityZones)).toBe(3);
    expect(getNodeCount(out!.availabilityZones)).toBeGreaterThanOrEqual(3);
  });

  it('1 region, 2 AZs: RF 3, single AZ, 3 nodes', () => {
    const out = getExpertNodesStepDefaultPlacement(expertBase([makeRegion('a', 2)]) as any);
    expect(out).not.toBeNull();
    expect(out!.replicationFactor).toBe(3);
    expect(getAZCount(out!.availabilityZones)).toBe(1);
    expect(getNodeCount(out!.availabilityZones)).toBe(3);
  });

  it('2 regions: RF 3, 3 AZs, 2+1 split, total nodes >= 3', () => {
    const out = getExpertNodesStepDefaultPlacement(
      expertBase([makeRegion('r0', 3), makeRegion('r1', 3)]) as any
    );
    expect(out).not.toBeNull();
    expect(out!.replicationFactor).toBe(3);
    expect(getAZCount(out!.availabilityZones)).toBe(3);
    expect(out!.availabilityZones.r0.length).toBe(2);
    expect(out!.availabilityZones.r1.length).toBe(1);
    expect(
      [...out!.availabilityZones.r0, ...out!.availabilityZones.r1].every((z) => z.name === '')
    ).toBe(true);
    expect(getNodeCount(out!.availabilityZones)).toBeGreaterThanOrEqual(3);
  });

  it('3 regions: RF 3, 3 AZs, one per region', () => {
    const out = getExpertNodesStepDefaultPlacement(
      expertBase([makeRegion('r0', 2), makeRegion('r1', 2), makeRegion('r2', 2)]) as any
    );
    expect(out).not.toBeNull();
    expect(out!.replicationFactor).toBe(3);
    expect(getAZCount(out!.availabilityZones)).toBe(3);
    expect(out!.availabilityZones.r0.length).toBe(1);
    expect(out!.availabilityZones.r1.length).toBe(1);
    expect(out!.availabilityZones.r2.length).toBe(1);
  });

  it('4 regions: RF 5, 4 AZs, total nodes >= 5', () => {
    const out = getExpertNodesStepDefaultPlacement(
      expertBase([
        makeRegion('r0', 2),
        makeRegion('r1', 2),
        makeRegion('r2', 2),
        makeRegion('r3', 2)
      ]) as any
    );
    expect(out).not.toBeNull();
    expect(out!.replicationFactor).toBe(5);
    expect(getAZCount(out!.availabilityZones)).toBe(4);
    expect(getNodeCount(out!.availabilityZones)).toBeGreaterThanOrEqual(5);
  });

  it('6 regions: RF 7, 6 AZs, total nodes >= 7', () => {
    const regions = Array.from({ length: 6 }, (_, i) => makeRegion(`r${i}`, 2));
    const out = getExpertNodesStepDefaultPlacement(expertBase(regions) as any);
    expect(out).not.toBeNull();
    expect(out!.replicationFactor).toBe(7);
    expect(getAZCount(out!.availabilityZones)).toBe(6);
    expect(getNodeCount(out!.availabilityZones)).toBeGreaterThanOrEqual(7);
  });

  describe('REGION_LEVEL fault tolerance (parity with AZ_LEVEL Figma defaults)', () => {
    it('1 region, >2 AZs: same as AZ_LEVEL', () => {
      const az = expertBaseRegionLevel([makeRegion('a', 5)]) as any;
      const out = getExpertNodesStepDefaultPlacement(az);
      expect(out).not.toBeNull();
      expect(out!.replicationFactor).toBe(3);
      expect(getAZCount(out!.availabilityZones)).toBe(3);
    });

    it('2 regions: 2+1 split', () => {
      const out = getExpertNodesStepDefaultPlacement(
        expertBaseRegionLevel([makeRegion('r0', 3), makeRegion('r1', 3)]) as any
      );
      expect(out).not.toBeNull();
      expect(out!.replicationFactor).toBe(3);
      expect(out!.availabilityZones.r0.length).toBe(2);
      expect(out!.availabilityZones.r1.length).toBe(1);
    });

    it('3 regions: one AZ per region', () => {
      const out = getExpertNodesStepDefaultPlacement(
        expertBaseRegionLevel([
          makeRegion('r0', 2),
          makeRegion('r1', 2),
          makeRegion('r2', 2)
        ]) as any
      );
      expect(out).not.toBeNull();
      expect(out!.replicationFactor).toBe(3);
      expect(out!.availabilityZones.r0.length).toBe(1);
      expect(out!.availabilityZones.r1.length).toBe(1);
      expect(out!.availabilityZones.r2.length).toBe(1);
    });
  });
});

describe('getInferredOutageCount', () => {
  it('returns 0 when total nodes are below RF', () => {
    const availabilityZones = {
      r0: [
        { name: '', uuid: '', nodeCount: 1, preffered: 0 },
        { name: '', uuid: '', nodeCount: 1, preffered: 1 }
      ],
      r1: [{ name: '', uuid: '', nodeCount: 1, preffered: 2 }]
    };

    expect(getInferredOutageCount(FaultToleranceType.NODE_LEVEL, 5, availabilityZones as any)).toBe(0);
  });

  it('uses RF cap for valid NODE_LEVEL placement at RF node count', () => {
    const availabilityZones = {
      r0: [
        { name: '', uuid: '', nodeCount: 2, preffered: 0 },
        { name: '', uuid: '', nodeCount: 2, preffered: 1 }
      ],
      r1: [{ name: '', uuid: '', nodeCount: 1, preffered: 2 }]
    };

    expect(getInferredOutageCount(FaultToleranceType.NODE_LEVEL, 5, availabilityZones as any)).toBe(2);
  });

  it('uses RF cap for valid NODE_LEVEL placement above RF', () => {
    const availabilityZones = {
      r0: [
        { name: '', uuid: '', nodeCount: 3, preffered: 0 },
        { name: '', uuid: '', nodeCount: 2, preffered: 1 }
      ],
      r1: [{ name: '', uuid: '', nodeCount: 2, preffered: 2 }]
    };

    expect(getInferredOutageCount(FaultToleranceType.NODE_LEVEL, 5, availabilityZones as any)).toBe(2);
  });

  it('keeps RF-based outage count for non-node-level resilience', () => {
    const availabilityZones = {
      r0: [
        { name: '', uuid: '', nodeCount: 3, preffered: 0 },
        { name: '', uuid: '', nodeCount: 2, preffered: 1 }
      ]
    };

    expect(
      getInferredOutageCount(FaultToleranceType.AZ_LEVEL, 5, availabilityZones as any)
    ).toBe(2);
  });

  it('returns 0 for RF=1 sanity case', () => {
    const availabilityZones = {
      r0: [{ name: '', uuid: '', nodeCount: 1, preffered: 0 }]
    };
    expect(getInferredOutageCount(FaultToleranceType.NODE_LEVEL, 1, availabilityZones as any)).toBe(0);
  });
});

describe('inferResilience', () => {
  it('returns null when there are no selected AZs', () => {
    const resilience = expertBase([makeRegion('r0', 3)], 3);
    const out = inferResilience(resilience as any, {
      availabilityZones: {},
      replicationFactor: 3,
      useDedicatedNodes: false
    });
    expect(out).toBeNull();
  });

  it('returns null when regions exceed RF', () => {
    const resilience = expertBase([makeRegion('r0', 1), makeRegion('r1', 1), makeRegion('r2', 1)], 2);
    const out = inferResilience(resilience as any, {
      availabilityZones: {
        r0: [{ name: '', uuid: '', nodeCount: 1, preffered: 0 }],
        r1: [{ name: '', uuid: '', nodeCount: 1, preffered: 1 }],
        r2: [{ name: '', uuid: '', nodeCount: 1, preffered: 2 }]
      },
      replicationFactor: 2,
      useDedicatedNodes: false
    });
    expect(out).toBeNull();
  });

  it('returns REGION_LEVEL when regions equal RF', () => {
    const resilience = expertBase([makeRegion('r0', 2), makeRegion('r1', 2), makeRegion('r2', 2)], 3);
    const out = inferResilience(resilience as any, {
      availabilityZones: {
        r0: [{ name: '', uuid: '', nodeCount: 1, preffered: 0 }],
        r1: [{ name: '', uuid: '', nodeCount: 1, preffered: 1 }],
        r2: [{ name: '', uuid: '', nodeCount: 1, preffered: 2 }]
      },
      replicationFactor: 3,
      useDedicatedNodes: false
    });
    expect(out).toBe(FaultToleranceType.REGION_LEVEL);
  });

  it('returns AZ_LEVEL when regions < RF and AZ count equals RF', () => {
    const resilience = expertBase([makeRegion('r0', 3), makeRegion('r1', 3)], 3);
    const out = inferResilience(resilience as any, {
      availabilityZones: {
        r0: [
          { name: '', uuid: '', nodeCount: 1, preffered: 0 },
          { name: '', uuid: '', nodeCount: 1, preffered: 1 }
        ],
        r1: [{ name: '', uuid: '', nodeCount: 1, preffered: 2 }]
      },
      replicationFactor: 3,
      useDedicatedNodes: false
    });
    expect(out).toBe(FaultToleranceType.AZ_LEVEL);
  });

  it('returns NODE_LEVEL when regions < RF and AZ count < RF', () => {
    const resilience = expertBase([makeRegion('r0', 3), makeRegion('r1', 3)], 5);
    const out = inferResilience(resilience as any, {
      availabilityZones: {
        r0: [
          { name: '', uuid: '', nodeCount: 1, preffered: 0 },
          { name: '', uuid: '', nodeCount: 1, preffered: 1 }
        ],
        r1: [{ name: '', uuid: '', nodeCount: 1, preffered: 2 }]
      },
      replicationFactor: 5,
      useDedicatedNodes: false
    });
    expect(out).toBe(FaultToleranceType.NODE_LEVEL);
  });

  it('returns null when AZ count exceeds RF', () => {
    const resilience = expertBase([makeRegion('r0', 3), makeRegion('r1', 3)], 3);
    const out = inferResilience(resilience as any, {
      availabilityZones: {
        r0: [
          { name: '', uuid: '', nodeCount: 1, preffered: 0 },
          { name: '', uuid: '', nodeCount: 1, preffered: 1 }
        ],
        r1: [
          { name: '', uuid: '', nodeCount: 1, preffered: 2 },
          { name: '', uuid: '', nodeCount: 1, preffered: 3 }
        ]
      },
      replicationFactor: 3,
      useDedicatedNodes: false
    });
    expect(out).toBeNull();
  });
});

const minimalOtherAdvanced = (): OtherAdvancedProps => ({
  masterHttpPort: 7000,
  masterRpcPort: 7100,
  tserverHttpPort: 9000,
  tserverRpcPort: 9100,
  yqlServerHttpPort: 9042,
  yqlServerRpcPort: 9042,
  ysqlServerHttpPort: 5433,
  ysqlServerRpcPort: 5433,
  internalYsqlServerRpcPort: 6433,
  redisServerHttpPort: 11000,
  redisServerRpcPort: 6379,
  nodeExporterPort: 9300,
  ybControllerrRpcPort: 7200,
  instanceTags: [],
  useTimeSync: false,
  awsArnString: '',
  useSystemd: true,
  accessKeyCode: '',
  enableExposingService: false
});

describe('effectiveUseDedicatedNodes', () => {
  const base = (): createUniverseFormProps => ({
    activeStep: 1,
    nodesAvailabilitySettings: {
      availabilityZones: {},
      useDedicatedNodes: false
    }
  });

  it('is true for Kubernetes even when useDedicatedNodes is false', () => {
    expect(
      effectiveUseDedicatedNodes({
        ...base(),
        generalSettings: { cloud: CloudType.kubernetes } as any
      })
    ).toBe(true);
  });

  it('is false for non-K8s when useDedicatedNodes is false', () => {
    expect(
      effectiveUseDedicatedNodes({
        ...base(),
        generalSettings: { cloud: CloudType.aws } as any
      })
    ).toBe(false);
  });

  it('is true for non-K8s when useDedicatedNodes is true', () => {
    expect(
      effectiveUseDedicatedNodes({
        ...base(),
        generalSettings: { cloud: CloudType.aws } as any,
        nodesAvailabilitySettings: { availabilityZones: {}, useDedicatedNodes: true }
      })
    ).toBe(true);
  });
});

describe('reduceExpertNodeCountsToAtMostRf', () => {
  it('reduces totals to rf, decrementing highest-preferred AZs with spare nodes first', () => {
    const zones = {
      r0: [
        { name: 'a', uuid: '1', nodeCount: 3, preffered: 0 },
        { name: 'b', uuid: '2', nodeCount: 2, preffered: 1 },
        { name: 'c', uuid: '3', nodeCount: 2, preffered: 2 }
      ]
    };
    reduceExpertNodeCountsToAtMostRf(zones, 5);
    expect(getNodeCount(zones)).toBe(5);
    expect(zones.r0.map((z) => z.nodeCount)).toEqual([3, 1, 1]);
  });

  it('no-ops when total already at or below rf', () => {
    const zones = {
      r0: [
        { name: 'a', uuid: '1', nodeCount: 1, preffered: 0 },
        { name: 'b', uuid: '2', nodeCount: 1, preffered: 1 }
      ]
    };
    reduceExpertNodeCountsToAtMostRf(zones, 5);
    expect(getNodeCount(zones)).toBe(2);
  });

  it('stops when every AZ is at minimum node count but total still exceeds rf', () => {
    const zones = {
      r0: [
        { name: 'a', uuid: '1', nodeCount: 1, preffered: 0 },
        { name: 'b', uuid: '2', nodeCount: 1, preffered: 1 },
        { name: 'c', uuid: '3', nodeCount: 1, preffered: 2 }
      ]
    };
    reduceExpertNodeCountsToAtMostRf(zones, 2);
    expect(getNodeCount(zones)).toBe(3);
  });
});

describe('getNodeSpec / mapCreateUniversePayload (K8s as dedicated)', () => {
  const k8sForm = (): createUniverseFormProps => {
    const region = makeRegion('r0', 1);
    return {
      activeStep: 9,
      generalSettings: {
        cloud: CloudType.kubernetes,
        universeName: 'u1',
        databaseVersion: '2.20.0.0-b0',
        providerConfiguration: {
          uuid: 'prov-k8s',
          code: CloudType.kubernetes,
          name: 'k8s',
          active: true,
          customerUUID: 'cust',
          imageBundles: [],
          isOnPremManuallyProvisioned: false
        } as any
      },
      resilienceAndRegionsSettings: {
        ...expertBase([region], 1),
        [RESILIENCE_TYPE]: ResilienceType.SINGLE_NODE,
        [SINGLE_AVAILABILITY_ZONE]: region.zones[0].code
      } as any,
      nodesAvailabilitySettings: {
        availabilityZones: {},
        useDedicatedNodes: false
      },
      instanceSettings: {
        arch: ArchitectureType.X86_64,
        imageBundleUUID: 'bundle-1',
        useSpotInstance: false,
        instanceType: null,
        masterInstanceType: null,
        deviceInfo: null,
        masterDeviceInfo: null,
        tserverK8SNodeResourceSpec: { cpuCoreCount: 4, memoryGib: 8 },
        masterK8SNodeResourceSpec: null,
        keepMasterTserverSame: true,
        enableEbsVolumeEncryption: false,
        ebsKmsConfigUUID: null
      },
      databaseSettings: {
        ysql: { enable: true, enable_auth: false, password: '' },
        ycql: { enable: true, enable_auth: false, password: '' },
        gFlags: [],
        enableConnectionPooling: false,
        enablePGCompatibitilty: false
      },
      securitySettings: {
        enableIPV6: false,
        kmsConfig: ''
      },
      proxySettings: {
        enableProxyServer: false,
        secureWebProxy: false,
        secureWebProxyServer: '',
        webProxy: false,
        webProxyServer: '',
        byPassProxyList: false,
        byPassProxyListValues: []
      },
      otherAdvancedSettings: minimalOtherAdvanced()
    };
  };

  it('getNodeSpec does not require instance type when K8s and useDedicatedNodes is false', () => {
    const spec = getNodeSpec(k8sForm());
    expect(spec.k8s_tserver_resource_spec?.cpu_core_count).toBe(4);
    expect(spec.k8s_tserver_resource_spec?.memory_gib).toBe(8);
    expect(spec.k8s_master_resource_spec?.cpu_core_count).toBe(4);
  });

  it('mapCreateUniversePayload sets dedicated_nodes true for K8s when toggle is false', () => {
    const payload = mapCreateUniversePayload(k8sForm() as createUniverseFormProps);
    expect(payload.spec?.clusters?.[0]?.node_spec?.dedicated_nodes).toBe(true);
  });
});
