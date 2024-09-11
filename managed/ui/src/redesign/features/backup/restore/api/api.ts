/*
 * Created on Tue Aug 20 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import axios from 'axios';
import { ROOT_URL } from '../../../../../config';
import {
  PerLocationBackupInfo,
  PreflightResponseParams,
  restoreBackupProps
} from '../../../../../components/backupv2/components/restore/api';

export interface RestorableTablesResp {
  // List of tables that can be restored
  tableNames: string[];
  // Keyspaces that can be restored
  keyspace: string;
}

// API to get the list of restorable tables
/**
 * @param baseBackupUUID - UUID of the base backup
 * most useful for advanced restore
 */
export const getRestorableTables = (baseBackupUUID: string) => {
  const customerUUID = localStorage.getItem('customerId');
  return axios.get<RestorableTablesResp[]>(
    `${ROOT_URL}/customers/${customerUUID}/backups/${baseBackupUUID}/restorable_keyspace_tables`
  );
};

// API to validate the restorable tables
// throws error if PITR validation is failed
export interface ValidateRestoreApiReq {
  backupUUID: string;
  keyspaceTables: RestorableTablesResp[];
  restoreToPointInTimeMillis?: number;
}

export const validateRestorableTables = (payload: ValidateRestoreApiReq) => {
  const customerUUID = localStorage.getItem('customerId');
  return axios.post(
    `${ROOT_URL}/customers/${customerUUID}/restore/validate_restorable_keyspace_tables`,
    payload
  );
};

interface PreflightReqParams {
  backupUUID: string;
  keyspaceTables: RestorableTablesResp[];
  universeUUID: string;
  restoreToPointInTimeMillis?: number;
}
export type PointInTimeRestoreWindow = {
  timestampRetentionWindowStartMillis: number;
  timestampRetentionWindowEndMillis: number;
};
export interface PreflightResponseV2Params extends PreflightResponseParams {
  perLocationBackupInfoMap: {
    [key: string]: PerLocationBackupInfo & {
      PointInTimeRestoreWindow: PointInTimeRestoreWindow;
    };
  };
}

// API to get the preflight check
export const getPreflightCheck = (payload: PreflightReqParams) => {
  const cUUID = localStorage.getItem('customerId');
  const requestUrl = `${ROOT_URL}/customers/${cUUID}/restore/preflight`;
  return axios
    .post<PreflightResponseV2Params>(requestUrl, { ...payload })
    .then((resp) => resp.data);
};

export type RestoreV2BackupProps = restoreBackupProps & {
  restoreToPointInTimeMillis?: number;
};

// API to restore the backup
export const restoreBackup = (payload: RestoreV2BackupProps) => {
  const cUUID = localStorage.getItem('customerId');

  return axios.post(`${ROOT_URL}/customers/${cUUID}/restore`, { ...payload, customerUUID: cUUID });
};
