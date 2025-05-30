import { FC } from 'react';
import { useUpdateEffect } from 'react-use';
import { useTranslation, Trans } from 'react-i18next';
import { useFormContext, useWatch } from 'react-hook-form';
import { makeStyles } from '@material-ui/core';
import { mui, YBToggleField, YBTooltip, YBInputField } from '@yugabyte-ui-library/core';
import { YBEarlyAccessTag } from '../../../../components';
// import { isVersionConnectionPoolSupported } from '../../../../features/universe/universe-form/utils/helpers';

import { DatabaseSettingsProps } from '../../steps/database-settings/dtos';
import { YSQL_FIELD } from '../ysql-settings/YSQLSettingsField';

const { Box, Typography } = mui;

import { ReactComponent as NextLineIcon } from '../../../../assets/next-line.svg';

interface ConnectionPoolFieldProps {
  disabled: boolean;
}

const CONNECTION_POOLING_FIELD = 'enableConnectionPooling';

const useStyles = makeStyles((theme) => ({
  subText: {
    fontSize: '11.5px',
    lineHeight: '18px',
    fontWeight: 400,
    color: '#4E5F6D'
  }
}));

export const ConnectionPoolingField: FC<ConnectionPoolFieldProps> = ({ disabled }) => {
  const { control, setValue } = useFormContext<DatabaseSettingsProps>();
  const { t } = useTranslation();
  const classes = useStyles();

  //watchers
  const isYSQLEnabled = useWatch({ name: YSQL_FIELD });
  const isConPoolEnabled = useWatch({ name: CONNECTION_POOLING_FIELD });
  const isOverrideCPEnabled = useWatch({ name: 'overrideCPPorts' });
  //   const dbVersionValue = useWatch({ name: SOFTWARE_VERSION_FIELD });

  const isConnectionPoolSupported = true;

  useUpdateEffect(() => {
    if (!isYSQLEnabled) setValue(CONNECTION_POOLING_FIELD, false);
  }, [isYSQLEnabled]);

  //   useUpdateEffect(() => {
  //     //set toggle to false if unsupported db version is selected
  //     if (!isVersionConnectionPoolSupported(dbVersionValue))
  //       setValue(CONNECTION_POOLING_FIELD, false);
  //   }, [dbVersionValue]);

  return (
    <Box
      sx={{
        display: 'flex',
        flexDirection: 'column',
        width: '548px',
        backgroundColor: '#FBFCFD',
        border: '1px solid #D7DEE4',
        borderRadius: '8px'
      }}
    >
      <Box sx={{ display: 'flex', flexDirection: 'column', padding: '16px 24px' }}>
        <Box sx={{ display: 'flex', flexDirection: 'row', alignItems: 'center', gap: '12px' }}>
          <YBTooltip
            title={
              isYSQLEnabled ? (
                isConnectionPoolSupported ? (
                  ''
                ) : (
                  <Typography className={classes.subText}>
                    {t('universeForm.advancedConfig.conPoolVersionTooltip')}
                  </Typography>
                )
              ) : (
                <Typography className={classes.subText}>
                  {t('universeForm.advancedConfig.conPoolYSQLWarn')}
                </Typography>
              )
            }
          >
            <div>
              <YBToggleField
                name={CONNECTION_POOLING_FIELD}
                inputProps={{
                  'data-testid': 'PGCompatibiltyField-Toggle'
                }}
                control={control}
                disabled={disabled || !isYSQLEnabled || !isConnectionPoolSupported}
                label={t('universeForm.advancedConfig.enableConnectionPooling')}
              />
            </div>
          </YBTooltip>
          <YBEarlyAccessTag />
        </Box>
        <Box sx={{ ml: 5 }}>
          <Typography className={classes.subText}>
            <Trans>{t('universeForm.advancedConfig.conPoolTooltip')}</Trans>
          </Typography>
        </Box>
      </Box>
      {/* If CP Enabled */}
      {isConPoolEnabled && (
        <Box
          sx={{
            display: 'flex',
            flexDirection: 'column',
            padding: '16px 24px',
            borderTop: '1px solid #D7DEE4'
          }}
        >
          <Box sx={{ display: 'flex', flexDirection: 'row', alignItems: 'center' }}>
            <NextLineIcon />
            <Box sx={{ marginBottom: '-5px', mr: 1, ml: 2 }}>
              <YBToggleField
                name={'overrideCPPorts'}
                control={control}
                label="Override related YSQL Ports (Optional)"
              />
            </Box>
          </Box>
          {isOverrideCPEnabled && (
            <Box
              sx={{
                display: 'flex',
                flexDirection: 'column',
                ml: 5,
                pt: 2,
                pl: 1,
                gap: '16px',
                width: '200px'
              }}
            >
              <YBInputField
                name={'ysqlServerRpcPort'}
                control={control}
                label="YSQL Port"
                helperText="Default 5433"
              />
              <YBInputField
                name={'internalYsqlServerRpcPort'}
                control={control}
                label="Internal YSQL Port"
                helperText="Default 6433"
              />
            </Box>
          )}
        </Box>
      )}
    </Box>
  );
};
