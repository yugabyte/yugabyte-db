import { find, keys, values } from 'lodash';
import { NodeAvailabilityProps } from '../steps/nodes-availability/dtos';
import {
  FaultToleranceType,
  ResilienceAndRegionsProps,
  ResilienceFormMode,
  ResilienceType
} from '../steps/resilence-regions/dtos';
import { AvailabilityZone, Region } from '../../../../helpers/dtos';
import {
  FAULT_TOLERANCE_TYPE,
  REGIONS_FIELD,
  RESILIENCE_FORM_MODE,
  RESILIENCE_TYPE
} from '../fields/FieldNames';
import { getFaultToleranceNeeded, getEffectiveReplicationFactorForResilience } from './resilienceReplication';
import { PlacementRegion } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

export const getNodeCount = (availabilityZones: NodeAvailabilityProps['availabilityZones']) => {
  if (keys(availabilityZones).length === 0) {
    return 0;
  }
  return Object.values(availabilityZones).reduce((total, zones) => {
    return total + zones.reduce((sum, zone) => sum + zone.nodeCount, 0);
  }, 0);
};

export const getNodeCountNeeded = (
  totalNodesCount: number,
  totalRegions: number,
  regionIndex: number
) => {
  const base = Math.floor(totalNodesCount / totalRegions);
  const extra = regionIndex < totalNodesCount % totalRegions ? 1 : 0;
  return base + extra;
};

export const assignRegionsAZNodeByReplicationFactor = (
  resilienceAndRegionsSettings: ResilienceAndRegionsProps
): NodeAvailabilityProps['availabilityZones'] => {
  const {
    regions = [],
    resilienceFactor,
    resilienceType,
    faultToleranceType,
    singleAvailabilityZone
  } = resilienceAndRegionsSettings;

  if (resilienceType === ResilienceType.SINGLE_NODE) {
    const singleZone: Region | AvailabilityZone | undefined = singleAvailabilityZone
      ? regions[0]?.zones.find((region) => region.code === singleAvailabilityZone)
      : regions[0];
    if (singleZone && regions[0]) {
      return {
        [regions[0].code]: [
          {
            ...singleZone,
            nodeCount: 1,
            preffered: 0
          }
        ]
      };
    }
    return {};
  }

  // None and node-level resilience: only the first region and a single AZ (same placement shape).
  if (
    faultToleranceType === FaultToleranceType.NONE ||
    faultToleranceType === FaultToleranceType.NODE_LEVEL
  ) {
    const firstRegion = regions[0];
    const singleZone = firstRegion?.zones?.[0];
    if (!firstRegion || !singleZone) {
      return {};
    }
    const nodeCount =
      faultToleranceType === FaultToleranceType.NODE_LEVEL
        ? getFaultToleranceNeeded(resilienceFactor)
        : 1;
    return {
      [firstRegion.code]: [
        {
          ...singleZone,
          nodeCount,
          preffered: 0
        }
      ]
    };
  }

  const updatedRegions: NodeAvailabilityProps['availabilityZones'] = {};

  const faultToleranceNeeded = getFaultToleranceNeeded(resilienceFactor);

  if (faultToleranceType === FaultToleranceType.AZ_LEVEL) {
    // AZ-level defaults: split required AZs across regions (same remainder pattern as
    // getNodeCountNeeded), then spill forward when a region has fewer zones than its target.
    const selectedZonesByRegion: NodeAvailabilityProps['availabilityZones'] = {};
    const regionCount = regions.length;
    const N = faultToleranceNeeded;

    regions.forEach((region) => {
      selectedZonesByRegion[region.code] = [];
    });

    regions.forEach((region, index) => {
      const targetAzs = getNodeCountNeeded(N, regionCount, index);
      const toTake = Math.min(targetAzs, region.zones.length);
      for (let i = 0; i < toTake; i++) {
        selectedZonesByRegion[region.code].push({
          ...region.zones[i],
          nodeCount: 1, // placeholder; distributed below.
          preffered: i
        });
      }
    });

    let selectedAzCount = values(selectedZonesByRegion).reduce(
      (sum, zones) => sum + zones.length,
      0
    );

    while (selectedAzCount < N) {
      let progressed = false;
      for (const region of regions) {
        if (selectedAzCount >= N) break;
        const already = selectedZonesByRegion[region.code].length;
        if (already >= region.zones.length) continue;
        const zoneIndex = already;
        selectedZonesByRegion[region.code].push({
          ...region.zones[zoneIndex],
          nodeCount: 1,
          preffered: zoneIndex
        });
        selectedAzCount += 1;
        progressed = true;
        break;
      }
      if (!progressed) break;
    }

    if (selectedAzCount === 0) {
      return selectedZonesByRegion;
    }

    let nodesRemaining = faultToleranceNeeded;
    values(selectedZonesByRegion).forEach((zones) => {
      zones.forEach((zone) => {
        zone.nodeCount = 1;
        nodesRemaining -= 1;
      });
    });

    // Distribute any remaining nodes one-by-one in stable order.
    while (nodesRemaining > 0) {
      let assignedInPass = false;
      values(selectedZonesByRegion).forEach((zones) => {
        zones.forEach((zone) => {
          if (nodesRemaining <= 0) return;
          zone.nodeCount += 1;
          nodesRemaining -= 1;
          assignedInPass = true;
        });
      });
      if (!assignedInPass) break;
    }

    return selectedZonesByRegion;
  }

  values(regions).forEach((region, index) => {
    const nodeCount = getNodeCountNeeded(faultToleranceNeeded, regions.length, index);

    updatedRegions[region.code] = [];

    region.zones.forEach((zone, index) => {
      const nodesNeededForAZ = Math.floor(nodeCount / region.zones.length);

      const remainder = nodeCount % region.zones.length;

      const nodeCountForAz = nodesNeededForAZ + (index < remainder ? 1 : 0);

      if (nodeCountForAz === 0) return;

      updatedRegions[region.code].push({
        ...zone,
        nodeCount: nodeCountForAz,
        preffered: index
      });
    });
  });

  return updatedRegions;
};

