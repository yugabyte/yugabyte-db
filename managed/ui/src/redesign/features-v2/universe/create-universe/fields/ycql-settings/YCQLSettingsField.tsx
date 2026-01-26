/*
 * Created on Tue Mar 25 2025
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { ReactElement } from 'react';
import { useTranslation } from 'react-i18next';
import { mui, YBToggleField, YBPasswordField, YBTooltip } from '@yugabyte-ui-library/core';
import { useFormContext, useWatch } from 'react-hook-form';
import { DatabaseSettingsProps } from '../../steps/database-settings/dtos';
import { YSQL_FIELD } from '../ysql-settings/YSQLSettingsField';
import { FieldContainer } from '../../components/DefaultComponents';

const { Box } = mui;

import NextLineIcon from '../../../../../assets/next-line.svg';

interface YCQLProps {
  disabled?: boolean;
}

export const YCQL_FIELD = 'ycql.enable';
const YCQL_AUTH_FIELD = 'ycql.enable_auth';
const YCQL_PASSWORD_FIELD = 'ycql.password';
const YCQL_CONFIRM_PWD = 'ycql.confirm_pwd';

export const YCQField = (): ReactElement => {
  const { control } = useFormContext<DatabaseSettingsProps>();
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.databaseSettings'
  });

  //watchers
  const ycqlEnabled = useWatch({ name: YCQL_FIELD });
  const ycqlAuthEnabled = useWatch({ name: YCQL_AUTH_FIELD });
  const ysqlEnabled = useWatch({ name: YSQL_FIELD });

  return (
    <FieldContainer>
      <Box
        sx={{ display: 'flex', flexDirection: 'row', padding: '16px 24px', alignItems: 'center' }}
      >
        <Box sx={{ marginBottom: '-5px', mr: 1 }}>
          <YBTooltip
            title={!ysqlEnabled && !ycqlEnabled ? (t('enableYsqlOrYcql') as string) : ''}
            placement="top-start"
          >
            <div>
              <YBToggleField
                name={YCQL_FIELD}
                control={control}
                label={t('ycqlSettings.toggleLabel')}
                dataTestId="ycql-settings-field"
              />
            </div>
          </YBTooltip>
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
                label={t('ycqlSettings.authToggleLabel')}
                dataTestId="ycql-settings-auth-field"
              />
            </Box>
          </Box>
          {ycqlAuthEnabled && (
            <Box sx={{ display: 'flex', flexDirection: 'column', mt: 4, pl: 5 }}>
              <YBPasswordField
                name={YCQL_PASSWORD_FIELD}
                control={control}
                placeholder={t('ycqlSettings.authPwd')}
                label={t('ycqlSettings.authPwd')}
                dataTestId="ycql-settings-auth-pwd-field"
              />
              <Box sx={{ display: 'flex', flexDirection: 'column', mt: 3, width: '100%' }}>
                <YBPasswordField
                  name={YCQL_CONFIRM_PWD}
                  control={control}
                  placeholder={t('ycqlSettings.authConfirmPwd')}
                  label={t('ycqlSettings.authConfirmPwd')}
                  dataTestId="ycql-settings-auth-confirm-pwd-field"
                />
              </Box>
            </Box>
          )}
        </Box>
      )}
    </FieldContainer>
  );
};
