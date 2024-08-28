/*
 * Created on Wed Jul 17 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { createContext } from 'react';
import { GeneralSettingsModel } from './IGeneralSettings';
import { BackupObjectsModel } from './IBackupObjects';
import { BackupFrequencyModel, BackupStrategyType, TimeUnit } from './IBackupFrequency';
import { Backup_Options_Type, IBackupSchedule } from '../../../../../../components/backupv2';

export type ExtendedBackupScheduleProps = {
  scheduleName: string;
  frequencyTimeUnit: string;
  incrementalBackupFrequency: number;
  incrementalBackupFrequencyTimeUnit: string;
  enablePointInTimeRestore: boolean;
  keyspaceTableList: any[];
} & IBackupSchedule['backupInfo'];

export enum SubmitLabels {
  Next = 'Next'
}

export enum Page {
  GENERAL_SETTINGS = 'GENERAL_SETTINGS',
  BACKUP_OBJECTS = 'BACKUP_OBJECTS',
  BACKUP_FREQUENCY = 'BACKUP_FREQUENCY',
  BACKUP_SUMMARY = 'BACKUP_SUMMARY'
}

export type formWizardProps = {
  currentPage: Page;
  submitLabel: SubmitLabels;
  disableSubmit: boolean;
  isSubmitting: boolean;
};

export interface ScheduledBackupContext {
  formProps: formWizardProps;
  formData: {
    generalSettings: GeneralSettingsModel;
    backupObjects: BackupObjectsModel;
    backupFrequency: BackupFrequencyModel;
  };
}

// default state
export const initialScheduledBackupContextState: ScheduledBackupContext = {
  formData: {
    generalSettings: {
      scheduleName: '',
      storageConfig: {
        label: '',
        value: '',
        name: ''
      },
      parallelism: 1,
      isYBCEnabledInUniverse: true
    },
    backupObjects: {
      keyspace: null,
      useTablespaces: true,
      selectedTables: [],
      tableBackupType: Backup_Options_Type.ALL
    },
    backupFrequency: {
      backupStrategy: BackupStrategyType.STANDARD,
      frequency: 1,
      frequencyTimeUnit: TimeUnit.DAYS,
      cronExpression: '',
      incrementalBackupFrequency: 1,
      incrementalBackupFrequencyTimeUnit: TimeUnit.HOURS,
      useCronExpression: false,
      useIncrementalBackup: false,
      expiryTime: 1,
      expiryTimeUnit: TimeUnit.DAYS,
      keepIndefinitely: false
    }
  },
  formProps: {
    currentPage: Page.GENERAL_SETTINGS,
    submitLabel: SubmitLabels.Next,
    disableSubmit: false,
    isSubmitting: false
  }
};

export const ScheduledBackupContext = createContext<ScheduledBackupContext>(
  initialScheduledBackupContextState
);

export const scheduledBackupMethods = (context: ScheduledBackupContext) => ({
  setGeneralSettings: (val: GeneralSettingsModel) => ({
    ...context,
    formData: {
      ...context.formData,
      generalSettings: val
    }
  }),
  setBackupObjects: (val: BackupObjectsModel) => ({
    ...context,
    formData: {
      ...context.formData,
      backupObjects: val
    }
  }),
  setBackupFrequency: (val: BackupFrequencyModel) => ({
    ...context,
    formData: {
      ...context.formData,
      backupFrequency: val
    }
  }),
  setPage: (page: Page) => ({
    ...context,
    formProps: {
      ...context.formProps,
      currentPage: page
    }
  }),
  reset: () => ({
    ...context,
    ...initialScheduledBackupContextState
  }),
  setDisableSubmit: (flag: boolean): ScheduledBackupContext => ({
    ...context,
    formProps: {
      ...context.formProps,
      disableSubmit: flag
    }
  }),
  setIsSubmitting: (flag: boolean): ScheduledBackupContext => ({
    ...context,
    formProps: {
      ...context.formProps,
      isSubmitting: flag
    }
  })
});

export type ScheduledBackupContextMethods = [
  ScheduledBackupContext,
  ReturnType<typeof scheduledBackupMethods>,
  {
    hideModal: () => void;
  }
];

// Navigate berween pages
export type PageRef = {
  onNext: () => void;
  onPrev: () => void;
};
