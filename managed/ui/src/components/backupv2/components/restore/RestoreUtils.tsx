/*
 * Created on Tue Jul 04 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { find, flatten, intersection, values } from "lodash";
import { BACKUP_API_TYPES, ITable, IUniverse } from "../..";
import { Page, RestoreContext } from "./RestoreContext";
import { PreflightResponseParams } from "./api";
import { isYbcEnabledUniverse } from "../../../../utils/UniverseUtils";

// gets the next page to show in the modal
export const getNextPage = (restoreContext: RestoreContext): RestoreContext => {

    const { formProps: { currentPage }, formData: { generalSettings } } = restoreContext;

    switch (currentPage) {
        case 'PREFETCH_CONFIGS':
            return {
                ...restoreContext,
                formProps: {
                    ...restoreContext.formProps,
                    currentPage: 'GENERAL_SETTINGS'
                }
            };
        case 'GENERAL_SETTINGS':
            return currentPageIsGeneralSettings(restoreContext);
        case 'RENAME_KEYSPACES':

            return {
                ...restoreContext,
                formProps: {
                    ...restoreContext.formProps,
                    currentPage: generalSettings?.tableSelectionType === 'SUBSET_OF_TABLES' ? 'SELECT_TABLES' : 'RESTORE_FINAL'
                }
            };
        case 'SELECT_TABLES':
            return {
                ...restoreContext,
                formProps: {
                    ...restoreContext.formProps,
                    currentPage: 'RESTORE_FINAL'
                }
            };
        default:
            return restoreContext;
    }

};

export const currentPageIsGeneralSettings = (restoreContext: RestoreContext): RestoreContext => {

    const { formData: { generalSettings } } = restoreContext;
    if (generalSettings?.renameKeyspace) {
        return {
            ...restoreContext,
            formProps: {
                ...restoreContext.formProps,
                currentPage: 'RENAME_KEYSPACES'
            }
        };
    }

    if (generalSettings?.tableSelectionType === 'SUBSET_OF_TABLES') {
        return {
            ...restoreContext,
            formProps: {
                ...restoreContext.formProps,
                currentPage: 'SELECT_TABLES'
            }
        };
    }

    return {
        ...restoreContext,
        formProps: {
            ...restoreContext.formProps,
            currentPage: 'RESTORE_FINAL'
        }
    };

};

export const getPrevPage = (restoreContext: RestoreContext): RestoreContext => {
    const { formProps: { currentPage }, formData: { generalSettings } } = restoreContext;
    switch (currentPage) {
        case 'RENAME_KEYSPACES':
            return {
                ...restoreContext,
                formProps: {
                    ...restoreContext.formProps,
                    currentPage: 'GENERAL_SETTINGS'
                }
            };
        case 'SELECT_TABLES':
            return {
                ...restoreContext,
                formProps: {
                    ...restoreContext.formProps,
                    currentPage: generalSettings?.renameKeyspace ? 'RENAME_KEYSPACES' : 'GENERAL_SETTINGS'
                }
            };
        default:
            return restoreContext;
    }
};

export const getKeyspacesFromPreflighResponse = (preflight: PreflightResponseParams) => {
    return Object.keys(preflight.perLocationBackupInfoMap).map((k) => {
        return preflight.perLocationBackupInfoMap[k].perBackupLocationKeyspaceTables.originalKeyspace;
    });
};

export const getTablesFromPreflighResponse = (preflight: PreflightResponseParams) => {
    return flatten(Object.keys(preflight.perLocationBackupInfoMap).map((k) => {
        return flatten(preflight.perLocationBackupInfoMap[k].perBackupLocationKeyspaceTables.tableNameList);
    }));
};

export const isDuplicateKeyspaceExistsinUniverse = (preflight: PreflightResponseParams | undefined, tablesInUniverse: ITable[]) => {

    if (!preflight) return false;

    const keyspacesInTargetUniverse = tablesInUniverse
        // we allow duplicate keyspaces for ycql. Only ysql is cheked.
        .filter((k) => k.tableType === BACKUP_API_TYPES.YSQL)
        .map((k) => k.keySpace);

    const keyspacesInBackup = getKeyspacesFromPreflighResponse(preflight);

    return intersection(keyspacesInTargetUniverse, keyspacesInBackup).length > 0;

};

export const isSelectiveRestoreSupported = (preflightResp: PreflightResponseParams) => {
    if (!preflightResp) return false;

    return values(preflightResp.perLocationBackupInfoMap).some((pl) => pl.isSelectiveRestoreSupported);
};

export const isYBCEnabledInUniverse = (universeList: IUniverse[], currentUniverseUUID: string) => {
    const universe = find(universeList, { universeUUID: currentUniverseUUID });
    return isYbcEnabledUniverse(universe?.universeDetails);
};

export const getBackButDisableState = (currentPage: Page) => {
    return ['PREFETCH_CONFIGS', 'GENERAL_SETTINGS', 'RESTORE_FINAL'].includes(currentPage);
};
