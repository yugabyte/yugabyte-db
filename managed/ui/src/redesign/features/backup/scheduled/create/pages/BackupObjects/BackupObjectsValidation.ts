/*
 * Created on Tue Aug 06 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import * as yup from 'yup';
import { TFunction } from 'i18next';
import { ScheduledBackupContext } from '../../models/ScheduledBackupContext';
import { BackupObjectsModel } from '../../models/IBackupObjects';
import { BACKUP_API_TYPES, Backup_Options_Type } from '../../../../../../../components/backupv2';

export const getValidationSchema = (
  _scheduledBackupContext: ScheduledBackupContext,
  t: TFunction
) => {
  const validationSchema = yup.object<Partial<BackupObjectsModel>>({
    keyspace: yup.object().typeError(t('errMsg.keyspace')).required(t('errMsg.keyspace')) as any,
    selectedTables: yup.array().when(['keyspace', 'tableBackupType'], {
      is: (keyspace, tableBackupType) =>
        tableBackupType === Backup_Options_Type.CUSTOM &&
        keyspace?.tableType === BACKUP_API_TYPES.YCQL &&
        !keyspace?.isDefaultOption,
      then: yup.array().min(1, t('errMsg.selectedTables')).required(t('errMsg.selectedTables')),
      otherwise: yup.array().notRequired()
    }) as any
  });

  return validationSchema;
};
