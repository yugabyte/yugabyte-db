import React, { FC } from 'react';
import { Controller, useFormContext } from 'react-hook-form';
import { InstanceConfigFormValue } from '../../steps/instance/InstanceConfig';
import { Input } from '../../../../uikit/Input/Input';

interface ProfileARNFieldProps {
  disabled: boolean;
}

export const ProfileARNField: FC<ProfileARNFieldProps> = ({ disabled }) => {
  const { control } = useFormContext<InstanceConfigFormValue>();

  return (
    <Controller
      control={control}
      name="awsArnString"
      render={({ field }) => (
        <Input
          type="text"
          disabled={disabled}
          value={field.value || ''}
          onChange={(event) => field.onChange(event.target.value)}
        />
      )}
    />
  );
};
