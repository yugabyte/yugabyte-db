/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import { AxiosError } from 'axios';
import { FieldValues, Path, UseFormSetError } from 'react-hook-form';
import { customAlphabet } from 'nanoid';

import { YBBeanValidationError, YBPError } from '../../../../redesign/helpers/dtos';

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

/**
 * Handle server errors on the form component.
 */
export const handleFormServerError = <TFieldValues extends FieldValues, TFormError>(
  error: Error | AxiosError<TFormError>,
  asyncErrorField: Path<TFieldValues>,
  setError: UseFormSetError<TFieldValues>
) => {
  // Currently we handle server errors by setting an error on a form field
  // provided by the caller. This will cause the form submission to fail since a field
  // has an error.
  // It is the responsisbility of the caller to clear this error when they wish to resubmit.
  setError(asyncErrorField, { type: 'server', message: error.message });
};

export const getCreateProviderErrorMessage = (
  error: AxiosError<YBBeanValidationError | YBPError>
) =>
  typeof error.response?.data.error === 'string' || error.response?.data.error instanceof String
    ? `Create provider request failed: ${error.response.data.error as string}`
    : 'Form validation failed.';

const LOWER_CASE_ALPHANUMERIC = '0123456789abcdefghijklmnopqrstuvwxyz';
export const generateLowerCaseAlphanumericId = customAlphabet(LOWER_CASE_ALPHANUMERIC);
