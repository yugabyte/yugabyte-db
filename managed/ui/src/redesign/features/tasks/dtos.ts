/*
 * Created on Wed Dec 20 2023
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

// UserTaskDetails.java
export interface TaskDetails {
  title: string;
  description: string;
  state: TaskState;
  extraDetails: any[];
}

// CustomerTaskFormData.java

export const TaskState = {
  CREATED: 'Created',
  INITIALIZING: 'Initializing',
  RUNNING: 'Running',
  UNKNOWN: 'Unknown',
  SUCCESS: 'Success',
  FAILURE: 'Failure',
  ABORTED: 'Aborted',
  ABORT: 'Abort',
  PAUSED: 'Paused'
} as const;
export type TaskState = (typeof TaskState)[keyof typeof TaskState];

export const TaskType = {
  GFlags_UPGRADE: 'GFlagsUpgrade',
  EDIT: 'Update',
  SOFTWARE_UPGRADE: 'SoftwareUpgrade',
  ROLLBACK_UPGRADE: 'RollbackUpgrade',
  FINALIZE_UPGRADE: 'FinalizeUpgrade',
  RESIZE_NODE: 'ResizeNode',
  RESTORE_YBA_BACKUP: 'RestoreYbaBackup'
};
export const TargetType = {
  UNIVERSE: 'Universe',
  BACKUP: 'Backup',
  GFlags: 'GFlags'
};

export const ServerType = {
  MASTER: 'MASTER',
  TSERVER: 'TSERVER'
} as const;
export type ServerType = (typeof ServerType)[keyof typeof ServerType];

export const AZUpgradeStatus = {
  NOT_STARTED: 'NOT_STARTED',
  IN_PROGRESS: 'IN_PROGRESS',
  COMPLETED: 'COMPLETED',
  FAILED: 'FAILED'
} as const;
export type AZUpgradeStatus = (typeof AZUpgradeStatus)[keyof typeof AZUpgradeStatus];

export const CanaryPauseState = {
  NOT_PAUSED: 'NOT_PAUSED',
  PAUSED_AFTER_MASTERS: 'PAUSED_AFTER_MASTERS',
  PAUSED_AFTER_TSERVERS_AZ: 'PAUSED_AFTER_TSERVERS_AZ'
} as const;
export type CanaryPauseState = (typeof CanaryPauseState)[keyof typeof CanaryPauseState];

export interface AZUpgradeState {
  azUUID: string;
  azName: string;
  serverType: ServerType;
  clusterUUID: string;
  status: AZUpgradeStatus;
}

export const DbUpgradePrecheckStatus = {
  SUCCESS: 'success',
  RUNNING: 'running',
  FAILED: 'failed'
} as const;
export type DbUpgradePrecheckStatus =
  (typeof DbUpgradePrecheckStatus)[keyof typeof DbUpgradePrecheckStatus];
export interface SoftwareUpgradeProgress {
  canaryUpgrade: boolean;
  canaryPauseState: CanaryPauseState | null;
  precheckStatus: DbUpgradePrecheckStatus;
  masterAZUpgradeStatesList: AZUpgradeState[];
  tserverAZUpgradeStatesList: AZUpgradeState[];
}

export interface Task {
  id: string;
  title: string;
  percentComplete: number;
  createTime: string;
  completionTime: string;
  target: typeof TargetType & string;
  targetUUID: string;
  type: typeof TaskType & string;
  typeName: string;
  status: TaskState;
  details: {
    taskDetails: TaskDetails[];
    /** Present for universe software-upgrade tasks when the backend exposes per-AZ / canary progress. */
    softwareUpgradeProgress?: SoftwareUpgradeProgress | null;
    versionNumbers?: {
      ybPrevSoftwareVersion?: string;
      ybSoftwareVersion?: string;
    };
  };
  abortable: boolean;
  retryable: boolean;
  canRollback: boolean;
  correlationId: string;
  userEmail: string;
  subtaskInfos: SubTaskInfo[];
  taskInfo: {
    taskParams: {
      previousTaskUUID?: string;
    };
  };
}

export interface FailedTask {
  failedSubTasks: {
    errorString: string;
    subTaskGroupType: string;
    subTaskState: TaskState;
    subTaskType: string;
    subTaskUUID: string;
  }[];
}

export interface SubTaskInfo {
  uuid: string;
  parentUuid: Task['id'];
  taskType: string;
  taskState: TaskState;
  subTaskGroupType: Task['type'];
  createTime: number;
  updateTime: number;
  percentDone: number;
  position: number;
  details?: {
    error?: {
      code: string;
      message: string;
      originMessage: string;
    };
  };
  taskParams?: {
    nodeName?: string;
    nodeDetailsSet?: {
      nodeName?: string;
    }[];
  };
}

export type SubTaskDetailsResp = {
  [key: string]: Task[];
};
