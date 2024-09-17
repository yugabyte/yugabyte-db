/*
 * Created on Mon Aug 19 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { createContext } from 'react';
import moment from 'moment';
import {
  Backup_Options_Type,
  IBackup,
  ICommonBackupInfo
} from '../../../../../components/backupv2';
import { IncrementalBackupProps } from '../../../../../components/backupv2/components/BackupDetails';
import { RestoreFormModel, TimeToRestoreType } from './RestoreFormModel';
import { PreflightResponseV2Params, RestorableTablesResp } from '../api/api';

export enum Page {
  SOURCE = 'SOURCE',
  TARGET = 'TARGET',
  RENAME_KEYSPACES = 'RENAME_KEYSPACES',
  RESTORE_FINAL = 'RESTORE_FINAL',
  PREFETCH_DATA = 'PREFETCH_DATA'
}

export type formWizardProps = {
  currentPage: Page;
  submitLabel: string; // label of the ybmodal submit
  disableSubmit: boolean; //disable the submit button
  isSubmitting: boolean;
};

export type RestoreContext = {
  // backup objet from the backup list / backup details page
  backupDetails: IBackup | null;
  // additional backup props for backup. denotes user's click action
  additionalBackupProps: IncrementalBackupProps | null;
  // api resp from list_increments
  incrementalBackupsData: ICommonBackupInfo[] | null;
  // response from restorable_keyspace_tables response
  restorableTables: RestorableTablesResp[];
  // form wizard props
  formProps: formWizardProps;
  // preflight response
  preflightResponse: PreflightResponseV2Params | null;

  keyspacesInTargetUniverse: string[];
};

export const initialRestoreContextState: RestoreContext = {
  backupDetails: null,
  additionalBackupProps: null,
  incrementalBackupsData: null,
  restorableTables: [],
  preflightResponse: null,
  keyspacesInTargetUniverse: [],
  formProps: {
    currentPage: Page.PREFETCH_DATA, // default page to show
    submitLabel: 'Next',
    disableSubmit: false,
    isSubmitting: false
  }
};

export const RestoreFormContext = createContext<RestoreContext>(initialRestoreContextState);

export const restoreMethods = (context: RestoreContext) => ({
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
  setBackupDetails: (backupDetails: IBackup): RestoreContext => ({
    ...context,
    backupDetails
  }),
  setIncrementalBackupProps: (incrementalBackupProps: IncrementalBackupProps): RestoreContext => ({
    ...context,
    additionalBackupProps: incrementalBackupProps
  }),
  setIncrementalBackupsData: (incrementalBackupsData: ICommonBackupInfo[]): RestoreContext => ({
    ...context,
    incrementalBackupsData
  }),
  setPreflightResponse: (preflightResponse: PreflightResponseV2Params): RestoreContext => ({
    ...context,
    preflightResponse
  }),
  setDisableSubmit: (flag: boolean): RestoreContext => ({
    ...context,
    formProps: {
      ...context.formProps,
      disableSubmit: flag
    }
  }),
  setisSubmitting: (flag: boolean): RestoreContext => ({
    ...context,
    formProps: {
      ...context.formProps,
      isSubmitting: flag
    }
  }),
  setRestorableTables: (restorableTables: RestorableTablesResp[]): RestoreContext => ({
    ...context,
    restorableTables
  }),
  setKeyspacesInTargetUniverse: (keyspacesInTargetUniverse: string[]): RestoreContext => ({
    ...context,
    keyspacesInTargetUniverse
  })
});

export type RestoreContextMethods = [
  RestoreContext,
  ReturnType<typeof restoreMethods>,
  {
    hideModal: () => void;
  }
];

// PageRef is used to navigate between pages in the wizard
export type PageRef = {
  onNext: () => void;
  onPrev: () => void;
};

// Default values for the restore form
export const defaultRestoreFormValues: RestoreFormModel = {
  source: {
    keyspace: null,
    pitrMillisOptions: {
      date: moment().format('YYYY-MM-DD'),
      time: moment(),
      secs: 0,

      incBackupTime: ''
    },

    timeToRestoreType: TimeToRestoreType.RECENT_BACKUP,

    tableBackupType: Backup_Options_Type.ALL,

    selectedTables: []
  },

  target: {
    targetUniverse: null,

    renameKeyspace: false,

    forceKeyspaceRename: false,

    kmsConfig: null as any,

    useTablespaces: false,

    parallelThreads: 0
  },

  renamedKeyspace: [],

  pitrMillis: 0
};
