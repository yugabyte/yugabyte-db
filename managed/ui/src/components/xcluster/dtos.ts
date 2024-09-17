import { XClusterConfigStatus, XClusterConfigType, XClusterTableStatus } from './constants';
import { PitrConfig, YBTable } from '../../redesign/helpers/dtos';
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
  replicationStatusErrors: string[];

  sourceTableInfo?: YBTable;
  targetTableInfo?: YBTable;
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

  // `imported` is dropped from the model defined in XClusterConfig.java.
  // This is intended for backend usage and API users shouldn't need to use this field.
}

/**
 * Source: managed/src/main/java/com/yugabyte/yw/forms/XClusterConfigNeedBootstrapPerTableResponse.java
 */
export const XClusterNeedBootstrapReason = {
  TABLE_MISSING_ON_TARGET: 'TABLE_MISSING_ON_TARGET',
  TABLE_HAS_DATA: 'TABLE_HAS_DATA',
  BIDIRECTIONAL_REPLICATION: 'BIDIRECTIONAL_REPLICATION'
} as const;
export type XClusterNeedBootstrapReason = typeof XClusterNeedBootstrapReason[keyof typeof XClusterNeedBootstrapReason];

/**
 * Source: managed/src/main/java/com/yugabyte/yw/forms/XClusterConfigNeedBootstrapPerTableResponse.java
 */
export interface XClusterConfigNeedBootstrapPerTableResponse {
  [tableUuid: string]: {
    description: string;
    reasons: XClusterNeedBootstrapReason[];
    bootstrapRequired: boolean;
  };
}

/**
 * Source: managed/src/main/java/com/yugabyte/yw/controllers/XClusterConfigController.java
 */
export interface XClusterConfigNeedBootstrapPerTableSimpleResponse {
  [tableUuid: string]: boolean;
}
