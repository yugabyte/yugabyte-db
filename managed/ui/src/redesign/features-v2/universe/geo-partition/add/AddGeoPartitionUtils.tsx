import { useCallback, useContext, useMemo } from 'react';
import { find, keys } from 'lodash';
import {
  AddGeoPartitionContext,
  AddGeoPartitionContextMethods,
  AddGeoPartitionContextProps,
  AddGeoPartitionSteps
} from './AddGeoPartitionContext';
import { Step } from '@yugabyte-ui-library/core/dist/esm/components/YBMultiLevelStepper/YBMultiLevelStepper';
import {
  ClusterPartitionSpec,
  ClusterSpecClusterType,
  PlacementAZ,
  PlacementRegion,
  UniverseRespResponse
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { ResilienceAndRegionsProps } from '../../create-universe/steps/resilence-regions/dtos';
import { Region } from '@app/redesign/helpers/dtos';

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
                title: 'Resilience and Regions'
              },
              {
                title: 'Nodes and Availability Zones'
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

export const extractRegionsAndNodeDataFromUniverse = (
  universeData: UniverseRespResponse,
  providerRegions: Region[]
): RegionsAndNodesFormType => {
  const regions: RegionsAndNodesFormType['regions'] = [];

  universeData.spec?.clusters.forEach((cluster) => {
    cluster.provider_spec.region_list?.forEach((region) => {
      const regionData = providerRegions.find((r) => r.uuid === region);
      const azs = cluster.placement_spec?.cloud_list
        .map((cloud) => cloud.region_list)
        .flat()
        .find((r) => r?.uuid === region)?.az_list;

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

  const providerUUID = universeData?.spec?.clusters[0].provider_spec.provider;

  if (!providerUUID) {
    throw new Error('Provider UUID is missing in universe data');
  }
  if (geoPartitions.length) {
    return geoPartitions.map((gp, index) => {
      if (!gp.resilience) {
        throw new Error(`Resilience data is missing in geo partition ${gp.name}`);
      }
      const azs = gp.nodesAndAvailability?.availabilityZones;
      if (!azs) throw new Error(`Availability zones data is missing in geo partition ${gp.name}`);
      const primaryCluster = universeData?.spec?.clusters.find(
        (c) => c.cluster_type === ClusterSpecClusterType.PRIMARY
      );
      const regionList: PlacementRegion[] = keys(azs).map((regionuuid) => {
        const region = find(gp.resilience?.regions, { code: regionuuid });
        if (!region) {
          throw new Error(
            `Region with code ${regionuuid} not found in resilience and regions settings`
          );
        }
        return {
          uuid: region.uuid,
          name: region.name,
          code: region.code,
          az_list: azs[regionuuid].map((az) => {
            const azFromRegion = find(region.zones, { uuid: az.uuid });
            return {
              uuid: az.uuid,
              name: azFromRegion!.name,
              num_nodes_in_az: az.nodeCount,
              subnet: azFromRegion!.subnet,
              leader_affinity: true,
              replication_factor: gp.resilience?.replicationFactor
            };
          })
        };
      });

      return {
        name: gp.name,
        default_partition: isNewGeoPartition && index === 0,
        replication_factor:
          isNewGeoPartition && index === 0
            ? universeData!.spec!.clusters[0].replication_factor!
            : gp.resilience.replicationFactor,
        // if the universe doesn't have a default geo_parition then send the regions list in the new default geo partition
        ...(isNewGeoPartition && index === 0
          ? { placement: primaryCluster!.placement_spec! }
          : {
              placement: {
                cloud_list: [
                  {
                    uuid: providerUUID,
                    code: primaryCluster!.placement_spec!.cloud_list[0].code!,
                    region_list: regionList
                  }
                ]
              }
            })
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
