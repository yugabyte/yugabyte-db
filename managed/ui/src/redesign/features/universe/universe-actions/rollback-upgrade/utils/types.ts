import { Cluster, Universe } from '../../../universe-form/utils/dto';

export enum UPGRADE_TYPE {
  ROLLING = 'Rolling',
  NON_ROLLING = 'Non-Rolling'
}

export interface DBUpgradeFormFields {
  softwareVersion: string | null;
  rollingUpgrade: boolean;
  timeDelay: Number;
}

export interface DBUpgradePayload {
  ybSoftwareVersion: string;
  sleepAfterMasterRestartMillis: Number;
  sleepAfterTServerRestartMillis: Number;
  universeUUID: string;
  taskType: string;
  upgradeOption: UPGRADE_TYPE;
  clusters: Cluster[];
  nodePrefix: string;
}

export interface DBRollbackFormFields {
  rollingUpgrade: boolean;
  timeDelay: Number;
}

export interface DBRollbackPayload {
  upgradeOption: UPGRADE_TYPE;
  sleepAfterMasterRestartMillis: Number;
  sleepAfterTServerRestartMillis: Number;
}

export interface GetInfoPayload {
  ybSoftwareVersion: string;
}

export interface GetInfoResponse {
  requireFinalize: boolean;
  affectedXClsuterUniversesList?: Universe[];
}

export interface TaskObject {
  abortable: boolean;
  completionTime: string;
  correlationId: string;
  createTime: string;
  id: string;
  percentComplete: 10;
  retryable: true;
  status: string;
  target: string;
  targetUUID: string;
  title: string;
  type: string;
  typeName: string;
  userEmail: string;
}
