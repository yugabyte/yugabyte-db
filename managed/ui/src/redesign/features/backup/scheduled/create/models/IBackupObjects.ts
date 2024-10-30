/*
 * Created on Thu Jul 18 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { Backup_Options_Type, IBackup, ITable } from '../../../../../../components/backupv2';

export interface BackupObjectsModel {
  keyspace: {
    value: string;
    label: string;
    // frontend attribute. Used when user selects All Databases/All Keyspaces
    isDefaultOption: boolean;
    tableType: ITable['tableType'];
  } | null;
  // only for YSQL
  useTablespaces: IBackup['useTablespaces'];
  // only for YCQL. select all or selected tables
  tableBackupType: Backup_Options_Type;
  // only for YCQL. selected tables
  selectedTables: ITable[];
}
