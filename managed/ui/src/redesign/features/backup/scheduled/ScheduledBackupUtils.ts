/*
 * Created on Thu Jul 18 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { useLocation } from 'react-use';
import { groupBy, isArray } from 'lodash';
import {
  BACKUP_API_TYPES,
  Backup_Options_Type,
  IStorageConfig,
  ITable
} from '../../../../components/backupv2';
import {
  ExtendedBackupScheduleProps,
  ScheduledBackupContext
} from './create/models/ScheduledBackupContext';
import { ParallelThreads } from '../../../../components/backupv2/common/BackupUtils';
import { MILLISECONDS_IN } from '../../../../components/backupv2/scheduled/ScheduledBackupUtils';
import { BackupStrategyType } from './create/models/IBackupFrequency';

export const DEFAULT_MIN_INCREMENTAL_BACKUP_INTERVAL = 1800; //in secs

// group storage configs by provider type

export const groupStorageConfigs = (storageConfigs: IStorageConfig[]) => {
  if (!isArray(storageConfigs)) {
    return [];
  }
  const filteredConfigs = storageConfigs.filter((c: IStorageConfig) => c.type === 'STORAGE');

  const configs = filteredConfigs.map((c: IStorageConfig) => {
    return {
      value: c.configUUID,
      label: c.configName,
      name: c.name,
      regions: c.data?.REGION_LOCATIONS
    };
  });

  return Object.entries(groupBy(configs, (c: IStorageConfig) => c.name)).map(([label, options]) => {
    return { label, options };
  });
};

export function GetUniverseUUID() {
  const location = useLocation();
  const regex = /\/universes\/([a-f0-9-]+)\/backups/;
  const match = location?.pathname?.match(regex);
  return match ? match[1] : null;
}

export const prepareScheduledBackupPayload = (
  formValues: ScheduledBackupContext,
  universeUUID: string
) => {
  const {
    formData: { generalSettings, backupFrequency, backupObjects }
  } = formValues;

  const payload: Partial<ExtendedBackupScheduleProps> = {
    scheduleName: generalSettings.scheduleName,
    backupType: backupObjects.keyspace?.tableType as BACKUP_API_TYPES,
    sse: generalSettings.storageConfig.name === 'S3',
    storageConfigUUID: generalSettings.storageConfig.value,
    universeUUID,
    // backupObjects
    useTablespaces: backupObjects.useTablespaces,

    //Backup Frequency
    schedulingFrequency:
      backupFrequency.frequency *
      (MILLISECONDS_IN as any)[backupFrequency.frequencyTimeUnit.toUpperCase()],
    frequencyTimeUnit: backupFrequency.frequencyTimeUnit.toUpperCase(),
    enablePointInTimeRestore: backupFrequency.backupStrategy === BackupStrategyType.POINT_IN_TIME
  };

  if (!generalSettings.isYBCEnabledInUniverse) {
    payload['parallelism'] = generalSettings.parallelism ?? ParallelThreads.MIN;
  }

  if (backupFrequency.useIncrementalBackup) {
    const incTimeUnit = backupFrequency.incrementalBackupFrequencyTimeUnit.toUpperCase();
    payload['incrementalBackupFrequency'] =
      backupFrequency.incrementalBackupFrequency * (MILLISECONDS_IN as any)[incTimeUnit];
    payload['incrementalBackupFrequencyTimeUnit'] = incTimeUnit;
  }

  if (backupFrequency.keepIndefinitely) {
    payload['timeBeforeDelete'] = 0;
  } else {
    const expirtyTimeUnit = backupFrequency.expiryTimeUnit.toUpperCase();
    payload['timeBeforeDelete'] =
      backupFrequency.expiryTime * (MILLISECONDS_IN as any)[expirtyTimeUnit];
    payload['expiryTimeUnit'] = expirtyTimeUnit;
  }

  const keyspaceGroups = groupBy(backupObjects.selectedTables, 'keySpace');

  payload['keyspaceTableList'] = Object.keys(keyspaceGroups).map((keyspace) => {
    if (backupObjects.keyspace?.tableType === BACKUP_API_TYPES.YSQL) {
      return {
        keyspace
      };
    }
    return {
      keyspace,
      tableNameList:
        backupObjects.tableBackupType === Backup_Options_Type.ALL
          ? []
          : keyspaceGroups[keyspace].map((t: ITable) => t.tableName),
      tableUUIDList:
        backupObjects.tableBackupType === Backup_Options_Type.ALL
          ? []
          : keyspaceGroups[keyspace].map((t: ITable) => t.tableUUID)
    };
  });
  return payload;
};
