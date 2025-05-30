/*
 * Created on Wed Apr 02 2025
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { ChangeEvent } from 'react';
import { YBAutoComplete, YBLabel, YBSelectProps } from '@yugabyte-ui-library/core';
import { Controller, FieldValues, Path, PathValue, useFormContext } from 'react-hook-form';
import { api, QUERY_KEY } from '../../../../features/universe/universe-form/utils/api';
import { useQuery } from 'react-query';
import {
  ProviderCode,
  ProviderStatus
} from '../../../../../components/configRedesign/providerRedesign/constants';
import { isEmpty, sortBy } from 'lodash';
import { YBProvider } from '../../../../../components/configRedesign/providerRedesign/types';
import { CloudType } from '../../../../features/universe/universe-form/utils/dto';
import { Box } from '@material-ui/core';
interface ProviderConfigurationFieldProps<T extends FieldValues>
  extends Omit<YBSelectProps, 'name' | 'control'> {
  name: Path<T>;
  label: string;
  placeholder?: string;
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
  sx
}: ProviderConfigurationFieldProps<T>) => {
  const { control, getValues, setValue } = useFormContext<T>();
  const { data, isLoading } = useQuery(QUERY_KEY.getProvidersList, api.getProvidersList, {
    onSuccess: (providers) => {
      // Pre-select provider by default
      if (isEmpty(getValues('providerConfiguration' as Path<T>)) && providers.length >= 1) {
        const provider = providers[0];
        const isOnPremManuallyProvisioned =
          provider?.code === ProviderCode.ON_PREM && provider?.details?.skipProvisioning;
        setValue(
          'providerConfiguration' as Path<T>,
          { code: provider?.code, uuid: provider?.uuid, isOnPremManuallyProvisioned } as PathValue<
            T,
            Path<T>
          >,
          { shouldValidate: true }
        );
      }
    }
  });

  let providersList: Provider[] = [];
  if (!isLoading && data) {
    providersList = (data as YBProvider[]).filter(
      (provider) => provider.usabilityState === ProviderStatus.READY //&&
      // (!filterByProvider || provider.code === filterByProvider)
    ) as Provider[];
    providersList = sortBy(providersList, 'code', 'name'); //sort by provider code and name
  }

  const handleChange = (e: ChangeEvent<{}>, option: any) => {
    if (option) {
      const isOnPremManuallyProvisioned =
        option?.code === ProviderCode.ON_PREM && option?.details?.skipProvisioning;
      const { code, uuid } = option;
      setValue(name, { code, uuid, isOnPremManuallyProvisioned } as PathValue<T, Path<T>>, {
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
                groupBy={(option: Record<string, string>) => option.code} //group by code for easy navigation
                onChange={handleChange}
                ybInputProps={{
                  error: !!fieldState.error,
                  helperText: fieldState.error?.message,
                  // 'data-testid': 'ProvidersField-AutoComplete',
                  placeholder: placeholder
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
