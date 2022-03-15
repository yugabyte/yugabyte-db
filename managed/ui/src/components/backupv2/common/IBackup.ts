/*
 * Created on Thu Feb 10 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

export enum Backup_States {
  IN_PROGRESS = 'InProgress',
  COMPLETED = 'Completed',
  FAILED = 'Failed',
  DELETED = 'Deleted',
  SKIPPED = 'Skipped',
  FAILED_TO_DELETE = 'FailedToDelete',
  STOPPED = 'Stopped',
  DELETE_IN_PROGRESS = 'DeleteInProgress',
  QUEUED_FOR_DELETION = 'QueuedForDeletion'
}
export enum TableType {
  YQL_TABLE_TYPE = 'YQL_TABLE_TYPE',
  REDIS_TABLE_TYPE = 'REDIS_TABLE_TYPE',
  PGSQL_TABLE_TYPE = 'PGSQL_TABLE_TYPE'
}

export interface Keyspace_Table {
  keyspace: string;
  tablesList: string[];
  storageLocation: string;
}

export interface IBackup {
  state: Backup_States;
  backupUUID: string;
  backupType: TableType;
  storageConfigUUID: string;
  universeUUID: string;
  scheduleUUID: string;
  customerUUID: string;
  universeName: string;
  isStorageConfigPresent: boolean;
  isUniversePresent: boolean;
  onDemand: boolean;
  createTime: number;
  updateTime: number;
  expiryTime: number;
  responseList: Keyspace_Table[];
  sse: boolean;
}

export interface IUniverse {
  universeUUID: string;
  name: string;
}

export enum RESTORE_ACTION_TYPE {
  RESTORE = 'RESTORE',
  RESTORE_KEYS = 'RESTORE_KEYS'
}
export interface TIME_RANGE_STATE {
  startTime: any;
  endTime: any;
  label: any;
}
