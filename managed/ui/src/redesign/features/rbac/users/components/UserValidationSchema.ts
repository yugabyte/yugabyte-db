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

export const getUserValidationSchema = (t: TFunction) => {
  const validationSchema = yup.object({
    email: yup
      .string()
      .required(t('form.validationMsg.emailRequired'))
      .email(t('form.validationMsg.invalidEmail')),
    password: yup.string().required(t('form.validationMsg.passwordRequired')),
    confirmPassword: yup
      .string()
      .required(t('form.validationMsg.confirmPasswordRequired'))
      .oneOf([yup.ref('password')], t('form.validationMsg.confirmPasswordMatch'))
  });

  return validationSchema;
};
