/*
 * Created on Wed Apr 02 2025
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { ChangeEvent, useState, useEffect } from 'react';
import { sortBy, isEmpty } from 'lodash';
import { useQuery } from 'react-query';
import { useTranslation, Trans } from 'react-i18next';
import { Controller, FieldValues, Path, PathValue, useFormContext } from 'react-hook-form';
import { YBAutoComplete, YBLabel, YBSelectProps, YBTooltip, mui } from '@yugabyte-ui-library/core';
import { api, QUERY_KEY } from '../../../../../features/universe/universe-form/utils/api';
import { YBProvider } from '../../../../../../components/configRedesign/providerRedesign/types';
import { CloudType } from '../../../../../features/universe/universe-form/utils/dto';
import {
  ProviderCode,
  ProviderStatus
} from '../../../../../../components/configRedesign/providerRedesign/constants';

//icons
import InfoIcon from '../../../../../assets/info-new.svg';

interface ProviderConfigurationFieldProps<T extends FieldValues>
  extends Omit<YBSelectProps, 'name' | 'control'> {
  name: Path<T>;
  label: string;
  placeholder?: string;
  filterByProvider?: string | null;
  disabled?: boolean;
}

export interface Provider {
  uuid: string;
  code: CloudType;
  name: string;
  active: boolean;
  customerUUID: string;
  details: Record<string, any>;
}

const { Box, Typography } = mui;

export const ProviderConfigurationField = <T extends FieldValues>({
  name,
  label,
  placeholder,
  filterByProvider,
  sx,
  disabled
}: ProviderConfigurationFieldProps<T>) => {
  const { control, setValue, getValues } = useFormContext<T>();
  const [filteredProviders, setFilteredProviders] = useState<Provider[]>([]);
  const { t } = useTranslation('translation', { keyPrefix: 'createUniverseV2.generalSettings' });

  const getFilteredProviders = (providersList: Provider[], cloud: any) => {
    let providers: Provider[] = [];
    providers = (providersList as YBProvider[]).filter(
      (provider) =>
        provider.usabilityState === ProviderStatus.READY &&
        (!filterByProvider || provider.code === filterByProvider)
    ) as Provider[];
    providers = sortBy(providers, 'code', 'name');
    return providers;
  };

  const { data, isLoading } = useQuery(QUERY_KEY.getProvidersList, api.getProvidersList, {
    enabled: !!filterByProvider,
    onSuccess: (providers) => {
      // Pre-select provider by default
      if (isEmpty(getValues(name)) && providers.length >= 1) {
        const provider = getFilteredProviders(providers, filterByProvider)[0];
        const isOnPremManuallyProvisioned =
          provider?.code === ProviderCode.ON_PREM && provider?.details?.skipProvisioning;
        setValue(name, { ...provider, isOnPremManuallyProvisioned } as PathValue<T, Path<T>>);
      }
    }
  });

  const handleChange = (e: ChangeEvent<{}>, option: any) => {
    if (option) {
      const isOnPremManuallyProvisioned =
        option?.code === ProviderCode.ON_PREM && option?.details?.skipProvisioning;
      // const { code, uuid } = option;
      setValue(name, { ...option, isOnPremManuallyProvisioned } as PathValue<T, Path<T>>, {
        shouldValidate: true
      });
    } else {
      setValue(name, null as PathValue<T, Path<T>>);
    }
  };

  useEffect(() => {
    if (data && !isLoading) {
      const currentProvider = getValues(name);
      const updatedList = getFilteredProviders(data, filterByProvider);
      setFilteredProviders(updatedList);
      if (!isEmpty(currentProvider) && currentProvider?.code !== filterByProvider)
        setValue('providerConfiguration' as Path<T>, updatedList[0] as PathValue<T, Path<T>>);
    }
  }, [filterByProvider, isLoading]);

  const renderEmptyState = () => {
    return (
      <Box sx={{ display: 'flex', flexDirection: 'column', padding: '8px 0px' }}>
        <Typography sx={{ color: '#4E5F6D' }} variant="subtitle1">
          <Trans
            t={t}
            i18nKey={'providerEmptyState'}
            components={{ a: <br />, b: <strong /> }}
            values={{ providerCode: filterByProvider?.toUpperCase() }}
          />
        </Typography>
      </Box>
    );
  };

  return (
    <Controller
      control={control}
      name={name}
      render={({ field, fieldState }) => {
        const value =
          filteredProviders.find((provider) => provider.uuid === field.value?.uuid) ?? null;
        return (
          <div>
            <YBLabel error={!!fieldState.error}>
              {label}
              <YBTooltip title={t('providerTooltip')} placement="top-start">
                <span style={{ marginTop: '4px' }}>
                  <InfoIcon />
                </span>
              </YBTooltip>
            </YBLabel>
            <Box sx={{ flex: 1 }}>
              <YBAutoComplete
                loading={isLoading}
                value={(value as unknown) as Record<string, string>}
                options={(filteredProviders as unknown) as Record<string, string>[]}
                getOptionLabel={(option: Record<string, string> | string) =>
                  typeof option === 'string' ? option : option.name
                }
                onChange={handleChange}
                ybInputProps={{
                  error: !!fieldState.error,
                  helperText: fieldState.error?.message,
                  placeholder: placeholder,
                  dataTestId: 'ProvidersField-AutoComplete'
                }}
                sx={sx}
                dataTestId="ProvidersField-AutoComplete-container"
                size="large"
                disabled={disabled}
                noOptionsText={renderEmptyState()}
              />
            </Box>
          </div>
        );
      }}
    />
  );
};
