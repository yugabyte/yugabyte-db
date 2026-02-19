/*
 * Created on Tue Mar 25 2025
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { useEffectOnce } from 'react-use';
import { Controller, FieldValues, Path, useFormContext } from 'react-hook-form';
import { YBInputFieldProps, yba } from '@yugabyte-ui-library/core';
import { generateUniqueName } from '@app/redesign/helpers/utils';
import { GeneralSettingsProps } from '../../steps/general-settings/dtos';

const { YBInputField } = yba;

interface UniverseNameFieldProps<T extends FieldValues>
  extends Omit<YBInputFieldProps<T>, 'name' | 'control'> {
  name: Path<GeneralSettingsProps>;
  label: string;
  placeholder?: string;
  type?: string;
}

export const UniverseNameField = <T extends FieldValues>({
  name,
  label,
  placeholder,
  type = 'text',
  sx
}: UniverseNameFieldProps<T>) => {
  const { control, setValue, getValues } = useFormContext<GeneralSettingsProps>();

  useEffectOnce(() => {
    if (!getValues(name)) {
      //set deafault value only for the first time
      setValue(name, generateUniqueName());
    }
  });

  return (
    <Controller
      control={control}
      name={name}
      render={({ field }) => (
        <YBInputField
          {...field}
          control={control}
          id={name}
          placeholder={placeholder}
          type={type}
          label={label}
          sx={sx}
          dataTestId="universe-name-field"
        />
      )}
    />
  );
};
