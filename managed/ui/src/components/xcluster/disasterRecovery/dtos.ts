import { PitrConfig } from '../../../redesign/helpers/dtos';
import { XClusterConfigStatus } from '../constants';
import { XClusterConfig, XClusterTableDetails } from '../dtos';

/**
 * Models the data object provided from YBA API.
 *
 * Source: managed/src/main/java/com/yugabyte/yw/models/DrConfig.java
 */
export interface DrConfig {
  uuid: string;
  name: string;
  createTime: string;
  modifyTime: string;
  state: DrConfigState;

  // Replication Participants
  drReplicaUniverseActive: boolean;
  drReplicaUniverseState?: TargetUniverseDrState;
  drReplicaUniverseUuid?: string;
  primaryUniverseActive: boolean;
  primaryUniverseState?: SourceUniverseDrState;
  primaryUniverseUuid?: string;

  // Replication Fields
  paused: boolean;
  pitrConfigs: PitrConfig[];
  replicationGroupName: string;
  status: XClusterConfigStatus;
  tableDetails: XClusterTableDetails[];
  tables: string[];
  xclusterConfigUuid: string;
  xclusterConfigsUuid: string[]; // Internal API field for now.
}

// ---------------------------------------------------------------------------
// DR Config States
//
// Matches the Java enums returned from YBA API.
// Source: managed/src/main/java/com/yugabyte/yw/common/DrConfigStates.java
// ---------------------------------------------------------------------------

/**
 * It is important to note that the state of the underlying xCluster config (XClusterConfigState)
 * tracks additional information not captured in the DrConfigState.
 * (ex. Participating universe is deleted, Failed to delete xCluster config, etc.)
 *
 * Any change to the object values must also be made the respective i18n key in
 * src/translations/en.json.
 *
 * Source: managed/src/main/java/com/yugabyte/yw/common/DrConfigStates.java
 */
export const DrConfigState = {
  INITIALIZING: 'Initializing',
  REPLICATING: 'Replicating',
  SWITCHOVER_IN_PROGRESS: 'Switchover in Progress',
  FAILOVER_IN_PROGRESS: 'Failover in Progress',
  HALTED: 'Halted'
} as const;
export type DrConfigState = typeof DrConfigState[keyof typeof DrConfigState];

/**
 * Any change to the object values must also be made the respective i18n key in
 * src/translations/en.json.
 *
 * Source: managed/src/main/java/com/yugabyte/yw/common/DrConfigStates.java
 */
export const SourceUniverseDrState = {
  UNCONFIGURED: 'Unconfigured for DR',
  READY_TO_REPLICATE: 'Ready to replicate',
  WAITING_FOR_DR: 'Waiting for DR',
  REPLICATING_DATA: 'Replicating data',
  PREPARING_SWITCHOVER: 'Preparing for switchover',
  SWITCHING_TO_DR_REPLICA: 'Switching to DR replica',
  DR_FAILED: 'Universe marked as DR failed'
} as const;
export type SourceUniverseDrState = typeof SourceUniverseDrState[keyof typeof SourceUniverseDrState];

/**
 * Any change to the object values must also be made the respective i18n key in
 * src/translations/en.json.
 *
 * Source: managed/src/main/java/com/yugabyte/yw/common/DrConfigStates.java
 */
export const TargetUniverseDrState = {
  UNCONFIGURED: 'Unconfigured for DR',
  BOOTSTRAPPING: 'Bootstrapping',
  RECEIVING_DATA: 'Receiving data, Ready for reads',
  SWITCHING_TO_DR_PRIMARY: 'Switching to DR primary',
  DR_FAILED: 'Universe marked as DR failed'
} as const;
export type TargetUniverseDrState = typeof TargetUniverseDrState[keyof typeof TargetUniverseDrState];
// ---------------------------------------------------------------------------

/**
 * Contains the current safetime values (safetime, safetime lag, safetime skew) for
 * each namespace in the DR config.
 *
 * Source: managed/src/main/java/com/yugabyte/yw/forms/DrConfigSafetimeResp.java
 */
export interface DrConfigSafetimeResponse {
  safetimes: {
    namespaceId: string;
    namespaceName: string;
    safetimeEpochUs: number;
    safetimeLagUs: number;
    safetimeSkewUs: number;
  }[];
}
