import React, { FC } from 'react';
import { Controller, useFormContext } from 'react-hook-form';
import { SecurityConfigFormValue } from '../../steps/security/SecurityConfig';
import { Toggle } from '../../../../uikit/Toggle/Toggle';
import './EnableAuthenticationField.scss';

interface EnableAuthenticationFieldProps {
  disabled: boolean;
}

export const EnableAuthenticationField: FC<EnableAuthenticationFieldProps> = ({ disabled }) => {
  const { control } = useFormContext<SecurityConfigFormValue>();

  return (
    <div className="enable-authentication-field">
      <Controller
        control={control}
        name="enableAuthentication"
        render={({ field }) => (
          <Toggle value={field.value} onChange={field.onChange} disabled={disabled} />
        )}
      />
    </div>
  );
};
