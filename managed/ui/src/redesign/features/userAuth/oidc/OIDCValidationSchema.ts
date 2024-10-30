/*
 * Created on Mon Aug 05 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import * as yup from 'yup';
import { TFunction } from 'i18next';
import { OIDCFormProps } from './OIDCConstants';

export const getOIDCValidationSchema = (t: TFunction) =>
  yup.object<Partial<OIDCFormProps>>({
    clientID: yup.string().required(t('messages.clientIDRequired')),
    secret: yup.string().required(t('messages.clientSecretRequired')),
    discoveryURI: yup.string().required(t('messages.discoveryURLRequired'))
  });
