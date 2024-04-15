/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import { AxiosError } from 'axios';
import { FieldValues, FormState, UseFormReturn } from 'react-hook-form';
import { useQuery } from 'react-query';
import { get, keys } from 'lodash';

import { CloudType, YBPError, YBPStructuredError } from '../../../../redesign/helpers/dtos';
import { isYBPBeanValidationError, isYBPError } from '../../../../utils/errorHandlingUtils';
import {
  KeyPairManagement,
  ProviderCode,
  ProviderStatus,
  TRANSITORY_PROVIDER_STATUSES
} from '../constants';
import { fetchGlobalRunTimeConfigs } from '../../../../api/admin';
import { AccessKey, YBProvider } from '../types';
import { NonEditableInUseProviderField } from './constants';
import { runtimeConfigQueryKey } from '../../../../redesign/helpers/api';
import { RunTimeConfigEntry } from '../../../../redesign/features/universe/universe-form/utils/dto';

export const readFileAsText = (sshKeyFile: File) => {
  const reader = new FileReader();
  return new Promise<string | null>((resolve, reject) => {
    reader.onerror = () => {
      reject(new Error('Error reading file.'));
    };
    reader.onabort = () => {
      reject(new Error('Aborted file reading.'));
    };
    reader.onload = () => {
      resolve(reader.result as string | null);
    };
    reader.readAsText(sshKeyFile);
  });
};

type FieldItem = { fieldId: string };
export const addItem = <TFieldItem extends FieldItem>(
  currentItem: TFieldItem,
  items: TFieldItem[],
  setItems: (items: TFieldItem[]) => void
) => {
  const itemsCopy = Array.from(items);
  itemsCopy.push(currentItem);
  setItems(itemsCopy);
};
export const editItem = <TFieldItem extends FieldItem>(
  currentItem: TFieldItem,
  items: TFieldItem[],
  setItems: (items: TFieldItem[]) => void
) => {
  const itemsCopy = Array.from(items);
  const currentItemIndex = itemsCopy.findIndex((region) => region.fieldId === currentItem.fieldId);
  if (currentItemIndex !== -1) {
    itemsCopy[currentItemIndex] = currentItem;
    setItems(itemsCopy);
  }
};
export const deleteItem = <TFieldItem extends FieldItem>(
  currentItem: TFieldItem,
  items: TFieldItem[],
  setItems: (items: TFieldItem[]) => void
) => {
  const itemsCopy = Array.from(items);
  const currentItemIndex = itemsCopy.findIndex((item) => item.fieldId === currentItem.fieldId);
  if (currentItemIndex !== -1) {
    itemsCopy.splice(currentItemIndex, 1);
    setItems(itemsCopy);
  }
};

export const getMutateProviderErrorMessage = (
  error: AxiosError<YBPStructuredError | YBPError>,
  errorLabel = 'Mutate provider request failed'
) => {
  if (isYBPError(error)) {
    const errorMessageDetails = error.response?.data.error;
    return `${errorLabel}${errorMessageDetails ? `: ${errorMessageDetails}` : '.'}`;
  }
  if (isYBPBeanValidationError(error)) {
    return `${errorLabel}: Form validation failed.`;
  }
  const errorMessageDetails =
    (error as AxiosError<YBPStructuredError>).response?.data.error?.['message'] ?? error.message;
  return `${errorLabel}${errorMessageDetails ? `: ${errorMessageDetails}` : '.'}`;
};

export const getCreateProviderErrorMessage = (error: AxiosError<YBPStructuredError | YBPError>) =>
  getMutateProviderErrorMessage(error, 'Create provider request failed');

export const getEditProviderErrorMessage = (error: AxiosError<YBPStructuredError | YBPError>) =>
  getMutateProviderErrorMessage(error, 'Edit provider request failed');

export const generateLowerCaseAlphanumericId = (stringLength = 14) =>
  Array.from(Array(stringLength), () => Math.floor(Math.random() * 36).toString(36)).join('');

/**
 * Constructs the access keys portion of the create provider payload.
 */
export const constructAccessKeysCreatePayload = (
  sshKeypairManagement: KeyPairManagement,
  sshKeypairName: string,
  sshPrivateKeyContent: string,
  skipKeyValidateAndUpload = false
) => ({
  ...(sshKeypairManagement === KeyPairManagement.SELF_MANAGED && {
    allAccessKeys: [
      {
        keyInfo: {
          ...(sshKeypairName && { keyPairName: sshKeypairName }),
          ...(sshPrivateKeyContent && {
            sshPrivateKeyContent: sshPrivateKeyContent
          }),
          skipKeyValidateAndUpload
        }
      }
    ]
  })
});

