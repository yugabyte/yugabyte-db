/*
 * Created on Wed Apr 02 2025
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { ChangeEvent } from 'react';
import { sortBy } from 'lodash';
import { YBAutoComplete, YBLabel, YBSelectProps } from '@yugabyte-ui-library/core';
import { Controller, FieldValues, Path, PathValue, useFormContext } from 'react-hook-form';
import { api, QUERY_KEY } from '../../../../../features/universe/universe-form/utils/api';
import { useQuery } from 'react-query';
import { Box } from '@material-ui/core';
import {
  ProviderCode,
  ProviderStatus
} from '../../../../../../components/configRedesign/providerRedesign/constants';
import { YBProvider } from '../../../../../../components/configRedesign/providerRedesign/types';
import { CloudType } from '../../../../../features/universe/universe-form/utils/dto';
interface ProviderConfigurationFieldProps<T extends FieldValues>
  extends Omit<YBSelectProps, 'name' | 'control'> {
  name: Path<T>;
  label: string;
  placeholder?: string;
  filterByProvider?: string | null;
}

export interface Provider {
  uuid: string;
  code: CloudType;
  name: string;
  active: boolean;
  customerUUID: string;
  details: Record<string, any>;
}

export const ProviderConfigurationField = <T extends FieldValues>({
  name,
  label,
  placeholder,
  filterByProvider,
  sx
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
            <Box flex={1}>
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
              />
            </Box>
          </div>
        );
      }}
    />
  );
};
