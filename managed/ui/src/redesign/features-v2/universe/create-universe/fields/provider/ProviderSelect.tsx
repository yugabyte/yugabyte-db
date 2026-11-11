import { ReactElement, ReactNode, useMemo } from 'react';
import { Control, FieldValues, useFormContext } from 'react-hook-form';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { YBCloudSelectField } from '@yugabyte-ui-library/core';
import { CloudType } from '../../../../../features/universe/universe-form/utils/dto';
import {
  CREATE_UNIVERSE_PROVIDERS_QUERY_KEY,
  fetchCreateUniverseProviders,
  getOrderedCloudOptions
} from '../../helpers/generalSettingsDefaults';
import { MAX_PROVIDER_CONFIGURATIONS } from '../../helpers/providerConfigurations';

interface CloudFieldProps<T> {
  name: string;
  label: string;
  multiple?: boolean;
  disableKubernetes?: boolean;
  showMultiToggle?: boolean;
  multiToggleLabel?: string;
}

export const CloudField = <T,>({
  name,
  label,
  multiple = false,
  disableKubernetes = false,
  showMultiToggle = false,
  multiToggleLabel
}: CloudFieldProps<T>): ReactElement => {
  const { control } = useFormContext<T extends FieldValues ? T : FieldValues>();
  const { t } = useTranslation('translation', { keyPrefix: 'createUniverseV2.generalSettings' });

  const { data: providers = [] } = useQuery(
    CREATE_UNIVERSE_PROVIDERS_QUERY_KEY,
    fetchCreateUniverseProviders
  );

  const clouds = useMemo(() => {
    const options = getOrderedCloudOptions(providers).clouds;
    if (!disableKubernetes) return options;
    return options.map((option) =>
      option.value === CloudType.kubernetes
        ? {
            ...option,
            disabled: true,
            disabledTooltip: t('errMsg.kubernetesNotSupportedMulti')
          }
        : option
    );
  }, [providers, disableKubernetes, t]);

  const limitReachedTooltip: ReactNode = (
    <>
      <strong>{t('errMsg.providerLimitReachedTitle')}</strong>
      <br />
      {t('errMsg.providerLimitReachedBody')}
    </>
  );

  const capped = multiple || showMultiToggle;

  return (
    <YBCloudSelectField
      name={name}
      label={label}
      options={clouds as any}
      control={control as unknown as Control<FieldValues>}
      multiple={showMultiToggle ? undefined : multiple || undefined}
      showMultiToggle={showMultiToggle || undefined}
      multiToggleLabel={multiToggleLabel}
      limit={capped ? MAX_PROVIDER_CONFIGURATIONS : undefined}
      limitReachedTooltip={capped ? limitReachedTooltip : undefined}
    />
  );
};
