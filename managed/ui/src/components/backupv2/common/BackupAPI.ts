/*
 * Created on Thu Feb 10 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import axios from 'axios';
import { IBackup, Keyspace_Table, RESTORE_ACTION_TYPE, TIME_RANGE_STATE } from '..';
import { ROOT_URL } from '../../../config';

export function getBackupsList(
  page = 0,
  limit = 10,
  searchText: string,
  timeRange: TIME_RANGE_STATE,
  states: any[],
  sortBy: string,
  direction: string,
  universeUUID?:string
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
  if(universeUUID){
    payload['filter']['universeUUIDList'] = [universeUUID]
  }
  if (states.length !== 0 && states[0].label !== 'All') {
    payload.filter['states'] = [states[0].value];
  }
  if (timeRange.startTime && timeRange.endTime) {
    payload.filter['dateRangeStart'] = timeRange.startTime.toISOString();
    payload.filter['dateRangeEnd'] = timeRange.endTime.toISOString();
  }
  return axios.post(`${ROOT_URL}/customers/${cUUID}/backups/page`, payload);
}

export function restoreEntireBackup(backup: IBackup, values: Record<string, any>) {
  const cUUID = localStorage.getItem('customerId');
  const backupStorageInfoList = values['keyspaces'].map(
    (keyspace: Keyspace_Table, index: number) => {
      return {
        backupType: backup.backupType,
        keyspace: keyspace || backup.responseList[index].keyspace,
        sse: backup.sse,
        storageLocation: backup.responseList[index].storageLocation,
        tableNameList: backup.responseList[index].tablesList
      };
    }
  );
  const payload = {
    actionType: RESTORE_ACTION_TYPE.RESTORE,
    backupStorageInfoList: backupStorageInfoList,
    customerUUID: cUUID,
    universeUUID: values['targetUniverseUUID'].value,
    storageConfigUUID: backup.storageConfigUUID,
    parallelism: values['parallelThreads']
  };
  if (values['kmsConfigUUID']) {
    payload['encryptionAtRestConfig'] = {
      encryptionAtRestEnabled: true,
      kmsConfigUUID: values['kmsConfigUUID'].value
    };
  }
  return axios.post(`${ROOT_URL}/customers/${cUUID}/restore`, payload);
}

export function deleteBackup(backupList: IBackup[]) {
  const cUUID = localStorage.getItem('customerId');
  const backup_data = backupList.map((b) => {
    return {
      backupUUID: b.backupUUID,
      storageConfigUUID: b.storageConfigUUID
    };
  });
  return axios.delete(`${ROOT_URL}/customers/${cUUID}/delete_backups`, {
    data: {
      deleteBackupInfos: backup_data
    }
  });
}

export function cancelBackup(backup: IBackup) {
  const cUUID = localStorage.getItem('customerId');
  return axios.post(`${ROOT_URL}/customers/${cUUID}/backups/${backup.backupUUID}/stop`);
}

export function getKMSConfigs() {
  const cUUID = localStorage.getItem('customerId');
  const requestUrl = `${ROOT_URL}/customers/${cUUID}/kms_configs`;
  return axios.get(requestUrl).then((resp) => resp.data);
}
