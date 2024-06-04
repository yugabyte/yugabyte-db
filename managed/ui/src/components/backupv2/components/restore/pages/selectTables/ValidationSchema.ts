/*
 * Created on Thu Jul 06 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import * as yup from 'yup';
import { TFunction } from 'i18next';
import { RestoreContext } from '../../RestoreContext';
import { ISelectTables } from './TablesSelect';

export const getValidationSchema = (_restoreContext: RestoreContext, t: TFunction) => {
  const validationSchema = yup.object<Partial<ISelectTables>>({
    selectedTables: yup
      .array()
      .min(1, t('newRestoreModal.selectTables.validationMessages.minimumTables'))
  } as any);
  return validationSchema;
};
