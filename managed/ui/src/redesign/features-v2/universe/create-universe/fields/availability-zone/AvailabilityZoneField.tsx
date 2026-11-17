/*
 * Created on Wed Apr 02 2025
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { useEffect, useMemo } from 'react';
import { Controller, FieldValues, Path, PathValue, useFormContext } from 'react-hook-form';
import { YBAutoComplete, YBLabel, YBSelectProps, mui } from '@yugabyte-ui-library/core';
import { Region } from '../../../../../features/universe/universe-form/utils/dto';
import { REGIONS_FIELD } from '../FieldNames';

interface AvailabilityZoneFieldProps<T extends FieldValues>
  extends Omit<YBSelectProps, 'name' | 'control'> {
  name: Path<T>;
  label: string;
  placeholder?: string;
  style?: React.CSSProperties;
}

const { Box } = mui;

export const AvailabilityZoneField = <T extends FieldValues>({
  name,
  label,
  placeholder,
  sx,
  style
}: AvailabilityZoneFieldProps<T>) => {
  const { control, watch, setValue } = useFormContext<T>();

  const regions = watch(REGIONS_FIELD as Path<T>);

  const az = useMemo(() => regions.flatMap((region: Region) => region.zones ?? []), [regions]);
  const value = watch(name);

  // The AZ belongs to the selected region; drop it once it is no longer an option.
  useEffect(() => {
    if (value && !az.some((zone: Record<string, string>) => zone.name === value)) {
      setValue(name, '' as PathValue<T, Path<T>>, { shouldValidate: true });
    }
  }, [az, value, name, setValue]);

  return (
    <Controller
      control={control}
      name={name}
      render={({ field, fieldState }) => {
        return (
          <div style={style}>
            <YBLabel error={!!fieldState.error}>{label}</YBLabel>
            <Box flex={1}>
              <YBAutoComplete
                dataTestId="availability-zone-field-container"
                value={(field.value as unknown) as Record<string, string>}
                options={az}
                getOptionLabel={(option: Record<string, string> | string) => {
                  return typeof option === 'string'
                    ? option
                    : typeof option === 'number'
                    ? az[option].name
                    : option.name;
                }}
                onChange={(_, val) =>
                  setValue(name, ((val as any)?.name ?? '') as PathValue<T, Path<T>>, {
                    shouldValidate: true
                  })
                }
                ybInputProps={{
                  error: !!fieldState.error,
                  helperText: fieldState.error?.message,
                  placeholder: placeholder,
                  dataTestId: 'availability-zone-field'
                }}
                sx={sx}
                size="large"
              />
            </Box>
          </div>
        );
      }}
    />
  );
};
