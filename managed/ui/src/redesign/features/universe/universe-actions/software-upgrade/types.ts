import { YbdbRelease } from './dtos';
import { UpgradeMethod, UpgradePace } from './constants';

export interface DBUpgradeFormFields {
  targetDbVersion: string;
  upgradeMethod: UpgradeMethod;
  upgradePace: UpgradePace;

  maxNodesPerBatch?: number;
  waitBetweenBatchesSeconds?: number;

  canaryUpgradeConfig?: CanaryUpgradeConfig;
}

export interface AzUpgradeStep {
  azUuid: string;
  displayName: string;
  displayNameWithoutRegion: string;
  pauseAfterTserverUpgrade: boolean;
}

export interface CanaryUpgradeConfig {
  pauseAfterMasters: boolean;
  primaryClusterAzOrder: string[];
  primaryClusterAzSteps: Record<string, AzUpgradeStep>;
  readReplicaClusterAzOrder: string[];
  readReplicaClusterAzSteps: Record<string, AzUpgradeStep>;
}

export interface PrecheckResponse {
  finalizeRequired: boolean;
  ysqlMajorVersionUpgrade?: boolean;
}

export interface ReleaseOption {
  /** Display label for autocomplete (YBAutoComplete expects this for getOptionLabel) */
  label: string;
  version: string;
  releaseInfo: YbdbRelease;
  series: string;
}
