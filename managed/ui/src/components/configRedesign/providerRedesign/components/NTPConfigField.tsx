/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import React from 'react';
import { useFormContext } from 'react-hook-form';
import { Box } from '@material-ui/core';

import {
  OptionProps,
  RadioGroupOrientation,
  YBRadioGroupField
} from '../../../../redesign/components';
import { YBMultiEntryInputField } from './YBMultiEntryInputField';
import { NTPSetupType, NTPSetupTypeLabel, ProviderCode } from '../constants';
import { hasDefaultNTPServers } from '../utils';

interface NTPConfigFieldProps {
  providerCode: ProviderCode;
}

/**
 * Handles the `ntpSetupType` and `ntpServers` field
 */
export const NTPConfigField = ({ providerCode }: NTPConfigFieldProps) => {
  const {
    control,
    watch,
    formState: { defaultValues }
  } = useFormContext();

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
  return (
    <Box display="flex" flexDirection="column" justifyContent="center" width="100%">
      <YBRadioGroupField
        name="ntpSetupType"
        control={control}
        options={ntpSetupOptions}
        orientation={RadioGroupOrientation.HORIZONTAL}
      />
      {ntpSetupType === NTPSetupType.SPECIFIED && (
        <YBMultiEntryInputField
          placeholder="Add NTP Servers"
          defaultOptions={[].map(() => ({ value: '', label: '' }))}
          controllerProps={{ name: 'ntpServers', control: control }}
        />
      )}
    </Box>
  );
};
