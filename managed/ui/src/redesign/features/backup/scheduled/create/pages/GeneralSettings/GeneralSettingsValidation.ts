/*
 * Created on Thu Jul 18 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import * as yup from 'yup';
import { TFunction } from 'i18next';
import { GeneralSettingsModel } from '../../models/IGeneralSettings';
import { ScheduledBackupContext } from '../../models/ScheduledBackupContext';
import { ParallelThreads } from '../../../../../../../components/backupv2/common/BackupUtils';

export const getValidationSchema = (
  _scheduledBackupContext: ScheduledBackupContext,
  t: TFunction
) => {
  const keyPrefix = 'backup.scheduled.create.errMsg';
  const validationSchema = yup.object<Partial<GeneralSettingsModel>>({
    scheduleName: yup.string().required(t('scheduleNameRequired', { keyPrefix })),
    storageConfig: yup
      .object({
        value: yup.string(),
        label: yup.string(),
        name: yup.string()
      })
      .typeError(t('storageConfigRequired', { keyPrefix }))
      .test({
        name: 'storageConfig',
        message: t('storageConfigRequired', { keyPrefix }),
        test: function (value) {
          return value?.value !== '';
        }
      }),
    parallelism: yup
      .number()
      .required('asd')
      .min(ParallelThreads.MIN, t('parallelThreads', { keyPrefix }))
      .max(ParallelThreads.MAX, t('parallelThreads', { keyPrefix }))
  });

  return validationSchema;
};
