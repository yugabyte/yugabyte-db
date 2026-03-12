import { Cluster, RollMaxBatchSize } from '@app/redesign/features/universe/universe-form/utils/dto';
import { UPGRADE_TYPE } from '@app/redesign/features/universe/universe-actions/rollback-upgrade/utils/types';
import { YbdbRelease } from './dtos';
import { UpgradeMethod, UpgradePace } from './constants';

export interface DBUpgradeFormFields {
  targetDbVersion: string;
  upgradeMethod: UpgradeMethod;
  upgradePace: UpgradePace;

  maxNodesPerBatch?: number;
  waitBetweenBatchesSeconds?: number;
}

export interface DBUpgradePayload {
  ybSoftwareVersion: string;
  sleepAfterMasterRestartMillis: number;
  sleepAfterTServerRestartMillis: number;
  universeUUID: string;
  taskType: string;
  upgradeOption: UPGRADE_TYPE;
  clusters: Cluster[];
  nodePrefix: string;
  enableYbc: boolean;
  rollMaxBatchSize?: RollMaxBatchSize;
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
