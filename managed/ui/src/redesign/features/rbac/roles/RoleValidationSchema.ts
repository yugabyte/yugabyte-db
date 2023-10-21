/*
 * Created on Tue Oct 03 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { TFunction } from 'i18next';
import * as yup from 'yup';

export const getRoleValidationSchema = (t: TFunction) => {
  const validationSchema = yup.object({
    name: yup
      .string()
      .required(t('form.validationMsg.nameRequired'))
      .matches(/^\S+(?: \S+)*$/, t('form.validationMsg.invalidRoleName')),
    description: yup.string(),
    permissionDetails: yup.mixed().test({
      message: t('form.validationMsg.permRequired'),
      test: function (val) {
        return val.permissionList.length > 0;
      }
    })
  });

  return validationSchema;
};
