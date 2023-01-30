import React, { FC } from 'react';
import { Controller, useFormContext } from 'react-hook-form';
import { SecurityConfigFormValue } from '../../steps/security/SecurityConfig';
import { Toggle } from '../../../../uikit/Toggle/Toggle';
import { I18n } from '../../../../uikit/I18n/I18n';

interface EnableEncryptionAtRestFieldProps {
  disabled: boolean;
}

export const EnableEncryptionAtRestField: FC<EnableEncryptionAtRestFieldProps> = ({ disabled }) => {
  const { control } = useFormContext<SecurityConfigFormValue>();

  return (
    <Controller
      control={control}
      name="enableEncryptionAtRest"
      render={({ field }) => (
        <Toggle
          value={field.value}
          onChange={field.onChange}
          disabled={disabled}
          description={<I18n>Whether or not to enable encryption at-rest for database</I18n>}
        />
      )}
    />
  );
};
