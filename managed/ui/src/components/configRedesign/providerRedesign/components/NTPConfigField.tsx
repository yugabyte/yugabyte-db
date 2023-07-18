/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import { useQuery } from 'react-query';
import { useFormContext } from 'react-hook-form';
import { Box } from '@material-ui/core';
import { uniq } from 'lodash';

import {
  OptionProps,
  RadioGroupOrientation,
  YBRadioGroupField
} from '../../../../redesign/components';
import { YBMultiEntryInputField } from './YBMultiEntryInputField';
import { NTPSetupType, NTPSetupTypeLabel, ProviderCode } from '../constants';
import { hasDefaultNTPServers } from '../utils';
import { api, providerQueryKey } from '../../../../redesign/helpers/api';
import { YBProvider } from '../types';
import { YBBanner, YBBannerVariant } from '../../../common/descriptors';

interface NTPConfigFieldProps {
  isDisabled: boolean;
  providerCode: ProviderCode;
}

/**
 * Handles the `ntpSetupType` and `ntpServers` field
 */
export const NTPConfigField = ({ isDisabled, providerCode }: NTPConfigFieldProps) => {
  const {
    control,
    watch,
    formState: { defaultValues }
  } = useFormContext();
  const providerListQuery = useQuery(providerQueryKey.ALL, () => api.fetchProviderList());

  const ntpSetupOptions: OptionProps[] = [
    ...(hasDefaultNTPServers(providerCode)
      ? [
          {
            value: NTPSetupType.CLOUD_VENDOR,
            label: NTPSetupTypeLabel[NTPSetupType.CLOUD_VENDOR](providerCode)
          }
        ]
      : []),
    { value: NTPSetupType.SPECIFIED, label: NTPSetupTypeLabel[NTPSetupType.SPECIFIED] },
    { value: NTPSetupType.NO_NTP, label: NTPSetupTypeLabel[NTPSetupType.NO_NTP] }
  ];
  const ntpSetupType = watch('ntpSetupType', defaultValues?.ntpSetupType ?? NTPSetupType.SPECIFIED);
  const ntpInputPlaceholder =
    providerListQuery.isIdle || providerListQuery.isLoading
      ? 'Fetching list of existing NTP servers...'
      : 'Add NTP Servers';
  const existingNTPServers = providerListQuery.isSuccess
    ? getExistingNTPServers(providerListQuery.data)
    : [];
  return (
    <Box display="flex" flexDirection="column" justifyContent="center" width="100%">
      <YBRadioGroupField
        name="ntpSetupType"
        control={control}
        options={ntpSetupOptions}
        orientation={RadioGroupOrientation.HORIZONTAL}
        isDisabled={isDisabled}
      />
      {ntpSetupType === NTPSetupType.SPECIFIED && (
        <YBMultiEntryInputField
          placeholder={ntpInputPlaceholder}
          defaultOptions={existingNTPServers.map((ntpServer) => ({
            value: ntpServer,
            label: ntpServer
          }))}
          controllerProps={{
            name: 'ntpServers',
            control: control
          }}
          isDisabled={isDisabled}
        />
      )}
      {ntpSetupType === NTPSetupType.NO_NTP && (
        <YBBanner variant={YBBannerVariant.WARNING}>
          <b>Note! </b>Ensure that NTP is configured on your machine image, otherwise YugabyteDB
          will not be able to preserve data consistency.
        </YBBanner>
      )}
    </Box>
  );
};

const getExistingNTPServers = (providerList: YBProvider[]) =>
  uniq(
    providerList.reduce((existingNTPServers: string[], currentProvider) => {
      return currentProvider.code !== ProviderCode.KUBERNETES
        ? existingNTPServers.concat(currentProvider.details.ntpServers)
        : existingNTPServers;
    }, [])
  );
