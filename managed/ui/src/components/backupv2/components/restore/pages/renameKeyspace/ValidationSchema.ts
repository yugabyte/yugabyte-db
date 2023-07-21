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
import { IRenameKeyspace } from './RenameKeyspace';
import { KEYSPACE_VALIDATION_REGEX } from '../../../../common/BackupUtils';
import { TableType } from '../../../../../../redesign/helpers/dtos';

export const getValidationSchema = (
  restoreContext: RestoreContext,
  tables: string[],
  t: TFunction
) => {
  const contxt = {};

  const { backupDetails } = restoreContext;

  const validationSchema = yup.object<Partial<IRenameKeyspace>>({
    renamedKeyspaces: yup
      .array()
      .min(1)
      .of(
        yup.object().shape({
          renamedKeyspace: yup
            .string()
            .matches(KEYSPACE_VALIDATION_REGEX, {
              message: t('newRestoreModal.renameKeyspaces.validationMessages.invalidKeyspaceName'),
              excludeEmptyString: true
            })
            .when('renamedKeyspaces', {
              // check if the given name is already present in the database, only for YSQL
              is: () => backupDetails?.backupType === TableType.PGSQL_TABLE_TYPE,
              then: yup
                .string()
                .notOneOf(
                  tables,
                  t('newRestoreModal.renameKeyspaces.validationMessages.keyspacesAlreadyExists')
                )
            })
            // check if same name is given as input
            .test(
              'Unique',
              t('newRestoreModal.renameKeyspaces.validationMessages.duplicateKeyspaceName'),
              function (value) {
                if (!value) return true;
                if (contxt[value] !== undefined && this.options['index'] !== contxt[value]) {
                  return false;
                }
                contxt[value] = this.options['index'];
                return true;
              }
            )
        })
      ) as any
  });
  return validationSchema;
};
