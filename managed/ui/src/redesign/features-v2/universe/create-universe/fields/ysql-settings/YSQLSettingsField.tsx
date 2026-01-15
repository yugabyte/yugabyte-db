/*
 * Created on Tue Mar 25 2025
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { ReactElement } from 'react';
import { mui, YBToggleField, YBPasswordField, YBTooltip } from '@yugabyte-ui-library/core';
import { useTranslation } from 'react-i18next';
import { useFormContext, useWatch } from 'react-hook-form';
import { DatabaseSettingsProps } from '../../steps/database-settings/dtos';
import { YCQL_FIELD } from '../ycql-settings/YCQLSettingsField';
import { FieldContainer } from '../../components/DefaultComponents';

const { Box } = mui;

import NextLineIcon from '../../../../../assets/next-line.svg';

interface YSQLProps {
  disabled?: boolean;
}
//need to integrate enforceAuth runtime flag

export const YSQL_FIELD = 'ysql.enable';
const YSQL_AUTH_FIELD = 'ysql.enable_auth';
const YSQL_PASSWORD_FIELD = 'ysql.password';
const YSQL_CONFIRM_PWD = 'ysql.confirm_pwd';

export const YSQLField = (): ReactElement => {
  const { control } = useFormContext<DatabaseSettingsProps>();
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.databaseSettings'
  });

  //watchers
  const ysqlEnabled = useWatch({ name: YSQL_FIELD });
  const ysqlAuthEnabled = useWatch({ name: YSQL_AUTH_FIELD });
  const ycqlEnabled = useWatch({ name: YCQL_FIELD });

  return (
    <FieldContainer>
      <Box
        sx={{ display: 'flex', flexDirection: 'row', padding: '16px 24px', alignItems: 'center' }}
      >
        <Box sx={{ marginBottom: '-5px', mr: 1 }}>
          <YBTooltip
            title={!ycqlEnabled && !ysqlEnabled ? (t('enableYsqlOrYcql') as string) : ''}
            placement="top-start"
          >
            <div>
              <YBToggleField
                name={YSQL_FIELD}
                control={control}
                label={t('ysqlSettings.toggleLabel')}
                dataTestId="ysql-settings-field"
              />
            </div>
          </YBTooltip>
        </Box>
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
              <YBToggleField
                name={YSQL_AUTH_FIELD}
                control={control}
                label={t('ysqlSettings.authToggleLabel')}
                dataTestId="ysql-settings-auth-field"
              />
            </Box>
          </Box>
          {ysqlAuthEnabled && (
            <Box sx={{ display: 'flex', flexDirection: 'column', mt: 4, pl: 5 }}>
              <YBPasswordField
                name={YSQL_PASSWORD_FIELD}
                control={control}
                placeholder={t('ysqlSettings.authPwd')}
                label={t('ysqlSettings.authPwd')}
                dataTestId="ysql-settings-auth-pwd-field"
              />
              <Box sx={{ display: 'flex', flexDirection: 'column', mt: 3, width: '100%' }}>
                <YBPasswordField
                  name={YSQL_CONFIRM_PWD}
                  control={control}
                  placeholder={t('ysqlSettings.authConfirmPwd')}
                  label={t('ysqlSettings.authConfirmPwd')}
                  dataTestId="ysql-settings-auth-confirm-pwd-field"
                />
              </Box>
            </Box>
          )}
        </Box>
      )}
    </FieldContainer>
  );
};
