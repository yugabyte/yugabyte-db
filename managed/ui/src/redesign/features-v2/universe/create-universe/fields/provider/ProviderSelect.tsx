import { ReactElement, useMemo } from 'react';
import { Control, FieldValues, Path, useFormContext } from 'react-hook-form';
import { useQuery } from 'react-query';
import { YBCloudSelectField } from '@yugabyte-ui-library/core';
import {
  CREATE_UNIVERSE_PROVIDERS_QUERY_KEY,
  fetchCreateUniverseProviders,
  getOrderedCloudOptions
} from '../../helpers/generalSettingsDefaults';

interface CloudFieldProps<T> {
  name: string;
  label: string;
}

export const CloudField = <T,>({ name, label }: CloudFieldProps<T>): ReactElement => {
  const { control, getValues, setValue } =
    useFormContext<T extends FieldValues ? T : FieldValues>();
  const fieldName = name as Path<T extends FieldValues ? T : FieldValues>;

  const { data: providers = [] } = useQuery(
    CREATE_UNIVERSE_PROVIDERS_QUERY_KEY,
    fetchCreateUniverseProviders
  );

  const clouds = useMemo(() => getOrderedCloudOptions(providers).clouds, [providers]);

  return (
    <YBCloudSelectField
      name={name}
      label={label}
      options={clouds as any}
      control={control as unknown as Control<FieldValues>}
      value={getValues(fieldName)}
      onChange={(value: FieldValues) => {
        setValue(fieldName, value as any);
      }}
    />
  );
};