/**
 * Constructs the access keys portion of the edit provider payload.
 *
 * If no changes are requested, then return the currentAccessKeys.
 * If user provides a new access key, then send the user provided access key.
 * If user opts to let YBA manage the access keys, then omit `allAccessKeys` from the payload.
 */
export const constructAccessKeysEditPayload = (
  editSSHKeypair: boolean,
  sshKeypairManagement: KeyPairManagement,
  newAccessKey: {
    sshKeypairName: string;
    sshPrivateKeyContent: string;
    skipKeyValidateAndUpload?: boolean;
  },
  currentAccessKeys?: AccessKey[]
) => {
  if (editSSHKeypair && sshKeypairManagement === KeyPairManagement.YBA_MANAGED) {
    return {};
  }

  return {
    allAccessKeys: editSSHKeypair
      ? [
          {
            keyInfo: {
              ...(newAccessKey.sshKeypairName && { keyPairName: newAccessKey.sshKeypairName }),
              ...(newAccessKey.sshPrivateKeyContent && {
                sshPrivateKeyContent: newAccessKey.sshPrivateKeyContent
              }),
              skipKeyValidateAndUpload: newAccessKey.skipKeyValidateAndUpload ?? false
            }
          }
        ]
      : currentAccessKeys
  };
};

export const getIsFormDisabled = <TFieldValues extends FieldValues>(
  formState: FormState<TFieldValues>,
  providerConfig?: YBProvider
) => {
  const isProviderBusy =
    !!providerConfig?.usabilityState &&
    (TRANSITORY_PROVIDER_STATUSES as readonly ProviderStatus[]).includes(
      providerConfig?.usabilityState
    );

  return isProviderBusy || formState.isSubmitting;
};

export const getIsRegionFormDisabled = <TFieldValues extends FieldValues>(
  formState: FormState<TFieldValues>
) => formState.isSubmitting;

export const getIsFieldDisabled = (
  providerCode: ProviderCode,
  fieldName: string,
  isFormDisabled: boolean,
  isProviderInUse: boolean
) =>
  isFormDisabled ||
  (NonEditableInUseProviderField[providerCode].includes(fieldName) && isProviderInUse);

export const ValidationErrMsgDelimiter = '<br>';

export const handleFormSubmitServerError = (
  resp: any,
  formMethods: UseFormReturn<any>,
  errFormFieldsMap: Record<string, string>
) => {
  const { error } = resp;
  const errKeys = keys(error);

  errKeys.forEach((key) => {
    if (key === 'errorSource') return;
    const errMsg = error[key]?.join(',') ?? '';
    //accessKey is a special case
    if (!key.includes('[') || key.includes('allAccessKeys')) {
      const fieldNameInForm = errFormFieldsMap[key];
      if (!fieldNameInForm) return;

      formMethods.setError(fieldNameInForm, {
        message: errMsg
      });
    } else {
      const keyArr = key?.split('[');
      const topLevelKey = `${errFormFieldsMap[keyArr?.[0]]}[${(keyArr?.[1]).split(']')[0]}]`;
      const prevError = get(formMethods.formState.errors, topLevelKey);

      formMethods.setError(topLevelKey, {
        message: `${prevError ? prevError?.message + ValidationErrMsgDelimiter : ''}${
          error[key]?.join(ValidationErrMsgDelimiter) ?? ''
        }`
      });
    }
  });
};

const ProviderValidationRuntimeConfigKeys = {
  [CloudType.gcp]: 'yb.provider.gcp_provider_validation',
  [CloudType.azu]: 'yb.provider.azure_provider_validation',
  [CloudType.kubernetes]: 'yb.provider.kubernetes_provider_validation'
};

export function UseProviderValidationEnabled(
  provider: CloudType
): {
  isLoading: boolean;
  isValidationEnabled: boolean;
} {
  const isProviderSupported = [CloudType.azu, CloudType.gcp, CloudType.kubernetes].includes(
    provider
  );

  const { data: globalRuntimeConfigs, isLoading } = useQuery(
    runtimeConfigQueryKey.globalScope(),
    () => fetchGlobalRunTimeConfigs(true).then((res: any) => res.data),
    {
      enabled: isProviderSupported
    }
  );

  if (!isProviderSupported) return { isLoading: false, isValidationEnabled: false };

  const isValidationEnabled =
    globalRuntimeConfigs?.configEntries?.find(
      (c: RunTimeConfigEntry) => c.key === ProviderValidationRuntimeConfigKeys[provider]
    )?.value === 'true';

  return { isLoading, isValidationEnabled };
}
