/*
 * Created on Tue Aug 27 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { find, last } from "lodash";
import moment from "moment";
import { useContext } from "react";
import { Backup_States, IBackup, ICommonBackupInfo } from "../../../../components/backupv2";
import { IncrementalBackupProps } from "../../../../components/backupv2/components/BackupDetails";
import { ValidateRestoreApiReq } from "./api/api";
import { RestoreContext, RestoreContextMethods, RestoreFormContext } from "./models/RestoreContext";
import { RestoreFormModel, TimeToRestoreType } from "./models/RestoreFormModel";

// This function is used to determine if the user can select a time frame for the restore operation.
export const userCanSelectTimeFrame = (backupDetails: IBackup, restoreContext: RestoreContext): boolean => {

    const { additionalBackupProps } = restoreContext;

    const isPITREnabled = isPITREnabledInBackup(backupDetails.commonBackupInfo);

    if (!isPITREnabled && !backupDetails?.hasIncrementalBackups) {
        return false;
    }

    if (!isPITREnabled && !additionalBackupProps?.isRestoreEntireBackup) {
        return false;
    }
    return true;
};


export const prepareValidationPayload = (formValues: RestoreFormModel, restoreContext: RestoreContext): ValidateRestoreApiReq => {
    const { backupDetails, restorableTables, additionalBackupProps, incrementalBackupsData } = restoreContext;
    const { source, currentCommonBackupInfo } = formValues;
    const { selectedTables } = source;

    if (!backupDetails) {
        throw new Error("backupDetails is not defined");
    }
    if (!currentCommonBackupInfo) {
        throw new Error("currentCommonBackupInfo is not defined");
    }

    const isPITREnabled = isPITREnabledInBackup(currentCommonBackupInfo);

    const payload = {
        backupUUID: currentCommonBackupInfo.backupUUID,
        keyspaceTables: source.keyspace?.isDefaultOption ? restorableTables.filter(t => !(t as any).isDefaultOption) : [{
            keyspace: source.keyspace!.value,
            tableNames: selectedTables.map(t => t.tableName)
        }],
        restoreToPointInTimeMillis: 0
    };

    if (!isPITREnabled || formValues.source.timeToRestoreType === TimeToRestoreType.RECENT_BACKUP) {
        return payload;
    }

    payload['restoreToPointInTimeMillis'] = getMomentFromPITRMillis(source.pitrMillisOptions).valueOf();

    return payload;

};

export const getMomentFromPITRMillis = (pitrMillis: RestoreFormModel['source']['pitrMillisOptions']) => {
    return moment(pitrMillis.date + " " + pitrMillis.time.format("HH:mm") + ":" + pitrMillis.secs);
};

export const isPITREnabledInBackup = (commonBackupInfo: ICommonBackupInfo): boolean => {
    return !!commonBackupInfo.responseList[0].backupPointInTimeRestoreWindow;
};

export const getLastSuccessfulIncrementalBackup = (incrementalBackups: ICommonBackupInfo[]) => {
    return find(incrementalBackups, { state: Backup_States.COMPLETED });
};

export const getFullBackupInIncBackupChain = (incrementalBackups: ICommonBackupInfo[]) => {
    return last(incrementalBackups.filter(backup => backup.state === Backup_States.COMPLETED && backup.backupUUID === backup.baseBackupUUID));
};

export const getIncrementalBackupByUUID = (incrementalBackups: ICommonBackupInfo[], uuid: string) => {
    return find(incrementalBackups, { backupUUID: uuid });
};

export const isRestoreTriggeredFromIncBackup = (backup: IBackup, additionalBackupProps: IncrementalBackupProps) => {
    return backup.hasIncrementalBackups && !additionalBackupProps?.isRestoreEntireBackup && !!additionalBackupProps.incrementalBackupUUID;
};

export const doesUserSelectsSingleKeyspaceToRestore = (additionalBackupProps: IncrementalBackupProps) =>
    !additionalBackupProps?.isRestoreEntireBackup && additionalBackupProps?.singleKeyspaceRestore;

export function GetRestoreContext() {
    return (useContext(RestoreFormContext) as unknown) as RestoreContextMethods;
};
