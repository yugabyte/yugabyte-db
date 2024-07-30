/*
 * Created on Wed Dec 20 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

// UserTaskDetails.java
export interface TaskDetails {
  title: string;
  description: string;
  state: TaskStates;
  extraDetails: any[];
}

// CustomerTaskFormData.java

export enum TaskStates {
  CREATED = 'Created',
  INITIALIZING = 'Initializing',
  RUNNING = 'Running',
  UNKNOWN = 'Unknown',
  SUCCESS = 'Success',
  FAILURE = 'Failure',
  ABORTED = 'Aborted',
  ABORT = 'Abort'
}

export const TaskType = {
  GFlags_UPGRADE: 'GFlagsUpgrade',
  EDIT: 'Update',
  SOFTWARE_UPGRADE: 'SoftwareUpgrade'
};
export const TargetType = {
  UNIVERSE: 'Universe',
  BACKUP: 'Backup',
  GFlags: 'GFlags'
};
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
  status: TaskStates;
  details: {
    taskDetails: TaskDetails[];
    versionNumbers?: {
      ybPrevSoftwareVersion?: string;
      ybSoftwareVersion?: string;
    };
  };
  abortable: boolean;
  retryable: boolean;
  correlationId: string;
  userEmail: string;
}

export interface FailedTask {
  failedSubTasks: {
    errorString: string;
    subTaskGroupType: string;
    subTaskState: TaskStates;
    subTaskType: string;
    subTaskUUID: string;
  }[];
}
