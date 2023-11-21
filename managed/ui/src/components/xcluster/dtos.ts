import { XClusterConfigStatus, XClusterConfigType, XClusterTableStatus } from './constants';
import { PitrConfig } from '../../redesign/helpers/dtos';
import { SourceUniverseDrState, TargetUniverseDrState } from './disasterRecovery/dtos';

/**
 * Source: managed/src/main/java/com/yugabyte/yw/models/XClusterTableConfig.java
 */
export interface XClusterTableDetails {
  indexTable: boolean;
  needBootstrap: boolean;
  replicationSetupDone: true;
  status: XClusterTableStatus;
  streamId: string;
  tableId: string;

  bootstrapCreateTime?: string;
  restoreTime?: string;
}

/**
 * Models the data object provided from YBA API.
 *
 * Source: managed/src/main/java/com/yugabyte/yw/models/XClusterConfig.java
 */
export interface XClusterConfig {
  createTime: string;
  imported: boolean;
  modifyTime: string;
  name: string;
  paused: boolean;
  pitrConfigs: PitrConfig[];
  replicationGroupName: string;
  sourceActive: boolean;
  status: XClusterConfigStatus;
  tableDetails: XClusterTableDetails[];
  tableType: 'UNKNOWN' | 'YSQL' | 'YCQL';
  tables: string[];
  targetActive: boolean;
  type: XClusterConfigType;
  usedForDr: boolean;
  uuid: string;

  // The source/target universe may be undefined if they are deleted.
  sourceUniverseState?: SourceUniverseDrState;
  sourceUniverseUUID?: string;
  targetUniverseState?: TargetUniverseDrState;
  targetUniverseUUID?: string;

  txnTableDetails?: XClusterTableDetails;
}
