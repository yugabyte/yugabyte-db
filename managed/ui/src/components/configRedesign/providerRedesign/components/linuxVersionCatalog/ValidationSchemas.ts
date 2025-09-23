/*
 * Created on Tue Dec 12 2023
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import * as yup from 'yup';
import { find, mapValues } from 'lodash';
import { TFunction } from 'i18next';
import { ProviderCode } from '../../constants';
import { isDefinedNotNull, isNonEmptyString } from '../../../../../utils/ObjectUtils';
import { ImageBundle } from '../../types';

export const getAddLinuxVersionSchema = (
  providerCode: ProviderCode,
  t: TFunction,
  existingImageBundles: ImageBundle[],
  isEditMode: boolean,
  isYBAManagedBundle: boolean
) => {
  const translationPrefix = 'linuxVersion.form.validationMsg';

  const validationSchema = yup.object({
    name: yup
      .string()
      .required(t('nameRequired', { keyPrefix: translationPrefix }))
      .test(
        'duplicatename',
        t('linuxVersionAlreadyExists', { keyPrefix: translationPrefix }),
        function (value: any) {
          return (
            find(existingImageBundles, {
              name: value,
              details: { arch: this.parent?.details?.arch }
            }) === undefined
          );
        }
      ),
    details: yup.object().shape({
      globalYbImage: yup
        .string()
        .test(
          'globalYBImage',
          t('globalYBImagenameRequired', { keyPrefix: translationPrefix }),
          (value: any) => {
            if (isEditMode && isYBAManagedBundle) return true;
            return providerCode === ProviderCode.AWS ? true : isNonEmptyString(value);
          }
        ),
      regions: yup.lazy((obj: any) =>
        yup.object(
          mapValues(obj, () => {
            return yup.object().shape({
              ybImage: yup.string().required(t('amiRequired', { keyPrefix: translationPrefix }))
            });
          })
        )
      ),
      sshPort: yup
        .number()
        .typeError(t('sshPortRequired', { keyPrefix: translationPrefix }))
        .test('sshPort', t('sshPortRequired', { keyPrefix: translationPrefix }), function (value) {
          if (isEditMode && isYBAManagedBundle) return true;
          return isDefinedNotNull(value);
        }),

      sshUser: yup
        .string()
        .test('sshUser', t('sshUserRequired', { keyPrefix: translationPrefix }), function (value) {
          if (isEditMode && isYBAManagedBundle) return true;
          return isNonEmptyString(value);
        })
    })
  });

  return validationSchema;
};
