import React, { FC } from 'react';
import { Controller, useFormContext } from 'react-hook-form';
import { SecurityConfigFormValue } from '../../steps/security/SecurityConfig';
import { Toggle } from '../../../../uikit/Toggle/Toggle';
import { I18n } from '../../../../uikit/I18n/I18n';

interface EnableClientToNodeTLSFieldProps {
  disabled: boolean;
}

export const EnableClientToNodeTLSField: FC<EnableClientToNodeTLSFieldProps> = ({ disabled }) => {
  const { control } = useFormContext<SecurityConfigFormValue>();

  return (
    <Controller
      control={control}
      name="enableClientToNodeEncrypt"
      render={({ field }) => (
        <Toggle
          value={field.value}
          onChange={field.onChange}
          disabled={disabled}
          description={
            <I18n>Whether or not to enable TLS Encryption for client to node communication</I18n>
          }
        />
      )}
    />
  );
};