export type ExpertNodesStepDefaultPlacement = {
  replicationFactor: number;
  availabilityZones: NodeAvailabilityProps['availabilityZones'];
};

const EXPERT_SINGLE_REGION_DEFAULT_RF = 3;
const EXPERT_DEFAULT_RFS = [3, 5, 7] as const;

function expertMultiRegionRfAndAzCount(regionCount: number): { rf: number; azCount: number } | null {
  const rf = EXPERT_DEFAULT_RFS.find((v) => v >= regionCount);
  if (rf === undefined) return null;
  const azCount = regionCount === 2 ? rf : regionCount;
  return { rf, azCount };
}

function pickZonesRoundRobin(
  regions: ResilienceAndRegionsProps[typeof REGIONS_FIELD],
  azCount: number
): NodeAvailabilityProps['availabilityZones'] {
  const result: NodeAvailabilityProps['availabilityZones'] = {};
  const usedSlots = new Map<string, number>();

  regions.forEach((r) => {
    result[r.code] = [];
    usedSlots.set(r.code, 0);
  });

  let preferred = 0;
  let added = 0;
  let rIdx = 0;
  const maxIter = Math.max(azCount * regions.length * 20, 100);
  let iter = 0;

  while (added < azCount && iter < maxIter) {
    iter += 1;
    const region = regions[rIdx % regions.length];
    rIdx += 1;
    const used = usedSlots.get(region.code) ?? 0;
    // Expert mode defaults should not pre-select AZ names; create empty AZ slots
    // and let users choose specific AZs.
    if (used >= region.zones.length) continue;
    usedSlots.set(region.code, used + 1);
    result[region.code].push({
      name: '',
      uuid: '',
      nodeCount: 1,
      preffered: preferred++
    });
    added += 1;
  }

  return result;
}

function distributeNodesUntilTotalAtLeastRf(
  availabilityZones: NodeAvailabilityProps['availabilityZones'],
  rf: number
): void {
  const entries: { code: string; zi: number }[] = [];
  Object.entries(availabilityZones).forEach(([code, zones]) => {
    zones.forEach((_, zi) => entries.push({ code, zi }));
  });
  if (entries.length === 0) return;

  let total = entries.reduce(
    (s, e) => s + availabilityZones[e.code][e.zi].nodeCount,
    0
  );
  let i = 0;
  const maxBoost = rf * entries.length + 20;
  let boost = 0;
  while (total < rf && boost < maxBoost) {
    const e = entries[i % entries.length];
    availabilityZones[e.code][e.zi].nodeCount += 1;
    total += 1;
    i += 1;
    boost += 1;
  }
}

