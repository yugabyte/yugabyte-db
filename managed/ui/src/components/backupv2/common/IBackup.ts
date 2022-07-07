/*
 * Created on Thu Feb 10 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { TableType } from '../../../redesign/helpers/dtos';

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

export const BACKUP_LABEL_MAP: Record<Backup_States, string> = {
  InProgress: 'In progress',
  Completed: 'Completed',
  Failed: 'Backup failed',
  Deleted: 'Deleted',
  Skipped: 'Cancelled',
  FailedToDelete: 'Deletion failed',
  Stopped: 'Cancelled',
  DeleteInProgress: 'Deleting',
  QueuedForDeletion: 'Queued for deletion'
};

export interface Keyspace_Table {
  keyspace: string;
  tablesList: string[];
  storageLocation?: string;
  defaultLocation?: string;
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
  completionTime: number;
  expiryTime: number;
  responseList: Keyspace_Table[];
  sse: boolean;
  totalBackupSizeInBytes?: number;
  kmsConfigUUID?: null | string;
}

export interface IUniverse {
  universeUUID: string;
  name: string;
  universeDetails: {
    universePaused: boolean;
    [key: string]: any;
  };
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

export enum BACKUP_API_TYPES {
  YSQL = 'PGSQL_TABLE_TYPE',
  YCQL = 'YQL_TABLE_TYPE',
  YEDIS = 'REDIS_TABLE_TYPE'
}

export interface IStorageConfig {
  configUUID: string;
  configName: string;
  name: string;
  data: {
    BACKUP_LOCATION: string;
    REGION_LOCATIONS: any [];
  };
  state: 'ACTIVE' | 'INACTIVE';
  inUse: boolean;
  type: 'STORAGE' | 'CALLHOME';
}

export interface ITable {
  tableName: string;
  keySpace: string;
  tableUUID: string;
  tableType: BACKUP_API_TYPES;
  isIndexTable: boolean;
}

export enum Backup_Options_Type {
  ALL = 'all',
  CUSTOM = 'custom'
}
