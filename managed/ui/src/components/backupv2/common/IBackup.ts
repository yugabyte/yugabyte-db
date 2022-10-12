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

export interface ICommonBackupInfo {
  backupUUID: string;
  baseBackupUUID: string;
  completionTime: number;
  createTime: number;
  responseList: Keyspace_Table[];
  sse: boolean;
  state: Backup_States;
  storageConfigUUID: string;
  taskUUID: string;
  totalBackupSizeInBytes?: number;
  updateTime: number;
  parallelism: number;
}

export interface IBackup {
  commonBackupInfo: ICommonBackupInfo;
  isFullBackup: boolean;
  hasIncrementalBackups: boolean;
  lastBackupState: Backup_States;
  backupType: TableType;
  universeUUID: string;
  scheduleUUID: string;
  customerUUID: string;
  universeName: string;
  isStorageConfigPresent: boolean;
  isUniversePresent: boolean;
  onDemand: boolean;
  updateTime: number;
  expiryTime: number;
  kmsConfigUUID?: null | string;
  fullChainSizeInBytes: number;
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

export interface ThrottleParameters {
  max_concurrent_uploads: number;
  per_upload_num_objects: number;
  max_concurrent_downloads: number;
  per_download_num_objects: number;
}

interface IOptionType extends OptionTypeBase {
  value: string;
  label: string;
}
export type SELECT_VALUE_TYPE = IOptionType;
