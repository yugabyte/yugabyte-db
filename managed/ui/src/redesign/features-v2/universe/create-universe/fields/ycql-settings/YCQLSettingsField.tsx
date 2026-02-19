/*
 * Created on Tue Mar 25 2025
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext, useWatch } from 'react-hook-form';
import { mui, YBToggleField, YBPasswordField, YBTooltip } from '@yugabyte-ui-library/core';
import { FieldContainer } from '../../components/DefaultComponents';
import { DatabaseSettingsProps } from '../../steps/database-settings/dtos';
import { YSQL_FIELD } from '../ysql-settings/YSQLSettingsField';

//icons
import NextLineIcon from '../../../../../assets/next-line.svg';
import InfoIcon from '../../../../../assets/info-new.svg';

const { Box } = mui;

interface YCQLProps {
  disabled?: boolean;
}

export const YCQL_FIELD = 'ycql.enable';
const YCQL_AUTH_FIELD = 'ycql.enable_auth';
const YCQL_PASSWORD_FIELD = 'ycql.password';
const YCQL_CONFIRM_PWD = 'ycql.confirm_pwd';

export const YCQField: FC<YCQLProps> = ({ disabled }) => {
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
            title={!ysqlEnabled && ycqlEnabled ? (t('enableYsqlOrYcql') as string) : ''}
            placement="top-start"
          >
            <div
              style={{ display: 'flex', flexDirection: 'row', gap: '4px', alignItems: 'center' }}
            >
              <YBToggleField
                name={YCQL_FIELD}
                control={control}
                label={t('ycqlSettings.toggleLabel')}
                dataTestId="ycql-settings-field"
                disabled={!ysqlEnabled || disabled}
              />
              <span>
                <InfoIcon />
              </span>
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
            padding: '16px 24px'
          }}
        >
          <Box
            sx={{
              display: 'flex',
              flexDirection: 'row',
              alignItems: 'center',
              gap: '16px',
              marginBottom: '-5px'
            }}
          >
            <span>
              <NextLineIcon />
            </span>
            <Box sx={{ mr: 1 }}>
              <YBToggleField
                name={YCQL_AUTH_FIELD}
                control={control}
                label={t('ycqlSettings.authToggleLabel')}
                dataTestId="ycql-settings-auth-field"
              />
            </Box>
          </Box>
          {ycqlAuthEnabled && (
            <Box
              sx={{
                display: 'flex',
                flexDirection: 'column',
                gap: '24px',
                padding: '16px 0px 16px 40px'
              }}
            >
              <YBPasswordField
                name={YCQL_PASSWORD_FIELD}
                control={control}
                placeholder={t('ycqlSettings.authPwd')}
                label={t('ycqlSettings.authPwd')}
                dataTestId="ycql-settings-auth-pwd-field"
              />
              <YBPasswordField
                name={YCQL_CONFIRM_PWD}
                control={control}
                placeholder={t('ycqlSettings.authConfirmPwd')}
                label={t('ycqlSettings.authConfirmPwd')}
                dataTestId="ycql-settings-auth-confirm-pwd-field"
              />
            </Box>
          )}
        </Box>
      )}
    </FieldContainer>
  );
};
