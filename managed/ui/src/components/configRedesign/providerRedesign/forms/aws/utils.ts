import { AWSValidationKey } from '../../constants';
import { AWS_INVALID_FIELDS } from '../constants';
import {
  AWSProviderCreateFormFieldValues,
  QuickValidationErrorKeys
} from './AWSProviderCreateForm';

export const getInvalidFields = (
  validationError: QuickValidationErrorKeys
): (keyof AWSProviderCreateFormFieldValues)[] => {
  const invalidFields = new Set<keyof AWSProviderCreateFormFieldValues>();
  Object.entries(validationError).forEach(([keyString, _]) => {
    // Optimistically cast keyName as AWSValidationKey.
    // The optional chaining on `AWS_INVALID_FIELDS[keyName]` means an unexpected key can
    // be handled without crashing.
    const keyName = getFieldKeyName(keyString) as AWSValidationKey;
    AWS_INVALID_FIELDS[keyName]?.forEach((field) => invalidFields.add(field));
  });
  return Array.from(invalidFields);
};

const getFieldKeyName = (keyString: string) => keyString.replace(/^(data\.)/, '').split('.')[0];
