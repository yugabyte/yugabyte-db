/*
 * Created on Wed Apr 02 2025
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { Controller, FieldValues, Path, PathValue, useFormContext } from 'react-hook-form';
import { Box } from '@material-ui/core';
import { YBAutoComplete, YBLabel, YBSelectProps } from '@yugabyte-ui-library/core';
import { Region } from '../../../../../features/universe/universe-form/utils/dto';
import { REGIONS_FIELD } from '../FieldNames';

interface AvailabilityZoneFieldProps<T extends FieldValues>
  extends Omit<YBSelectProps, 'name' | 'control'> {
  name: Path<T>;
  label: string;
  placeholder?: string;
  style?: React.CSSProperties;
}

export const AvailabilityZoneField = <T extends FieldValues>({
  name,
  label,
  placeholder,
  sx,
  style
}: AvailabilityZoneFieldProps<T>) => {
  const { control, watch, setValue } = useFormContext<T>();

  const regions = watch(REGIONS_FIELD as Path<T>);

  const az = regions.map((region: Region) => region.zones).flat();

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
                onChange={(_, val) => setValue(name, (val as any).name as PathValue<T, Path<T>>)}
                ybInputProps={{
                  error: !!fieldState.error,
                  helperText: fieldState.error?.message,
                  placeholder: placeholder,
                  dataTestId: 'availability-zone-field'
                }}
                sx={sx}
              />
            </Box>
          </div>
        );
      }}
    />
  );
};
