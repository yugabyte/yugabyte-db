import { useCallback, useContext, useMemo } from 'react';
import {
  AddGeoPartitionContext,
  AddGeoPartitionContextMethods,
  AddGeoPartitionContextProps,
  AddGeoPartitionSteps
} from './AddGeoPartitionContext';
import { Step } from '@yugabyte-ui-library/core/dist/esm/components/YBMultiLevelStepper/YBMultiLevelStepper';
import {
  ClusterPartitionSpec,
  ClusterSpec,
  ClusterSpecClusterType,
  PlacementAZ,
  UniverseInfo,
  UniverseRespResponse
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { NodeAvailabilityProps } from '../../create-universe/steps/nodes-availability/dtos';
import { ResilienceAndRegionsProps } from '../../create-universe/steps/resilence-regions/dtos';
import { Region } from '@app/redesign/helpers/dtos';
import {
  getEffectiveReplicationFactorForResilience,
  getPlacementRegions
} from '../../create-universe/CreateUniverseUtils';
import { sanitizeClusters } from '../../read-replica/add/buildUniverseSpecForReadReplicaPricing';

export function navigateToUniverseSettingsFromWizard(
  universeData?: UniverseRespResponse
): void {
  const uuid = universeData?.info?.universe_uuid;
  if (uuid) {
    window.location.href = `/universes/${uuid}/settings`;
  }
}

export function useGeoPartitionNavigation() {
  const [addGeoPartitionContext, addGeoPartitionMethods] = (useContext(
    AddGeoPartitionContext
  ) as unknown) as AddGeoPartitionContextMethods;

  const { activeStep, geoPartitions, activeGeoPartitionIndex } = addGeoPartitionContext;
  const { setGeoPartitionContext } = addGeoPartitionMethods;
  const steps = useGetSteps(addGeoPartitionContext);
  const prev = useCallback(() => {
    const MAX_STEP_COUNT = steps[Math.max(0, activeGeoPartitionIndex - 1)].subSteps.length;
    const context = {
      ...addGeoPartitionContext
    };
    if (activeStep === AddGeoPartitionSteps.REVIEW) {
      context.activeStep = MAX_STEP_COUNT;
      context.activeGeoPartitionIndex = geoPartitions.length - 1;
      return setGeoPartitionContext(context);
    }
    if (activeStep === AddGeoPartitionSteps.GENERAL_SETTINGS && activeGeoPartitionIndex !== 0) {
      context.activeStep = MAX_STEP_COUNT;
      context.activeGeoPartitionIndex = activeGeoPartitionIndex - 1;
    } else {
      context.activeStep = Math.max(activeStep - 1, 1);
    }
    return setGeoPartitionContext(context);
  }, [addGeoPartitionContext]);

  const next = (ctx: AddGeoPartitionContextProps) => {
    const MAX_STEP_COUNT = steps[activeGeoPartitionIndex].subSteps.length;
    const context = {
      ...ctx
    };

    if (activeStep < MAX_STEP_COUNT) {
      context.activeStep = activeStep + 1;
    } else if (activeStep === MAX_STEP_COUNT) {
      if (activeGeoPartitionIndex < geoPartitions.length - 1) {
        context.activeGeoPartitionIndex = activeGeoPartitionIndex + 1;
        context.activeStep = 1;
      } else {
        context.activeGeoPartitionIndex = geoPartitions.length;
        context.activeStep = AddGeoPartitionSteps.REVIEW;
      }
    }
    return setGeoPartitionContext(context);
  };

  return { moveToPreviousPage: prev, moveToNextPage: next };
}

export function useGetSteps(context: AddGeoPartitionContextProps): Step[] {
  const { geoPartitions, activeGeoPartitionIndex, isNewGeoPartition } = context;

  return useMemo(() => {
    const steps: Step[] = geoPartitions.map((geoPartition, index) => ({
      groupTitle: geoPartition.name,
      subSteps: [
        {
          title: 'General Settings'
        },
        ...(index !== 0 || !isNewGeoPartition
          ? [
              {
                title: 'Regions'
              },
              {
                title: 'Availability Zones and Nodes'
              }
            ]
          : [])
      ]
    }));

    return [...steps, { groupTitle: 'Review', subSteps: [{ title: 'Summary and Cost' }] }];
  }, [geoPartitions, activeGeoPartitionIndex, isNewGeoPartition]);
}

export type RegionsAndNodesFormType = {
  regions: (ResilienceAndRegionsProps['regions'][number] & { zones: PlacementAZ[] } & {
    clusterType: ClusterSpecClusterType;
    partitionUUID?: string;
  })[];
};

/**
 * Primary cluster's first partition when it is the API default (`name === 'default'`).
 * Used to seed the add-geo-partition wizard from universe data.
 */
export const getDefaultPrimaryPartitionSpec = (
  universeData: UniverseRespResponse
): ClusterPartitionSpec | null => {
  const primaryCluster = universeData.spec?.clusters?.find(
    (c) => c.cluster_type === ClusterSpecClusterType.PRIMARY
  );
  const spec = primaryCluster?.partitions_spec;
  if (!spec?.length || spec[0].name !== 'default') {
    return null;
  }
  return spec[0];
};

export const isClusterGeoPartitioned = (info: UniverseInfo, cluster: ClusterSpec): boolean => {
  return (info.clusters?.find((c) => c.uuid === cluster.uuid) as any)?.geo_partitioned ?? false;
};

/**
 * Whether the add-geo-partition wizard should treat the first row as the "new default" flow
 * (read-only placement on first row, skip Regions/AZ sub-steps for index 0).
 *
 * True when the primary cluster is not yet geo-partitioned, or when geo_partitioned is set but
 * the primary still has at most one partition row (stale/edge case). False once geo_partitioned
 * is true and there is more than one partition on the primary cluster.
 */
export function computeIsNewGeoPartitionFromUniverse(
  universeData: UniverseRespResponse | undefined | null
): boolean {
  if (!universeData?.info || !universeData.spec?.clusters?.length) {
    return false;
  }
  const primaryCluster = universeData.spec.clusters.find(
    (c) => c.cluster_type === ClusterSpecClusterType.PRIMARY
  );
  if (!primaryCluster) {
    return false;
  }
  const geoPartitioned = isClusterGeoPartitioned(universeData.info, primaryCluster);
  const partitionCount = primaryCluster.partitions_spec?.length ?? 0;
  return !geoPartitioned || partitionCount <= 1;
}

export const extractRegionsFromPartitionPlacement = (
  partition: ClusterPartitionSpec,
  providerRegions: Region[]
): RegionsAndNodesFormType['regions'] => {
  const regions: RegionsAndNodesFormType['regions'] = [];

  partition.placement?.cloud_list?.forEach((cloud) => {
    cloud.region_list?.forEach((region) => {
      const regionData = providerRegions.find((r) => r.uuid === region.uuid);
      if (!regionData) return;
      regions.push({
        ...regionData,
        clusterType: ClusterSpecClusterType.PRIMARY,
        partitionUUID: partition.uuid,
        zones:
          region.az_list?.map((zone) => {
            const azData = regionData.zones.find((az) => az.uuid === zone.uuid);
            return {
              ...zone,
              ...azData
            };
          }) ?? []
      } as any);
    });
  });

  return regions;
};

export const extractRegionsAndNodeDataFromUniverse = (
  universeData: UniverseRespResponse,
  providerRegions: Region[]
): RegionsAndNodesFormType => {
  const regions: RegionsAndNodesFormType['regions'] = [];

  universeData.spec?.clusters.forEach((cluster) => {
    cluster.placement_spec?.cloud_list[0].region_list?.forEach((region) => {
      const regionData = providerRegions.find((r) => r.uuid === region.uuid);
      if (!regionData) return;
      const azs = region?.az_list;

      if (regionData) {
        regions.push({
          ...regionData,
          clusterType: cluster.cluster_type,
          zones: azs?.map((zone, index) => {
            const azData = regionData.zones.find((az) => az.uuid === zone.uuid);
            return {
              ...zone,
              ...azData
            };
          })
        } as any);
      }
    });
  });

  return { regions };
};

/**
 * True when the nodes-availability step has region keys (user-configured AZ layout).
 * Default geo-partition row without Regions/AZ steps keeps `{}` and uses primary placement.
 */
export function hasConfiguredAvailabilityZones(
  azs: NodeAvailabilityProps['availabilityZones'] | undefined
): boolean {
  return !!azs && Object.keys(azs).length > 0;
}

/** Sum of num_nodes_in_az for one partition placement (matches nodes-availability configuration). */
export function sumNumNodesInClusterPartitionPlacement(spec: ClusterPartitionSpec): number {
  let sum = 0;
  const clouds = spec.placement?.cloud_list ?? [];
  for (const cloud of clouds) {
    for (const region of cloud.region_list ?? []) {
      for (const az of region.az_list ?? []) {
        sum += az.num_nodes_in_az ?? 0;
      }
    }
  }
  return sum;
}

/**
 * Universe spec passed to getUniverseResources for one geo-partition row.
 *
 * Uses partitions_spec containing only that partition. The backend's
 * PlacementInfoUtil.validatePartitions forbids the same AZ UUID in more than one partition on the
 * cluster; merging the priced row into the full universe partitions_spec duplicates zones whenever
 * the new partition reuses an AZ from default (common in the wizard), which triggers
 * "Found duplicate zone ... for partition".
 *
 * Only the primary cluster is updated. Copying this placement onto read-replica clusters makes
 * configure() assign the same node counts to each cluster; UniverseResourceDetails then sums
 * userIntent.numNodes across all clusters and overstates num_nodes.
 */
export function buildUniverseSpecForGeoPartitionPricing(
  universeData: UniverseRespResponse,
  geoPartitionSpec: ClusterPartitionSpec
) {
  if (!universeData?.spec) return undefined;

  const primaryCluster = universeData.spec.clusters.find(
    (c) => c.cluster_type === ClusterSpecClusterType.PRIMARY
  );
  if (!primaryCluster) return undefined;

  const partitionsForPricing: ClusterPartitionSpec[] = [{ ...geoPartitionSpec }];

  return {
    spec: {
      ...universeData.spec,
      clusters: sanitizeClusters(universeData.spec.clusters).map((cluster) =>
        cluster.cluster_type === ClusterSpecClusterType.PRIMARY
          ? {
              ...cluster,
              placement_spec: geoPartitionSpec.placement,
              partitions_spec: partitionsForPricing
            }
          : cluster
      )
    },
    arch: universeData.info?.arch
  };
}

export const extractGeoPartitionsFromUniverse = (
  universeData: UniverseRespResponse,
  providerRegions: Region[]
): RegionsAndNodesFormType => {
  const geoPartitions: RegionsAndNodesFormType['regions'] = [];

  const partitions = universeData.spec?.clusters
    .map((cluster) => cluster.partitions_spec ?? [])
    .flat();

  partitions?.forEach((partition) => {
    partition.placement.cloud_list.forEach((cloud) => {
      cloud.region_list?.forEach((region) => {
        const regionData = providerRegions.find((r) => r.uuid === region.uuid);
        if (regionData) {
          geoPartitions.push({
            ...regionData,
            paritition_name: partition.name,
            clusterType: ClusterSpecClusterType.PRIMARY,
            partitionUUID: partition.uuid,
            zones:
              region.az_list?.map((zone) => {
                const azData = regionData.zones.find((az) => az.uuid === zone.uuid);
                return {
                  ...zone,
                  ...azData
                };
              }) ?? []
          } as any);
        }
      });
    });
  });
  return { regions: geoPartitions };
};

export const prepareAddGeoPartitionPayload = (
  addGeoPartitionContext: AddGeoPartitionContextProps
): ClusterPartitionSpec[] => {
  const { geoPartitions, universeData, isNewGeoPartition } = addGeoPartitionContext;

  if (!universeData?.spec?.clusters?.length) {
    throw new Error('Universe cluster data is missing');
  }

  const primaryCluster = universeData.spec.clusters.find(
    (c) => c.cluster_type === ClusterSpecClusterType.PRIMARY
  );
  if (!primaryCluster) {
    throw new Error('Primary cluster not found in universe data');
  }

  const providerUUID = primaryCluster.provider_spec?.provider;
  if (!providerUUID) {
    throw new Error('Provider UUID is missing in universe data');
  }

  const primaryPlacementCloud = primaryCluster.placement_spec?.cloud_list?.[0];
  if (!primaryPlacementCloud?.code) {
    throw new Error('Primary cluster placement cloud is missing in universe data');
  }

  // Preserve the existing default partition's uuid when the wizard is converting a
  // non-geo-partitioned universe (payload[0] replaces the existing default rather than appends).
  const existingDefaultPartition = getDefaultPrimaryPartitionSpec(universeData);

  if (geoPartitions.length) {
    return geoPartitions.map((gp, index) => {
      if (!gp.resilience) {
        throw new Error(`Resilience data is missing in geo partition ${gp.name}`);
      }
      const azs = gp.nodesAndAvailability?.availabilityZones;
      if (!azs) throw new Error(`Availability zones data is missing in geo partition ${gp.name}`);

      const isDefaultNewPartition = isNewGeoPartition && index === 0;

      const base: Pick<
        ClusterPartitionSpec,
        'uuid' | 'name' | 'tablespace_name' | 'default_partition' | 'replication_factor'
      > = {
        ...(isDefaultNewPartition && existingDefaultPartition?.uuid
          ? { uuid: existingDefaultPartition.uuid }
          : {}),
        name: gp.name,
        tablespace_name: gp.tablespaceName,
        default_partition: isDefaultNewPartition,
        replication_factor: isDefaultNewPartition
          ? primaryCluster.replication_factor!
          : getEffectiveReplicationFactorForResilience(gp.resilience)
      };

      // New default partition mirrors primary cluster placement unless AZs were configured in the wizard.
      if (isDefaultNewPartition) {
        if (hasConfiguredAvailabilityZones(azs)) {
          const regionList = getPlacementRegions(gp.resilience, azs);
          return {
            ...base,
            placement: {
              cloud_list: [
                {
                  uuid: providerUUID,
                  code: primaryPlacementCloud.code,
                  default_region: regionList[0]?.uuid,
                  region_list: regionList
                }
              ]
            }
          };
        }
        return {
          ...base,
          placement: primaryCluster.placement_spec!
        };
      }

      // Match create-universe: per-AZ replication_factor sums to partition RF, drop 0-node AZs,
      // and map leader_preference from preffered.
      const regionList = getPlacementRegions(gp.resilience, azs);

      return {
        ...base,
        placement: {
          cloud_list: [
            {
              uuid: providerUUID,
              code: primaryPlacementCloud.code,
              default_region: regionList[0]?.uuid,
              region_list: regionList
            }
          ]
        }
      };
    });
  }
  return [];
};

export const getExistingGeoPartitions = (
  universeData: UniverseRespResponse
): ClusterPartitionSpec[] => {
  const geoPartitions = universeData.spec?.clusters
    .map((cluster) => cluster.partitions_spec ?? [])
    .flat();
  return geoPartitions ?? [];
};

export function getNextGeoPartitionDisplayNumber(
  isNewGeoPartition: boolean,
  alreadyExistingCount: number,
  currentWizardPartitionCount: number
): number {
  return isNewGeoPartition
    ? alreadyExistingCount + currentWizardPartitionCount
    : alreadyExistingCount + currentWizardPartitionCount + 1;
}
