import { getFlagFromRegion } from '@app/redesign/features-v2/universe/create-universe/helpers/RegionToFlagUtils';
import {
  getClusterPlacementRegions,
  getPrimaryCluster,
  getReadOnlyCluster
} from '@app/redesign/utils/universeUtils';
import type { ClusterSpec } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import type { UniverseSoftwareUpgradeReqBody } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

import { UpgradePace } from '../constants';
import type { AzUpgradeStep, CanaryUpgradeConfig, DBUpgradeFormFields } from '../types';

export interface FormattedAzPlacement {
  azUuid: string;
  displayName: string;
}

/**
 * Format cluster placement AZs for use in the DB upgrade form.
 */
const getFormattedClusterPlacementAzs = (cluster: ClusterSpec | null): FormattedAzPlacement[] => {
  if (!cluster) {
    return [];
  }
  const clusterPlacementRegions = getClusterPlacementRegions(cluster);
  return clusterPlacementRegions.flatMap((region) =>
    region.az_list?.map((az) => ({
      azUuid: az.uuid ?? '',
      displayName: `${getFlagFromRegion(region.code)} ${az.name ?? ''} (${region.name ?? ''})`
    }))
  );
};

/**
 * Build default canary upgrade config from v2 API cluster specs.
 */
export const getDefaultCanaryUpgradeConfig = (clusters: ClusterSpec[]): CanaryUpgradeConfig => {
  const primaryCluster = getPrimaryCluster(clusters);
  const readReplicaCluster = getReadOnlyCluster(clusters);

  const primaryClusterPlacementAzs = getFormattedClusterPlacementAzs(primaryCluster);
  const primaryClusterAzOrder = primaryClusterPlacementAzs.map((placementAz) => placementAz.azUuid);
  const primaryClusterAzSteps: Record<string, AzUpgradeStep> = {};
  primaryClusterPlacementAzs.forEach((placementAz, index) => {
    primaryClusterAzSteps[placementAz.azUuid] = {
      azUuid: placementAz.azUuid,
      displayName: placementAz.displayName,
      pauseAfterTserverUpgrade: index === 0
    };
  });

  const readReplicaClusterPlacementAzs = getFormattedClusterPlacementAzs(readReplicaCluster);
  const readReplicaClusterAzOrder = readReplicaClusterPlacementAzs.map(
    (placementAz) => placementAz.azUuid
  );
  const readReplicaClusterAzSteps: Record<string, AzUpgradeStep> = {};
  readReplicaClusterPlacementAzs.forEach((placementAz) => {
    readReplicaClusterAzSteps[placementAz.azUuid] = {
      azUuid: placementAz.azUuid,
      displayName: placementAz.displayName,
      pauseAfterTserverUpgrade: false
    };
  });
  return {
    pauseAfterMasters: false,
    primaryClusterAzOrder,
    primaryClusterAzSteps,
    readReplicaClusterAzOrder,
    readReplicaClusterAzSteps
  };
};

/**
 * Build the base software upgrade request body from form values.
 */
export const buildRequestPayload = (
  values: DBUpgradeFormFields
): UniverseSoftwareUpgradeReqBody => {
  return {
    version: values.targetDbVersion,
    allow_rollback: true,
    rolling_upgrade: values.upgradePace === UpgradePace.ROLLING,
    ...(values.upgradePace === UpgradePace.ROLLING && {
      roll_max_batch_size: {
        primary_batch_size: values.maxNodesPerBatch,
        read_replica_batch_size: values.maxNodesPerBatch
      },
      sleep_after_master_restart_millis: values.waitBetweenBatchesSeconds
        ? values.waitBetweenBatchesSeconds * 1000
        : undefined,
      sleep_after_tserver_restart_millis: values.waitBetweenBatchesSeconds
        ? values.waitBetweenBatchesSeconds * 1000
        : undefined
    })
  };
};

/**
 * Build the canary upgrade config object from form values.
 *
 * Used to set the canary upgrade field in the software upgrade request body.
 */
export const buildCanaryUpgradeConfig = (values: DBUpgradeFormFields) => {
  const config = values.canaryUpgradeConfig;
  if (!config) {
    return undefined;
  }
  const primarySteps = config.primaryClusterAzOrder.map((azUuid) => ({
    az_uuid: azUuid,
    pause_after_tserver_upgrade: config.primaryClusterAzSteps[azUuid].pauseAfterTserverUpgrade
  }));
  const readReplicaSteps = config.readReplicaClusterAzOrder.map((azUuid) => ({
    az_uuid: azUuid,
    pause_after_tserver_upgrade: config.readReplicaClusterAzSteps[azUuid].pauseAfterTserverUpgrade
  }));
  return {
    pause_after_masters: config.pauseAfterMasters,
    primary_cluster_az_steps: primarySteps,
    read_replica_cluster_az_steps: readReplicaSteps
  };
};
