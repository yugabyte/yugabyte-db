/*
 * Created on Thu Feb 10 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import axios from 'axios';
import { Dictionary, groupBy } from 'lodash';
import { IBackup, Keyspace_Table, RESTORE_ACTION_TYPE, TIME_RANGE_STATE } from '..';
import { ROOT_URL } from '../../../config';
import { convertToISODateString } from '../../../redesign/helpers/DateUtils';
import { MILLISECONDS_IN } from '../scheduled/ScheduledBackupUtils';

import {
  BACKUP_API_TYPES,
  Backup_Options_Type,
  ICommonBackupInfo,
  IStorageConfig,
  ITable,
  ThrottleParameters,
  IBackupEditParams
} from './IBackup';

export function getBackupsList(
  page = 0,
  limit = 10,
  searchText: string,
  timeRange: TIME_RANGE_STATE,
  states: any[],
  sortBy: string,
  direction: string,
  moreFilters: any[] | undefined,
  universeUUID?: string,
  storageConfigUUID?: string | null
) {
  const cUUID = localStorage.getItem('customerId');
  const payload = {
    sortBy,
    direction,
    filter: {},
    limit,
    offset: page,
    needTotalCount: true
  };
  if (searchText) {
    payload['filter'] = {
      universeNameList: [searchText]
    };
  }
  if (universeUUID) {
    payload['filter']['universeUUIDList'] = [universeUUID];
  }

  if (storageConfigUUID) {
    payload['filter']['storageConfigUUIDList'] = [storageConfigUUID];
  }

  if (states.length !== 0 && states[0].label !== 'All') {
    payload.filter['states'] = [states[0].value];
  }
  if (timeRange.startTime && timeRange.endTime) {
    payload.filter['dateRangeStart'] = convertToISODateString(timeRange.startTime);
    payload.filter['dateRangeEnd'] = convertToISODateString(timeRange.endTime);
  }

  if (Array.isArray(moreFilters) && moreFilters?.length > 0) {
    payload.filter[moreFilters[0].value] = true;
  }

  return axios.post(`${ROOT_URL}/customers/${cUUID}/backups/page`, payload);
}

export function restoreEntireBackup(backup: IBackup, values: Record<string, any>) {
  const cUUID = localStorage.getItem('customerId');
  const backupStorageInfoList = values['keyspaces'].map(
    (keyspace: Keyspace_Table, index: number) => {
      const infoList = {
        backupType: backup.backupType,
        keyspace: keyspace || backup.commonBackupInfo.responseList[index].keyspace,
        sse: backup.commonBackupInfo.sse,
        storageLocation:
          backup.commonBackupInfo.responseList[index].storageLocation ??
          backup.commonBackupInfo.responseList[index].defaultLocation
      };
      if (values.allow_YCQL_conflict_keyspace) {
        infoList['tableNameList'] = backup.commonBackupInfo.responseList[index].tablesList;
      }
      return infoList;
    }
  );
  const payload = {
    actionType: RESTORE_ACTION_TYPE.RESTORE,
    backupStorageInfoList: backupStorageInfoList,
    customerUUID: cUUID,
    universeUUID: values['targetUniverseUUID'].value,
    storageConfigUUID: backup.commonBackupInfo.storageConfigUUID,
    parallelism: values['parallelThreads']
  };
  if (values['kmsConfigUUID']) {
    payload['kmsConfigUUID'] = values['kmsConfigUUID'].value;
  }
  return axios.post(`${ROOT_URL}/customers/${cUUID}/restore`, payload);
}

export function deleteBackup(backupList: IBackup[]) {
  const cUUID = localStorage.getItem('customerId');
  const backup_data = backupList.map((b) => {
    return {
      backupUUID: b.commonBackupInfo.backupUUID,
      storageConfigUUID: b.commonBackupInfo.storageConfigUUID
    };
  });
  return axios.post(`${ROOT_URL}/customers/${cUUID}/backups/delete`, {
    deleteBackupInfos: backup_data
  });
}

export function cancelBackup(backup: IBackup) {
  const cUUID = localStorage.getItem('customerId');
  return axios.post(
    `${ROOT_URL}/customers/${cUUID}/tasks/${backup.commonBackupInfo.taskUUID}/abort`
  );
}

export function getKMSConfigs() {
  const cUUID = localStorage.getItem('customerId');
  const requestUrl = `${ROOT_URL}/customers/${cUUID}/kms_configs`;
  return axios.get(requestUrl).then((resp) => resp.data);
}

export const prepareBackupCreationPayload = (values: Record<string, any>, cUUID: string | null) => {
  const backup_type = values['api_type'].value;

  const payload = {
    backupType: backup_type,
    customerUUID: cUUID,
    parallelism: values['parallel_threads'],
    sse: values['storage_config'].name === 'S3',
    storageConfigUUID: values['storage_config'].value,
    universeUUID: values['universeUUID'],
    tableByTableBackup: values['isTableByTableBackup'],
    useTablespaces: values['useTablespaces']
  };

  let dbMap: Dictionary<any> = [];

  const filteredTableList = values['tablesList'].filter((t: ITable) => t.tableType === backup_type);

  if (values['db_to_backup'].value !== null) {
    dbMap = {
      [values['db_to_backup'].value]: filteredTableList.filter(
        (t: ITable) => t.keySpace === values['db_to_backup'].value
      )
    };
  }

  if (
    backup_type === BACKUP_API_TYPES.YCQL &&
    values['backup_tables'] === Backup_Options_Type.CUSTOM
  ) {
    dbMap = groupBy(values['selected_ycql_tables'], 'keySpace');
  }

  payload['keyspaceTableList'] = Object.keys(dbMap).map((keyspace) => {
    if (backup_type === BACKUP_API_TYPES.YSQL) {
      return {
        keyspace
      };
    }
    return {
      keyspace,
      tableNameList:
        values['backup_tables'] === Backup_Options_Type.ALL
          ? []
          : dbMap[keyspace].map((t: ITable) => t.tableName),
      tableUUIDList:
        values['backup_tables'] === Backup_Options_Type.ALL
          ? []
          : dbMap[keyspace].map((t: ITable) => t.tableUUID)
    };
  });

  //Calculate TTL
  if (values['keep_indefinitely']) {
    payload['timeBeforeDelete'] = 0;
  } else {
    payload['timeBeforeDelete'] =
      values['retention_interval'] *
      MILLISECONDS_IN[values['retention_interval_type'].value.toUpperCase()];
    payload['expiryTimeUnit'] = values['retention_interval_type'].value.toUpperCase();
  }
  return payload;
};

export function createBackup(values: Record<string, any>, isIncrementalBackup = false) {
  const cUUID = localStorage.getItem('customerId');
  const requestUrl = `${ROOT_URL}/customers/${cUUID}/backups`;

  const payload = prepareBackupCreationPayload(values, cUUID);

  if (isIncrementalBackup) {
    payload['baseBackupUUID'] = values['baseBackupUUID'];
  }

  return axios.post(requestUrl, payload);
}

export function editBackup(values: IBackupEditParams) {
  const cUUID = localStorage.getItem('customerId');
  const backupUUID = values.backupUUID;
  const requestUrl = `${ROOT_URL}/customers/${cUUID}/backups/${backupUUID}`;

  return axios.put(requestUrl, values);
}

export const assignStorageConfig = (backup: IBackup, storageConfig: IStorageConfig) => {
  const cUUID = localStorage.getItem('customerId');
  const requestUrl = `${ROOT_URL}/customers/${cUUID}/backups/${backup.commonBackupInfo.backupUUID}`;
  return axios.put(requestUrl, {
    storageConfigUUID: storageConfig.configUUID
  });
};

export const fetchThrottleParameters = (universeUUID: string) => {
  const cUUID = localStorage.getItem('customerId');
  const requestUrl = `${ROOT_URL}/customers/${cUUID}/universes/${universeUUID}/ybc_throttle_params`;
  return axios.get<ThrottleParameters>(requestUrl);
};

export const setThrottleParameters = (
  universeUUID: string,
  values: ThrottleParameters['throttleParamsMap']
) => {
  const cUUID = localStorage.getItem('customerId');
  const payload = {
    maxConcurrentUploads: values.max_concurrent_uploads.currentValue,
    perUploadNumObjects: values.per_upload_num_objects.currentValue,
    maxConcurrentDownloads: values.max_concurrent_downloads.currentValue,
    perDownloadNumObjects: values.per_download_num_objects.currentValue
  };
  const requestUrl = `${ROOT_URL}/customers/${cUUID}/universes/${universeUUID}/ybc_throttle_params`;
  return axios.post<ThrottleParameters>(requestUrl, payload);
};

export const resetThrottleParameterToDefaults = (universeUUID: string) => {
  const cUUID = localStorage.getItem('customerId');
  const requestUrl = `${ROOT_URL}/customers/${cUUID}/universes/${universeUUID}/ybc_throttle_params`;
  return axios.post(requestUrl, {
    resetDefaults: true
  });
};

export const fetchIncrementalBackup = (backupUUID: string) => {
  const cUUID = localStorage.getItem('customerId');
  const requestUrl = `${ROOT_URL}/customers/${cUUID}/backups/${backupUUID}/list_increments`;
  return axios.get<ICommonBackupInfo[]>(requestUrl);
};

export const addIncrementalBackup = (backup: IBackup) => {
  const cUUID = localStorage.getItem('customerId');
  const requestUrl = `${ROOT_URL}/customers/${cUUID}/backups`;

  const payload = {
    backupType: backup.backupType,
    customerUUID: cUUID,
    parallelism: backup.commonBackupInfo.parallelism,
    sse: backup.commonBackupInfo.sse,
    storageConfigUUID: backup.commonBackupInfo.storageConfigUUID,
    universeUUID: backup.universeUUID,
    baseBackupUUID: backup.commonBackupInfo.baseBackupUUID,
    keyspaceTableList: backup.commonBackupInfo.responseList,
    tableByTableBackup: backup.commonBackupInfo.tableByTableBackup,
    useTablespaces: backup.useTablespaces
  };

  return axios.post(requestUrl, payload);
};

export function deleteIncrementalBackup(incrementalBackup: ICommonBackupInfo) {
  const cUUID = localStorage.getItem('customerId');
  return axios.post(`${ROOT_URL}/customers/${cUUID}/backups/delete`, {
    deleteBackupInfos: [
      {
        backupUUID: incrementalBackup.backupUUID,
        storageConfigUUID: incrementalBackup.storageConfigUUID
      }
    ]
  });
}
