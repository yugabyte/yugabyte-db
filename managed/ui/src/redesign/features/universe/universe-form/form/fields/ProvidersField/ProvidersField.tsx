import { ChangeEvent, ReactElement } from 'react';
import _ from 'lodash';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { Controller, useFormContext } from 'react-hook-form';
import { Box } from '@material-ui/core';
import { YBLabel, YBAutoComplete } from '../../../../../../components';
import { api, QUERY_KEY } from '../../../utils/api';
import { UniverseFormData, Provider, DEFAULT_CLOUD_CONFIG } from '../../../utils/dto';
import { PROVIDER_FIELD } from '../../../utils/constants';
import { useFormFieldStyles } from '../../../universeMainStyle';
import { YBProvider } from '../../../../../../../components/configRedesign/providerRedesign/types';
import {
  ProviderCode,
  ProviderStatus
} from '../../../../../../../components/configRedesign/providerRedesign/constants';

interface ProvidersFieldProps {
  disabled?: boolean;
  filterByProvider: string | null; //pass only if there is a need to filter the providers list by code
}

// simplified provider object with minimum fields needed in UI
export type ProviderMin = Pick<Provider, 'uuid' | 'code'> & {
  isOnPremManuallyProvisioned: boolean;
};
const getOptionLabel = (option: Record<string, string>): string => option.name;

export const ProvidersField = ({
  disabled,
  filterByProvider
}: ProvidersFieldProps): ReactElement => {
  const { control, setValue, getValues } = useFormContext<UniverseFormData>();
  const classes = useFormFieldStyles();
  const { t } = useTranslation();
  const { data, isLoading } = useQuery(QUERY_KEY.getProvidersList, api.getProvidersList, {
    onSuccess: (providers) => {
      // Pre-select provider by default
      if (_.isEmpty(getValues(PROVIDER_FIELD)) && providers.length >= 1) {
        const provider = providers[0];
        const isOnPremManuallyProvisioned =
          provider?.code === ProviderCode.ON_PREM && provider?.details?.skipProvisioning;
        setValue(
          PROVIDER_FIELD,
          { code: provider?.code, uuid: provider?.uuid, isOnPremManuallyProvisioned },
          { shouldValidate: true }
        );
      }
    }
  });

  let providersList: Provider[] = [];
  if (!isLoading && data) {
    providersList = (data as YBProvider[]).filter(
      (provider) =>
        provider.usabilityState === ProviderStatus.READY &&
        (!filterByProvider || provider.code === filterByProvider)
    ) as Provider[];
    providersList = _.sortBy(providersList, 'code', 'name'); //sort by provider code and name
  }

  const handleChange = (e: ChangeEvent<{}>, option: any) => {
    if (option) {
      const { code, uuid } = option;
      const isOnPremManuallyProvisioned =
        option?.code === ProviderCode.ON_PREM && option?.details?.skipProvisioning;
      setValue(
        PROVIDER_FIELD,
        { code, uuid, isOnPremManuallyProvisioned },
        { shouldValidate: true }
      );
    } else {
      setValue(PROVIDER_FIELD, DEFAULT_CLOUD_CONFIG.provider, { shouldValidate: true });
    }
  };

  return (
    <Box display="flex" width="100%" flexDirection={'row'} data-testid="ProvidersField-Container">
      <Controller
        name={PROVIDER_FIELD}
        control={control}
        rules={{
          required: t('universeForm.validation.required', {
            field: t('universeForm.cloudConfig.providerField')
          }) as string
        }}
        render={({ field, fieldState }) => {
          const value =
            providersList.find((provider) => provider.uuid === field.value?.uuid) || null;
          return (
            <>
              <YBLabel dataTestId="ProvidersField-Label">
                {t('universeForm.cloudConfig.providerField')}
              </YBLabel>
              <Box flex={1} className={classes.defaultTextBox}>
                <YBAutoComplete
                  loading={isLoading}
                  value={(value as unknown) as Record<string, string>}
                  options={(providersList as unknown) as Record<string, string>[]}
                  groupBy={(option: Record<string, string>) => option.code} //group by code for easy navigation
                  getOptionLabel={getOptionLabel}
                  onChange={handleChange}
                  disabled={disabled}
                  ybInputProps={{
                    error: !!fieldState.error,
                    helperText: fieldState.error?.message,
                    'data-testid': 'ProvidersField-AutoComplete',
                    placeholder: t('universeForm.cloudConfig.placeholder.selectProvider')
                  }}
                />
              </Box>
            </>
          );
        }}
      />
    </Box>
  );
};
