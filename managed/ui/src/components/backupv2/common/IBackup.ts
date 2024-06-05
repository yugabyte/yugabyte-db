/*
 * Created on Thu Feb 10 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { TableType } from '../../../redesign/helpers/dtos';
import { OptionTypeBase } from 'react-select';

export enum Backup_States {
  IN_PROGRESS = 'InProgress',
  COMPLETED = 'Completed',
  FAILED = 'Failed',
  DELETED = 'Deleted',
  SKIPPED = 'Skipped',
  FAILED_TO_DELETE = 'FailedToDelete',
  STOPPING = 'Stopping',
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
  Stopping: 'Cancelling',
  Stopped: 'Cancelled',
  DeleteInProgress: 'Deleting',
  QueuedForDeletion: 'Queued for deletion'
};

export interface Keyspace_Table {
  allTables: boolean;
  keyspace: string;
  tablesList: string[];
  storageLocation?: string;
  defaultLocation?: string;
  tableNameList?: string[];
  tableUUIDList?: string[]
}

export interface ICommonBackupInfo {
  backupUUID: string;
  baseBackupUUID: string;
  completionTime: string;
  createTime: string;
  responseList: Keyspace_Table[];
  sse: boolean;
  state: Backup_States;
  storageConfigUUID: string;
  taskUUID: string;
  totalBackupSizeInBytes?: number;
  updateTime: string;
  parallelism: number;
  kmsConfigUUID?: string;
  tableByTableBackup: boolean;
}

export interface IBackup {
  commonBackupInfo: ICommonBackupInfo;
  isFullBackup: boolean;
  hasIncrementalBackups: boolean;
  lastIncrementalBackupTime: string;
  lastBackupState: Backup_States;
  backupType: TableType;
  category: 'YB_BACKUP_SCRIPT' | 'YB_CONTROLLER';
  universeUUID: string;
  scheduleUUID: string;
  customerUUID: string;
  universeName: string;
  isStorageConfigPresent: boolean;
  isTableByTableBackup: boolean;
  isUniversePresent: boolean;
  onDemand: boolean;
  updateTime: string;
  expiryTime: string;
  expiryTimeUnit: string;
  fullChainSizeInBytes: number;
  kmsConfigUUID?: null | string;
  scheduleName: string;
  useTablespaces: boolean;
}

export interface IBackupEditParams {
  backupUUID: string;
  timeBeforeDeleteFromPresentInMillis: number;
  storageConfigUUID: string;
  expiryTimeUnit: string;
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
    REGION_LOCATIONS: any[];
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

export type ThrottleParamsVal = {
  currentValue: number;
  presetValues: {
    defaultValue: number;
    minValue: number;
    maxValue: number;
  };
};
export interface ThrottleParameters {
  throttleParamsMap: {
    per_download_num_objects: ThrottleParamsVal;
    max_concurrent_downloads: ThrottleParamsVal;
    max_concurrent_uploads: ThrottleParamsVal;
    per_upload_num_objects: ThrottleParamsVal;
  };
}

interface IOptionType extends OptionTypeBase {
  value: string;
  label: string;
}
export type SELECT_VALUE_TYPE = IOptionType;
