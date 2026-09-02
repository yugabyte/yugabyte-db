/*
 * Created on Wed Apr 02 2025
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { ChangeEvent, useMemo } from 'react';
import { useQuery } from 'react-query';
import { useTranslation, Trans } from 'react-i18next';
import { Controller, FieldValues, Path, PathValue, useFormContext } from 'react-hook-form';
import { YBAutoComplete, YBLabel, YBSelectProps, YBTooltip, mui } from '@yugabyte-ui-library/core';
import { CloudType } from '../../../../../features/universe/universe-form/utils/dto';
import {
  CREATE_UNIVERSE_PROVIDERS_QUERY_KEY,
  fetchCreateUniverseProviders,
  getReadyProviders,
  toProviderFormValue
} from '../../helpers/generalSettingsDefaults';

//icons
import InfoIcon from '../../../../../assets/approved/info-new.svg';

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
  const { control, setValue } = useFormContext<T>();
  const { t } = useTranslation('translation', { keyPrefix: 'createUniverseV2.generalSettings' });

  const { data, isLoading } = useQuery(
    CREATE_UNIVERSE_PROVIDERS_QUERY_KEY,
    fetchCreateUniverseProviders
  );

  const filteredProviders = useMemo(
    () => getReadyProviders(data, filterByProvider),
    [data, filterByProvider]
  );

  const handleChange = (e: ChangeEvent<{}>, option: any) => {
    if (option) {
      setValue(name, toProviderFormValue(option) as PathValue<T, Path<T>>, {
        shouldValidate: true
      });
    } else {
      setValue(name, null as PathValue<T, Path<T>>);
    }
  };

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
                isOptionEqualToValue={(option, selectedValue) =>
                  option.uuid === selectedValue.uuid
                }
                filterSelectedOptions={false}
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
                noOptionsText={
                  !isLoading && filteredProviders.length === 0 ? renderEmptyState() : undefined
                }
              />
            </Box>
          </div>
        );
      }}
    />
  );
};
