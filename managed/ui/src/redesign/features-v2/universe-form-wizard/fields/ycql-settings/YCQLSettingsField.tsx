/*
 * Created on Tue Mar 25 2025
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { ReactElement } from 'react';
import { mui, YBToggleField, YBPasswordField } from '@yugabyte-ui-library/core';
import { useFormContext, useWatch } from 'react-hook-form';
import { DatabaseSettingsProps } from '../../steps/database-settings/dtos';

const { Box, Typography } = mui;

import { ReactComponent as NextLineIcon } from '../../../../assets/next-line.svg';

interface YCQLProps {
  disabled?: boolean;
}

export const YCQL_FIELD = 'ycql.enable';
const YCQL_AUTH_FIELD = 'ycql.enable_auth';
const YCQL_PASSWORD_FIELD = 'ycql.password';
const YCQL_CONFIRM_PWD = 'ycql_confirm_password';

export const YCQLFIELD = ({ disabled }: YCQLProps): ReactElement => {
  const { control } = useFormContext<DatabaseSettingsProps>();

  //watchers
  const ycqlEnabled = useWatch({ name: YCQL_FIELD });
  const ycqlAuthEnabled = useWatch({ name: YCQL_AUTH_FIELD });
  const ycqlPassword = useWatch({ name: YCQL_PASSWORD_FIELD });

  return (
    <Box
      sx={{
        display: 'flex',
        flexDirection: 'column',
        width: '548px',
        height: 'auto',
        bgcolor: '#FBFCFD',
        border: '1px solid #D7DEE4',
        borderRadius: '8px'
      }}
    >
      <Box
        sx={{ display: 'flex', flexDirection: 'row', padding: '16px 24px', alignItems: 'center' }}
      >
        <Box sx={{ marginBottom: '-5px', mr: 1 }}>
          <YBToggleField name={YCQL_FIELD} control={control} label="Enable YCQL End Point" />
        </Box>
      </Box>
      {ycqlEnabled && (
        <Box
          sx={{
            display: 'flex',
            flexDirection: 'column',
            borderTop: '1px solid #D7DEE4',
            padding: '16px 24px 32px 32px'
          }}
        >
          <Box sx={{ display: 'flex', flexDirection: 'row', alignItems: 'center' }}>
            <NextLineIcon />
            <Box sx={{ marginBottom: '-5px', mr: 1, ml: 2 }}>
              <YBToggleField
                name={YCQL_AUTH_FIELD}
                control={control}
                label="Enable YCQL Authentication"
              />
            </Box>
          </Box>
          {ycqlAuthEnabled && (
            <Box sx={{ display: 'flex', flexDirection: 'column', mt: 4, pl: 5 }}>
              <YBPasswordField
                name={YCQL_PASSWORD_FIELD}
                control={control}
                placeholder="YCQL Auth Password"
                label="YCQL Auth Password"
              />
              <Box sx={{ display: 'flex', flexDirection: 'column', mt: 3, width: '100%' }}>
                <YBPasswordField
                  name={YCQL_CONFIRM_PWD}
                  control={control}
                  placeholder="Confirm YCQL Auth Password"
                  label="Confirm Password"
                />
              </Box>
            </Box>
          )}
        </Box>
      )}
    </Box>
  );
};
