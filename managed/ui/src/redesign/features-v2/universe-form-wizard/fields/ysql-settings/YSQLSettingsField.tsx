/*
 * Created on Tue Mar 25 2025
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { ReactElement } from 'react';
import {
  YBInputFieldProps,
  yba,
  mui,
  YBToggleField,
  YBPasswordField
} from '@yugabyte-ui-library/core';
import { useFormContext, useWatch } from 'react-hook-form';
import { DatabaseSettingsProps } from '../../steps/database-settings/dtos';

const { Box, Typography } = mui;

import { ReactComponent as NextLineIcon } from '../../../../assets/next-line.svg';

interface YSQLProps {
  disabled?: boolean;
}

export const YSQL_FIELD = 'ysql.enable';
const YSQL_AUTH_FIELD = 'ysql.enable_auth';
const YSQL_PASSWORD_FIELD = 'ysql.password';
const YSQL_CONFIRM_PWD = 'ysql_confirm_password';

export const YSQLField = ({ disabled }: YSQLProps): ReactElement => {
  const { control } = useFormContext<DatabaseSettingsProps>();

  //watchers
  const ysqlEnabled = useWatch({ name: YSQL_FIELD });
  const ysqlAuthEnabled = useWatch({ name: YSQL_AUTH_FIELD });
  const ysqlPassword = useWatch({ name: YSQL_PASSWORD_FIELD });

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
          <YBToggleField name={YSQL_FIELD} control={control} />
        </Box>
        <Typography variant="body2">Enable YSQL End Point</Typography>
      </Box>
      {ysqlEnabled && (
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
              <YBToggleField name={YSQL_AUTH_FIELD} control={control} />
            </Box>
            <Typography variant="body2">Enable YSQL Authentication</Typography>
          </Box>
          {ysqlAuthEnabled && (
            <Box sx={{ display: 'flex', flexDirection: 'column', mt: 4, pl: 5 }}>
              <YBPasswordField
                name={YSQL_PASSWORD_FIELD}
                control={control}
                placeholder="YSQL Auth Password"
                label="YSQL Auth Password"
              />
              <Box sx={{ display: 'flex', flexDirection: 'column', mt: 3, width: '100%' }}>
                <YBPasswordField
                  name={YSQL_CONFIRM_PWD}
                  control={control}
                  placeholder="Confirm YSQL Auth Password"
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
