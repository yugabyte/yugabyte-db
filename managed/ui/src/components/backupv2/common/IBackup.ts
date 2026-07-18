/*
 * Created on Thu Feb 10 2022
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
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
  InProgress: 'Backup In progress',
  Completed: 'Backup Completed',
  Failed: 'Backup Failed',
  Deleted: 'Backup Deleted',
  Skipped: 'Backup Cancelled',
  FailedToDelete: 'Deletion failed',
  Stopping: 'Cancelling Backup',
  Stopped: 'Backup Cancelled',
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
  tableUUIDList?: string[];
  backupPointInTimeRestoreWindow?: {
    timestampRetentionWindowStartMillis?: number;
    timestampRetentionWindowEndMillis?: number;
  };
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
  useRoles: boolean;
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

/**
 * Source: managed/src/main/java/com/yugabyte/yw/models/configs/CustomerConfig.java
 */
export const CustomerConfigType = {
  STORAGE: 'STORAGE',
  ALERTS: 'ALERTS',
  CALLHOME: 'CALLHOME',
  PASSWORD_POLICY: 'PASSWORD_POLICY'
} as const;
export type CustomerConfigType = typeof CustomerConfigType[keyof typeof CustomerConfigType];

/**
 * Source: managed/src/main/java/com/yugabyte/yw/models/configs/CustomerConfig.java
 */
export const CustomerConfigName = {
  ALERT_PREFERENCES: 'preferences',
  SMTP_INFO: 'smtp info',
  PASSWORD_POLICY: 'password policy',
  CALLHOME_PREFERENCES: 'callhome level'
} as const;
export type CustomerConfigName = typeof CustomerConfigName[keyof typeof CustomerConfigName];

/**
 * Source: managed/src/main/java/com/yugabyte/yw/models/configs/CustomerConfig.java
 */
export const CustomerConfigState = {
  ACTIVE: 'Active',
  QUEUE_FOR_DELETION: 'QueuedForDeletion'
} as const;
export type CustomerConfigState = typeof CustomerConfigState[keyof typeof CustomerConfigState];

/**
 * Base interface for customer config objects.
 * Data field type depends on the configuration type.
 *
 * Source: managed/src/main/java/com/yugabyte/yw/common/customer/config/CustomerConfigUI.java
 */
export interface CustomerConfigBase {
  // Customer Config Object fields
  configUUID: string;
  configName: string;
  type: CustomerConfigType;
  name: string;
  state: CustomerConfigState;
  // Data field type is specified by the configuration type
  // specific interfaces.

  // Additional fields
  inUse: boolean;
}

/**
 * Metadata for a region location.
 *
 * Source: managed/src/main/java/com/yugabyte/yw/models/configs/data/RegionLocationsBase.java
 */
export interface RegionLocationBase {
  REGION: string;
  LOCATION: string;
}

/**
 * Collection level for YBA diagnostics.
 *
 * Source: managed/src/main/java/com/yugabyte/yw/common/CallHomeManager.java
 */
export const CollectionLevel = {
  NONE: 'NONE',
  LOW: 'LOW',
  MEDIUM: 'MEDIUM',
  HIGH: 'HIGH'
} as const;
export type CollectionLevel = typeof CollectionLevel[keyof typeof CollectionLevel];

/**
 * Extends the base customer config object with storage config data fields from `CustomerConfigStorageData.java`.
 *
 * Source: managed/src/main/java/com/yugabyte/yw/models/configs/data/CustomerConfigStorageData.java
 */
export interface StorageConfig extends CustomerConfigBase {
  type: typeof CustomerConfigType.STORAGE;
  data: {
    BACKUP_LOCATION: string;
    REGION_LOCATIONS: RegionLocationBase[];
  };
}

/**
 * Extends the base customer config object with call home config data fields from `CustomerConfigCallHomeData.java`.
 *
 * Source: managed/src/main/java/com/yugabyte/yw/models/configs/data/CustomerConfigCallHomeData.java
 */
export interface CallHomeConfig extends CustomerConfigBase {
  type: typeof CustomerConfigType.CALLHOME;
  name: typeof CustomerConfigName.CALLHOME_PREFERENCES;
  data: {
    callhomeLevel: CollectionLevel;
  };
}

/**
 * Extends the base customer config object with alert preferences config data fields from `CustomerConfigAlertsPreferencesData.java`.
 *
 * Source: managed/src/main/java/com/yugabyte/yw/models/configs/data/CustomerConfigAlertsPreferencesData.java
 */
export interface AlertPreferencesConfig extends CustomerConfigBase {
  type: typeof CustomerConfigType.ALERTS;
  name: typeof CustomerConfigName.ALERT_PREFERENCES;
  data: {
    alertingEmail: string;
    sendAlertsToYb: boolean;
    checkIntervalMs: number;
    statusUpdateIntervalMs: number;
    reportOnlyErrors: boolean;
    activeAlertNotificationIntervalMs: number;
  };
}

/**
 * Extends the base customer config object with alert smtp config data fields from `CustomerConfigAlertsSmtpInfoData.java`.
 *
 * Source: managed/src/main/java/com/yugabyte/yw/models/configs/data/CustomerConfigAlertsSmtpInfoData.java
 */
export interface AlertSmtpConfig extends CustomerConfigBase {
  type: typeof CustomerConfigType.ALERTS;
  name: typeof CustomerConfigName.SMTP_INFO;
  data: {
    smtpServer: string;
    smtpPort: number;
    emailFrom: string;
    smtpUsername: string;
    smtpPassword: string;
    useSSL: boolean;
    useTLS: boolean;
  };
}

/**
 * Extends the base customer config object with password policy config data fields from `CustomerConfigPasswordPolicyData.java`.
 *
 * Source: managed/src/main/java/com/yugabyte/yw/models/configs/data/CustomerConfigPasswordPolicyData.java
 */
export interface PasswordPolicyConfig extends CustomerConfigBase {
  type: typeof CustomerConfigType.PASSWORD_POLICY;
  name: typeof CustomerConfigName.PASSWORD_POLICY;
  data: {
    minLength: number;
    minUppercase: number;
    minLowercase: number;
    minDigits: number;
    minSpecialCharacters: number;
    disallowedCharacters: string;
  };
}

/**
 * Models a customer config object with additional information for YBA UI.
 *
 * Source: managed/src/main/java/com/yugabyte/yw/common/customer/config/CustomerConfigUI.java
 */
export type CustomerConfig =
  | StorageConfig
  | CallHomeConfig
  | AlertPreferencesConfig
  | AlertSmtpConfig
  | PasswordPolicyConfig;

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
    disk_read_bytes_per_sec: ThrottleParamsVal;
    disk_write_bytes_per_sec: ThrottleParamsVal;
  };
}

interface IOptionType extends OptionTypeBase {
  value: string;
  label: string;
}
export type SELECT_VALUE_TYPE = IOptionType;
