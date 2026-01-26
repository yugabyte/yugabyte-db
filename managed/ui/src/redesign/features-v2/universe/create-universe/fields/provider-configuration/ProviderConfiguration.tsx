/*
 * Created on Wed Apr 02 2025
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { ChangeEvent } from 'react';
import { sortBy } from 'lodash';
import { useQuery } from 'react-query';
import { Controller, FieldValues, Path, PathValue, useFormContext } from 'react-hook-form';
import { YBAutoComplete, YBLabel, YBSelectProps, mui } from '@yugabyte-ui-library/core';
import { api, QUERY_KEY } from '../../../../../features/universe/universe-form/utils/api';
import { YBProvider } from '../../../../../../components/configRedesign/providerRedesign/types';
import { CloudType } from '../../../../../features/universe/universe-form/utils/dto';
import {
  ProviderCode,
  ProviderStatus
} from '../../../../../../components/configRedesign/providerRedesign/constants';

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

const { Box } = mui;

export const ProviderConfigurationField = <T extends FieldValues>({
  name,
  label,
  placeholder,
  filterByProvider,
  sx,
  disabled
}: ProviderConfigurationFieldProps<T>) => {
  const { control, setValue } = useFormContext<T>();
  const { data, isLoading } = useQuery(QUERY_KEY.getProvidersList, api.getProvidersList, {
    enabled: !!filterByProvider
  });

  let providersList: Provider[] = [];
  if (!isLoading && data) {
    providersList = (data as YBProvider[]).filter(
      (provider) =>
        provider.usabilityState === ProviderStatus.READY &&
        (!filterByProvider || provider.code === filterByProvider)
    ) as Provider[];
    providersList = sortBy(providersList, 'code', 'name'); //sort by provider code and name
  }

  const handleChange = (e: ChangeEvent<{}>, option: any) => {
    if (option) {
      const isOnPremManuallyProvisioned =
        option?.code === ProviderCode.ON_PREM && option?.details?.skipProvisioning;
      // const { code, uuid } = option;
      setValue(name, { ...option, isOnPremManuallyProvisioned } as PathValue<T, Path<T>>, {
        shouldValidate: true
      });
    } else {
      setValue(name, null as PathValue<T, Path<T>>, { shouldValidate: true });
    }
  };

  return (
    <Controller
      control={control}
      name={name}
      render={({ field, fieldState }) => {
        const value = providersList.find((provider) => provider.uuid === field.value?.uuid) ?? null;
        return (
          <div>
            <YBLabel error={!!fieldState.error}>{label}</YBLabel>
            <Box sx={{ flex: 1 }}>
              <YBAutoComplete
                loading={isLoading}
                value={(value as unknown) as Record<string, string>}
                options={(providersList as unknown) as Record<string, string>[]}
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
              />
            </Box>
          </div>
        );
      }}
    />
  );
};