/**
 * Decrements per-AZ node counts (never below 1) until total nodes are at most `rf`.
 * Mutates `availabilityZones`. Picks decrements from zones with the highest preferred rank
 * among zones with more than one node; tie-breaks by larger nodeCount, region code, then index.
 */
export function reduceExpertNodeCountsToAtMostRf(
  availabilityZones: NodeAvailabilityProps['availabilityZones'],
  rf: number
): void {
  let total = getNodeCount(availabilityZones);
  const maxIter = Math.max(total * 50, 100);
  let iter = 0;

  while (total > rf && iter < maxIter) {
    iter += 1;
    let best: {
      regionCode: string;
      zi: number;
      preferred: number;
      nodeCount: number;
    } | null = null;

    for (const regionCode of Object.keys(availabilityZones)) {
      const zones = availabilityZones[regionCode];
      if (!zones?.length) continue;
      for (let zi = 0; zi < zones.length; zi += 1) {
        const zone = zones[zi];
        const nc = zone.nodeCount;
        if (typeof nc !== 'number' || nc <= 1) {
          continue;
        }
        const preferred = typeof zone.preffered === 'number' ? zone.preffered : -1;

        if (!best) {
          best = { regionCode, zi, preferred, nodeCount: nc };
          continue;
        }
        if (preferred > best.preferred) {
          best = { regionCode, zi, preferred, nodeCount: nc };
          continue;
        }
        if (preferred < best.preferred) {
          continue;
        }
        if (nc > best.nodeCount) {
          best = { regionCode, zi, preferred, nodeCount: nc };
          continue;
        }
        if (nc < best.nodeCount) {
          continue;
        }
        if (regionCode > best.regionCode) {
          best = { regionCode, zi, preferred, nodeCount: nc };
          continue;
        }
        if (regionCode < best.regionCode) {
          continue;
        }
        if (zi > best.zi) {
          best = { regionCode, zi, preferred, nodeCount: nc };
        }
      }
    }

    if (!best) {
      break;
    }

    availabilityZones[best.regionCode][best.zi].nodeCount -= 1;
    total -= 1;
  }
}

/**
 * Expert-mode defaults when landing on Nodes & availability with no prior zone selection
 * Applies when fault tolerance is AZ-level or region-level; node-level and none use {@link assignRegionsAZNodeByReplicationFactor}.
 * Returns null when defaults do not apply.
 */
export function getExpertNodesStepDefaultPlacement(
  resilience: ResilienceAndRegionsProps
): ExpertNodesStepDefaultPlacement | null {
  if (resilience[RESILIENCE_FORM_MODE] !== ResilienceFormMode.EXPERT_MODE) {
    return null;
  }
  if (resilience[RESILIENCE_TYPE] !== ResilienceType.REGULAR) {
    return null;
  }
  const ft = resilience[FAULT_TOLERANCE_TYPE];
  if (ft !== FaultToleranceType.AZ_LEVEL && ft !== FaultToleranceType.REGION_LEVEL) {
    return null;
  }

  const regions = resilience[REGIONS_FIELD] ?? [];
  if (regions.length === 0) {
    return null;
  }

  if (regions.length === 1) {
    const region = regions[0];
    const zl = region.zones?.length ?? 0;
    if (zl === 0) {
      return null;
    }

    if (zl > 2) {
      const azToUse = Math.min(EXPERT_SINGLE_REGION_DEFAULT_RF, zl);
      const availabilityZones: NodeAvailabilityProps['availabilityZones'] = {
        [region.code]: region.zones.slice(0, azToUse).map((_, index) => ({
          name: '',
          uuid: '',
          nodeCount: 1,
          preffered: index
        }))
      };
      distributeNodesUntilTotalAtLeastRf(availabilityZones, EXPERT_SINGLE_REGION_DEFAULT_RF);
      return { replicationFactor: EXPERT_SINGLE_REGION_DEFAULT_RF, availabilityZones };
    }

    return {
      replicationFactor: EXPERT_SINGLE_REGION_DEFAULT_RF,
      availabilityZones: {
        [region.code]: [
          {
            name: '',
            uuid: '',
            nodeCount: EXPERT_SINGLE_REGION_DEFAULT_RF,
            preffered: 0
          }
        ]
      }
    };
  }

  const spec = expertMultiRegionRfAndAzCount(regions.length);
  if (!spec) {
    return null;
  }

  const availabilityZones = pickZonesRoundRobin(regions, spec.azCount);
  distributeNodesUntilTotalAtLeastRf(availabilityZones, spec.rf);

  return { replicationFactor: spec.rf, availabilityZones };
}

