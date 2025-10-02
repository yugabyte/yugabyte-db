/*
 * Created on Tue Mar 25 2025
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { YBInputFieldProps, yba } from '@yugabyte-ui-library/core';
import { Controller, FieldValues, Path, useFormContext } from 'react-hook-form';

const { YBInputField } = yba;

interface UniverseNameFieldProps<T extends FieldValues>
  extends Omit<YBInputFieldProps<T>, 'name' | 'control'> {
  name: Path<T>;
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
  const { control } = useFormContext<T>();

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
