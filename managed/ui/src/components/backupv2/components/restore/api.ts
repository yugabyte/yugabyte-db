/*
 * Created on Wed Jun 28 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import axios from "axios";
import { ROOT_URL } from "../../../../config";
import { IBackup, RESTORE_ACTION_TYPE } from "../../common/IBackup";
import { TableType } from "../../../../redesign/helpers/dtos";


type PreflightParams = {
    backupUUID: string;
    universeUUID: string;
    storageConfigUUID: string;
    backupLocations: string[]
}
export type PerLocationBackupInfo = {
    isYSQLBackup: boolean,
    isSelectiveRestoreSupported: boolean,
    backupLocation: string,
    perBackupLocationKeyspaceTables: {
        originalKeyspace: string,
        tableNameList: string[],
    },
    tablespaceResponse: {
        unsupportedTablespaces?: string[],
        conflictingTablespaces?: string[],
        containsTablespaces: boolean
    }
}
export type PreflightResponseParams = {
    hasKMSHistory: boolean,
    backupCategory: IBackup['category'],
    loggingID: string,
    perLocationBackupInfoMap: {
        [key: string]: PerLocationBackupInfo
    },
}

export const getPreflightCheck = (payload: PreflightParams) => {
    const cUUID = localStorage.getItem('customerId');
    const requestUrl = `${ROOT_URL}/customers/${cUUID}/restore/preflight`;
    return axios.post<PreflightResponseParams>(requestUrl, { ...payload }).then((resp) => resp.data);
};

export type restoreBackupProps = {
    actionType: RESTORE_ACTION_TYPE;
    backupStorageInfoList: {
        backupType: TableType;
        keyspace: string;
        sse: boolean;
        storageLocation?: string;
        tableNameList?: string[];
    }[];
    universeUUID: string;
    kmsConfigUUID?: string;
    storageConfigUUID: string;
    parallelism?: number;
}

export const restoreBackup = (payload: restoreBackupProps) => {
    const cUUID = localStorage.getItem('customerId');

    return axios.post(`${ROOT_URL}/customers/${cUUID}/restore`, { ...payload, customerUUID: cUUID });
};
