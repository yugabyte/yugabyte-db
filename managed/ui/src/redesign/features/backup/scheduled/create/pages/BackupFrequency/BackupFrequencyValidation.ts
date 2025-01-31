/*
 * Created on Tue Aug 13 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import * as Yup from 'yup';
import { TFunction } from 'i18next';
import { MILLISECONDS_IN } from '../../../../../../../components/backupv2/scheduled/ScheduledBackupUtils';
import { DEFAULT_MIN_INCREMENTAL_BACKUP_INTERVAL } from '../../../ScheduledBackupUtils';
import { ScheduledBackupContext } from '../../models/ScheduledBackupContext';
import { BackupFrequencyModel } from '../../models/IBackupFrequency';

type extraValues = {
  minIncrementalScheduleFrequencyInSecs: string;
};

export const getValidationSchema = (
  _scheduledBackupContext: ScheduledBackupContext,
  t: TFunction,
  extraVal: extraValues
) => {
  const validationSchema = Yup.object<Partial<BackupFrequencyModel>>({
    frequency: Yup.number()
      .required(t('errMsg.frequencyRequired'))
      .typeError(t('errMsg.frequencyRequired'))
      .test({
        message: t('errMsg.frequency'),
        test: function (value) {
          if (this.parent.useCronExpression) {
            return true;
          }

          return (
            value * (MILLISECONDS_IN as any)[this.parent.frequencyTimeUnit.toUpperCase()] >=
            MILLISECONDS_IN['HOURS']
          );
        }
      }),
    cronExpression: Yup.string().when('useCronExpression', {
      is: (useCronExpression) => useCronExpression,
      then: Yup.string().required(t('errMsg.cronExpressionRequired'))
    }),
    expiryTime: Yup.number().when('keepIndefinitely', {
      is: (keepIndefinitely) => !keepIndefinitely,
      then: Yup.number().min(1, t('errMsg.expiryTime'))
    }),
    incrementalBackupFrequency: Yup.number()
      .test({
        message: t('errMsg.incrementalBackupFrequency'),
        test: function (value) {
          if (!this.parent.useIncrementalBackup || this.parent.useCronExpression) return true;

          return (
            value *
              (MILLISECONDS_IN as any)[
                this.parent.incrementalBackupFrequencyTimeUnit.toUpperCase()
              ] <
            this.parent.frequency *
              (MILLISECONDS_IN as any)[this.parent.frequencyTimeUnit.toUpperCase()]
          );
        }
      })
      .test({
        message: t('errMsg.incGreaterThan', {
          min:
            Number(
              extraVal.minIncrementalScheduleFrequencyInSecs ??
                DEFAULT_MIN_INCREMENTAL_BACKUP_INTERVAL
            ) / 60
        }),
        test: function (value) {
          if (!this.parent.useIncrementalBackup || this.parent.useCronExpression) {
            return true;
          }
          if (extraVal.minIncrementalScheduleFrequencyInSecs) {
            if (
              value *
                (MILLISECONDS_IN as any)[
                  this.parent.incrementalBackupFrequencyTimeUnit.toUpperCase()
                ] >=
              parseInt(extraVal.minIncrementalScheduleFrequencyInSecs) * 1000
            ) {
              return true;
            }
            return false;
          }
          return true;
        }
      })
  });

  return validationSchema;
};
