import { ReactElement } from 'react';
import { Control, FieldValues, Path, useFormContext } from 'react-hook-form';
import {
  AWS_CLOUD_OPTION,
  AZURE_CLOUD_OPTION,
  GCP_CLOUD_OPTION,
  K8S_CLOUD_OPTION,
  ON_PREM_CLOUD_OPTION,
  YBCloudSelectField
} from '@yugabyte-ui-library/core';
import { CloudType } from '../../../../features/universe/universe-form/utils/dto';

interface CloudFieldProps<T> {
  name: string;
  label: string;
}

export const CloudField = <T,>({ name, label }: CloudFieldProps<T>): ReactElement => {
  const { control, getValues, setValue } = useFormContext<
    T extends FieldValues ? T : FieldValues
  >();
  const clouds = [
    AWS_CLOUD_OPTION,
    GCP_CLOUD_OPTION,
    {
      ...AZURE_CLOUD_OPTION,
      value: CloudType.azu
    },
    {
      ...K8S_CLOUD_OPTION,
      value: CloudType.kubernetes
    },
    ON_PREM_CLOUD_OPTION
  ];

  return (
    <YBCloudSelectField
      name={name}
      label={label}
      options={clouds as any}
      control={(control as unknown) as Control<FieldValues>}
      value={getValues(name as Path<T extends FieldValues ? T : FieldValues>)}
      onChange={(value: FieldValues) => {
        setValue(name as Path<T extends FieldValues ? T : FieldValues>, value as any);
      }}
    />
  );
};
