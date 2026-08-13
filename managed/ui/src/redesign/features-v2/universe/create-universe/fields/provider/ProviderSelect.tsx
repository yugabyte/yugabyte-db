import { ReactElement, useMemo } from 'react';
import { Control, FieldValues, Path, useFormContext } from 'react-hook-form';
import { useQuery } from 'react-query';
import {
  AWS_CLOUD_OPTION,
  AZURE_CLOUD_OPTION,
  GCP_CLOUD_OPTION,
  K8S_CLOUD_OPTION,
  OCI_CLOUD_OPTION,
  ON_PREM_CLOUD_OPTION,
  YBCloudSelectField
} from '@yugabyte-ui-library/core';
import { CloudType } from '../../../../../features/universe/universe-form/utils/dto';
import { api, QUERY_KEY } from '../../../../../features/universe/universe-form/utils/api';
import { ProviderStatus } from '../../../../../../components/configRedesign/providerRedesign/constants';
import { YBProvider } from '../../../../../../components/configRedesign/providerRedesign/types';
import { useEffectOnce } from 'react-use';

interface CloudFieldProps<T> {
  name: string;
  label: string;
}

const BASE_CLOUD_OPTIONS = [
  { ...AWS_CLOUD_OPTION, value: CloudType.aws },
  { ...GCP_CLOUD_OPTION, value: CloudType.gcp },
  { ...AZURE_CLOUD_OPTION, value: CloudType.azu },
  { ...K8S_CLOUD_OPTION, value: CloudType.kubernetes },
  { ...ON_PREM_CLOUD_OPTION, value: CloudType.onprem },
  { ...OCI_CLOUD_OPTION, value: CloudType.oci }
];

export const CloudField = <T,>({ name, label }: CloudFieldProps<T>): ReactElement => {
  const { control, getValues, setValue } =
    useFormContext<T extends FieldValues ? T : FieldValues>();
  const fieldName = name as Path<T extends FieldValues ? T : FieldValues>;

  const { data: providers = [] } = useQuery(QUERY_KEY.getProvidersList, api.getProvidersList);

  const configuredCodes = useMemo(() => {
    return new Set(
      (providers as YBProvider[])
        .filter((provider) => provider.usabilityState === ProviderStatus.READY)
        .map((provider) => provider.code as string)
    );
  }, [providers]);

  const clouds = useMemo(() => {
    const configured = BASE_CLOUD_OPTIONS.filter((option) =>
      configuredCodes.has(option.value)
    ).sort((a, b) => a.label.localeCompare(b.label));
    const unconfigured = BASE_CLOUD_OPTIONS.filter(
      (option) => !configuredCodes.has(option.value)
    );
    return [...configured, ...unconfigured];
  }, [configuredCodes]);

  useEffectOnce(() => {
    if (!getValues(fieldName)) {
      const defaultCloud =
        configuredCodes.size > 0 ? clouds[0].value : CloudType.aws;
      setValue(fieldName, defaultCloud as any);
    }
  });

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
