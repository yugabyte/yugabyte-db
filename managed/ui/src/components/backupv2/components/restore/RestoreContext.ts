/*
 * Created on Sat Jul 08 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { createContext } from "react";
import { IBackup } from "../../common/IBackup";
import { IGeneralSettings } from "./pages/generalSettings/GeneralSettings";
import { ISelectTables } from "./pages/selectTables/TablesSelect";
import { PreflightResponseParams } from "./api";
import { getNextPage, getPrevPage } from "./RestoreUtils";
import { IRenameKeyspace } from "./pages/renameKeyspace/RenameKeyspace";


const SubmitLabels = [
    'Next',
    'Restore'
] as const;

//list of pages 'SwitchRestoreContextPages' component loops through
export const Pages = [
    'PREFETCH_CONFIGS',
    'GENERAL_SETTINGS',
    'SELECT_TABLES',
    'RENAME_KEYSPACES',
    'RESTORE_FINAL'
] as const;

export type Page = typeof Pages[number];

export type formWizardProps = {
    currentPage: Page; 
    submitLabel: typeof SubmitLabels[number]; // label of the ybmodal submit 
    disableSubmit: boolean; //disable the submit button
}

export type RestoreContext = {
    backupDetails: IBackup | null
    formProps: formWizardProps;
    formData: {
        generalSettings: IGeneralSettings | null,
        preflightResponse: PreflightResponseParams | undefined,
        renamedKeyspaces: IRenameKeyspace,
        selectedTables: ISelectTables,
    }
}


export const initialRestoreContextState: RestoreContext = {
    backupDetails: null,
    formData: {
        generalSettings: {
            kmsConfig: null,
            forceKeyspaceRename: false,
            renameKeyspace: false,
            targetUniverse: null,
            tableSelectionType: 'ALL_TABLES',
            parallelThreads: 1,
            selectedKeyspace: null,
            incrementalBackupProps: {

            }
        },
        renamedKeyspaces: {
            renamedKeyspaces: []
        },
        selectedTables: {
            selectedTables: []
        },
        preflightResponse: undefined
    },
    formProps: {
        currentPage: 'PREFETCH_CONFIGS', // default page to show
        submitLabel: "Restore",
        disableSubmit: false
    }
};

export const RestoreFormContext = createContext<RestoreContext>(initialRestoreContextState);



export const restoreMethods = (context: RestoreContext) => ({
    moveToNextPage: () => getNextPage(context),
    moveToPrevPage: () => getPrevPage(context),
    moveToPage: (page: Page): RestoreContext => ({
        ...context,
        formProps: {
            ...context.formProps,
            currentPage: page
        }
    }),
    setSubmitLabel: (text: formWizardProps['submitLabel']): RestoreContext => ({
        ...context,
        formProps: {
            ...context.formProps,
            submitLabel: text
        }
    }),
    saveGeneralSettingsFormData: (val: IGeneralSettings): RestoreContext => ({
        ...context,
        formData: {
            ...context.formData,
            generalSettings: val
        }
    }),
    saveRenamedKeyspacesFormData: (val: IRenameKeyspace): RestoreContext => ({
        ...context,
        formData: {
            ...context.formData,
            renamedKeyspaces: val
        }
    }),
    saveSelectTablesFormData: (val: ISelectTables): RestoreContext => ({
        ...context,
        formData: {
            ...context.formData,
            selectedTables: val
        }
    }),
    setBackupDetails: (backupDetails: IBackup): RestoreContext => ({
        ...context,
        backupDetails
    }),
    savePreflightResponse: (preflightResponse: PreflightResponseParams): RestoreContext => ({
        ...context,
        formData: {
            ...context.formData,
            preflightResponse
        }
    }),
    setDisableSubmit: (flag: boolean): RestoreContext => ({
        ...context,
        formProps: {
            ...context.formProps,
            disableSubmit: flag
        }
    })
});

export type RestoreContextMethods = [
    RestoreContext,
    ReturnType<typeof restoreMethods>,
    {
        hideModal: () => void
    }
]

export type PageRef = {
    onNext: () => void;
    onPrev: () => void;
}
