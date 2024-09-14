/*
 * Created on Wed Aug 21 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { Backup_Options_Type, ICommonBackupInfo, ITable } from '../../../../../components/backupv2';
import { ValidateRestoreApiReq } from '../api/api';

// Enum for the type of restore action
export enum TimeToRestoreType {
  RECENT_BACKUP = 'RECENT_BACKUP',
  EARLIER_POINT_IN_TIME = 'EARLIER_POINT_IN_TIME'
}

// Model for the source of the restore
export interface RestoreSourceModel {
  // keyspace to restore. Either all db/keyspace or single keyspace/db
  keyspace: {
    value: string;
    label: string;
    // frontend attribute. Used when user selects All Databases/All Keyspaces
    isDefaultOption: boolean;
  } | null;

  // user can select all tables or specific tables to restore. Is used only for YCQL
  tableBackupType: Backup_Options_Type;

  // either incremental backup/point in time restore
  timeToRestoreType: TimeToRestoreType;

  pitrMillisOptions: {
    // user can select specific timelines to restore, incase pitr is enabled.
    date: string;
    time: any;
    secs: number;
    // if pitr is disabled, user can select from the incremental backup by thier creation time
    incBackupTime: string;
  };

  // selected tables to restore. Is used only for YCQL
  selectedTables: ITable[];
}

export interface RestoreTargetModel {
  // target universe to restore to
  targetUniverse: {
    label: string;
    value: string;
  } | null;

  // whether user choose to rename before restore
  renameKeyspace: boolean;

  // force the user to rename, in case of keyspace conflict. only for ysql
  forceKeyspaceRename: boolean;

  // whether to use tablespaces or not.
  useTablespaces: boolean;

  // kms config to use for decryption.
  kmsConfig: {
    label: string;
    value: string;
  };

  // whether to use parallel threads or not. only for YB_BACKUP_SCRIPT
  parallelThreads?: number;
}

export interface RestoreFormModel {
  source: RestoreSourceModel;
  target: RestoreTargetModel;

  // user selected keyspace to restore. Either all db/keyspace or single keyspace/db
  keyspacesToRestore?: ValidateRestoreApiReq;

  // used to hold the user's renamed keyspace value. the values are accessed with index
  renamedKeyspace: { originalKeyspaceName: string; renamedKeyspace: string }[];

  // the common backup info for the restore. The currentCommonBackupInfo holds important info like storage location, storageConfigUUID etc.
  // The responseList holds the keyspace, storage location, tables etc.
  // if entire backup is selected, then we will copy the commonBackupInfo from the baseBackupDetails
  // if Incremental Backup is selected, then we will copy the commonBackupInfo from the incrementalBackupDetails
  // if single keyspace restore is selected, then we will copy the commonBackupInfo from the backupDetails
  currentCommonBackupInfo?: ICommonBackupInfo;

  // the pitrMillis , chosen by the user. 0 means no pitr.
  pitrMillis?: number;
}