export const getPlacementRegions = (
  resilienceAndRegionsSettings: ResilienceAndRegionsProps,
  availabilityZones?: NodeAvailabilityProps['availabilityZones']
) => {
  const { resilienceType } = resilienceAndRegionsSettings;

  const azs =
    availabilityZones ?? assignRegionsAZNodeByReplicationFactor(resilienceAndRegionsSettings);

  // For single node, resilience factor should be 1 for the single AZ
  if (resilienceType === ResilienceType.SINGLE_NODE) {
    const region = resilienceAndRegionsSettings.regions[0];

    if (!region) {
      throw new Error(
        `Region with code ${resilienceAndRegionsSettings.singleAvailabilityZone} not found in resilience and regions settings`
      );
    }
    const az = find(region.zones, { code: resilienceAndRegionsSettings.singleAvailabilityZone });
    if (!az) {
      throw new Error(
        `AZ with code ${resilienceAndRegionsSettings.singleAvailabilityZone} not found in resilience and regions settings`
      );
    }
    return [
      {
        uuid: region.uuid,
        name: region.name,
        code: region.code,
        az_list: [
          {
            uuid: az.uuid,
            name: az!.name,
            num_nodes_in_az: 1,
            subnet: az!.subnet,
            leader_affinity: true,
            replication_factor: 1
          }
        ]
      }
    ];
  }

  const replicationFactorTotal =
    getEffectiveReplicationFactorForResilience(resilienceAndRegionsSettings);

  // Filter out AZs with 0 nodes first, then calculate replication factor distribution
  // This ensures we only distribute replicas across AZs that actually have nodes
  const azsWithNodes: NodeAvailabilityProps['availabilityZones'] = {};
  keys(azs).forEach((regionuuid) => {
    azsWithNodes[regionuuid] = azs[regionuuid].filter((az) => az.nodeCount > 0);
  });

  // Calculate total number of AZs with nodes across all regions
  const totalAZsWithNodes = Object.values(azsWithNodes).reduce(
    (sum, zones) => sum + zones.length,
    0
  );

  // Distribute replication factor across AZs that have nodes
  // Each AZ should get at least 1 replica if possible, then distribute remaining evenly
  // Ensure the sum of all AZ replication_factors equals the cluster replication_factor
  const baseReplicasPerAZ =
    totalAZsWithNodes > 0 ? Math.floor(replicationFactorTotal / totalAZsWithNodes) : 0;
  const extraReplicas =
    totalAZsWithNodes > 0 ? replicationFactorTotal % totalAZsWithNodes : 0;

  let replicaIndex = 0;
  const regionList: PlacementRegion[] = keys(azsWithNodes).map((regionuuid) => {
    const region = find(resilienceAndRegionsSettings.regions, { code: regionuuid });
    if (!region) {
      throw new Error(
        `Region with code ${regionuuid} not found in resilience and regions settings`
      );
    }
    return {
      uuid: region.uuid,
      name: region.name,
      code: region.code,
      az_list: azsWithNodes[regionuuid].map((az) => {
        const azFromRegion = find(region.zones, { uuid: az.uuid });
        // Calculate replication factor for this AZ
        // Distribute replicas: each AZ gets baseReplicasPerAZ, first extraReplicas AZs get one more
        // This ensures the sum equals the total replication_factor
        const azReplicationFactor = baseReplicasPerAZ + (replicaIndex < extraReplicas ? 1 : 0);
        replicaIndex++;

        return {
          uuid: az.uuid,
          name: azFromRegion!.name,
          num_nodes_in_az: az.nodeCount,
          subnet: azFromRegion!.subnet,
          leader_affinity: true,
          replication_factor: azReplicationFactor,
          ...(az.preffered !== undefined ? { leader_preference: az.preffered + 1 } : {})
        };
      })
    };
  });
  return regionList;
};

export const getAZCount = (availabilityZones: NodeAvailabilityProps['availabilityZones']) => {
  return values(availabilityZones).reduce((acc, zones) => acc + zones.length, 0);
};
