/* eslint-disable react/display-name */
/*
 * Created on Mon Nov 13 2023
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { mui, YBInputField } from '@yugabyte-ui-library/core';
import { OtherAdvancedProps } from '../../steps/advanced-settings/dtos';

const { Box } = mui;

interface InstanceARNProps {
  disabled: boolean;
}

const INSTANCE_ARN_FIELD = 'awsArnString';

export const InstanceARNField: FC<InstanceARNProps> = ({ disabled }) => {
  const { control } = useFormContext<OtherAdvancedProps>();
  const { t } = useTranslation();

  return (
    <Box sx={{ display: 'flex', width: '100%', flexDirection: 'column', gap: '12px' }}>
      <YBInputField
        dataTestId="instance-arn-field"
        control={control}
        name={INSTANCE_ARN_FIELD}
        fullWidth
        disabled={disabled}
        label={t('createUniverseV2.otherAdvancedSettings.arnField')}
        sx={{ width: '734px' }}
      />
    </Box>
  );
};
