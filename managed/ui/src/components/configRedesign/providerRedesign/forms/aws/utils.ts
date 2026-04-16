import { makeStyles } from '@material-ui/core';
import { AWSValidationKey } from '../../constants';
import { AWS_INVALID_FIELDS } from './constants';
import {
  AWSProviderCreateFormFieldValues,
  QuickValidationErrorKeys
} from './AWSProviderCreateForm';
import { AWSProviderEditFormFieldValues } from './AWSProviderEditForm';

export const getInvalidFields = <
  TFormFieldValues extends AWSProviderCreateFormFieldValues | AWSProviderEditFormFieldValues
>(
  validationError: QuickValidationErrorKeys
): (keyof TFormFieldValues)[] => {
  const invalidFields = new Set<keyof TFormFieldValues>();
  Object.entries(validationError).forEach(([keyString, _]) => {
    // Optimistically cast keyName as AWSValidationKey.
    // The optional chaining on `AWS_INVALID_FIELDS[keyName]` means an unexpected key can
    // be handled without crashing.
    const keyName = getFieldKeyName(keyString) as AWSValidationKey;
    AWS_INVALID_FIELDS[keyName]?.forEach((field) => invalidFields.add(field));
  });
  return Array.from(invalidFields);
};

const getFieldKeyName = (keyString: string) => keyString.split('.')[0];

export const useValidationStyles = makeStyles(() => ({
  errorList: {
    '& li': {
      listStyle: 'disc',
      '&:not(:first-child)': {
        marginTop: '12px'
      }
    }
  }
}));
